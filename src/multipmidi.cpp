#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <lv2.h>
#include <map>
#include <iostream>

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/log/logger.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include "lv2/lv2plug.in/ns/ext/time/time.h"
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>

/**********************************************************************************************************************************************************/
#define MIN(a,b) ( (a) < (b) ? (a) : (b) )
#define MAX(a,b) ( (a) > (b) ? (a) : (b) )
#define RAIL(v, min, max) (MIN((max), MAX((min), (v))))
#define ROUND(v) (uint32_t(v + 0.5f))
#define EVENT_NOTE_ON 0x90
#define EVENT_NOTE_OFF 0x80
#define SNARE_GROUP_LEN 6


#define PLUGIN_URI "http://plujain/plugins/multipmidi"
enum {
    SEQ_IN,
    IMPACT_IN,
    KICK_IN,
    MIDI_OUT,
    KICK_SPACING,
    MUTE,
    OPEN_TIME,
    KS_DEMUTE,
    VELO_1,
    PLUGIN_PORT_COUNT};

enum {BYPASS, FIRST_WAITING_PERIOD, WAITING_SIGNAL, FIRST_PERIOD, EFFECT, OUTING};

enum {MODE_ACTIVE, MODE_THRESHOLD, MODE_HOST_TRANSPORT, MODE_MIDI, MODE_MIDI_BYPASS};


typedef struct {
	LV2_URID atom_Blank;
	LV2_URID atom_Object;
	LV2_URID atom_Sequence;
	LV2_URID midi_MidiEvent;
	LV2_URID atom_Float;
	LV2_URID atom_Int;
	LV2_URID atom_Long;
	LV2_URID time_Position;
	LV2_URID time_bar;
	LV2_URID time_barBeat;
	LV2_URID time_beatUnit;
	LV2_URID time_beatsPerBar;
	LV2_URID time_beatsPerMinute;
	LV2_URID time_speed;
} PluginURIs;

/**********************************************************************************************************************************************************/

static void
map_mem_uris (LV2_URID_Map* map, PluginURIs* uris)
{
	uris->atom_Blank          = map->map (map->handle, LV2_ATOM__Blank);
	uris->atom_Object         = map->map (map->handle, LV2_ATOM__Object);
	uris->midi_MidiEvent      = map->map (map->handle, LV2_MIDI__MidiEvent);
	uris->atom_Sequence       = map->map (map->handle, LV2_ATOM__Sequence);
	uris->time_Position       = map->map (map->handle, LV2_TIME__Position);
	uris->atom_Long           = map->map (map->handle, LV2_ATOM__Long);
	uris->atom_Int            = map->map (map->handle, LV2_ATOM__Int);
	uris->atom_Float          = map->map (map->handle, LV2_ATOM__Float);
	uris->time_bar            = map->map (map->handle, LV2_TIME__bar);
	uris->time_barBeat        = map->map (map->handle, LV2_TIME__barBeat);
	uris->time_beatUnit       = map->map (map->handle, LV2_TIME__beatUnit);
	uris->time_beatsPerBar    = map->map (map->handle, LV2_TIME__beatsPerBar);
	uris->time_beatsPerMinute = map->map (map->handle, LV2_TIME__beatsPerMinute);
	uris->time_speed          = map->map (map->handle, LV2_TIME__speed);
}


class Multipmidi
{
public:
    Multipmidi() {};
    ~Multipmidi() {};
    static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double samplerate, const char* bundle_path, const LV2_Feature* const* features);
    static void activate(LV2_Handle instance);
    static void deactivate(LV2_Handle instance);
    static void connect_port(LV2_Handle instance, uint32_t port, void *data);
    static void run(LV2_Handle instance, uint32_t n_samples);
    static void cleanup(LV2_Handle instance);

    static const void* extension_data(const char* uri);

    void run_it(uint32_t n_samples);
    bool note_allowed(uint16_t note_mem, uint32_t frame, uint32_t frame_spacing, bool from_impact);
    
    const LV2_Atom_Sequence *seq_in;
    const LV2_Atom_Sequence *impact_in;
    const LV2_Atom_Sequence *kick_in;
    LV2_Atom_Sequence *midi_out;
    float *kick_spacing;
    float *mute;
    float *open_time;
    float *ks_demute;
    float *velocity_1;
    
    double samplerate;

    bool _kick_on;
    uint64_t _kick_on_frame;

    bool _muting;

    uint32_t _note_on_frame[0xfff];
    bool _note_is_on[0xfff];
    bool _note_comes_from_impact[0xfff];

    bool _demuted;
    uint16_t _snare_mute_group[6] = {0x000 | 37, 0x000 | 38, 0x000 | 39,
                                     0x000 | 40, 0x000 | 41, 0x000 | 42};
    uint16_t _hihat_mute_group[6] = {0x000 | 44, 0x000 | 45, 0x000 | 46,
                                     0x000 | 47, 0x000 | 48, 0x000 | 49};

    uint64_t _frame_count;
    /* LV2 Output */
	LV2_Log_Log* log;
	LV2_Log_Logger logger;
    
    LV2_Atom_Forge forge;
	LV2_Atom_Forge_Frame frame;
    
    LV2_URID_Map* map;
    
	PluginURIs uris;
};

/**********************************************************************************************************************************************************/

static const LV2_Descriptor Descriptor = {
    PLUGIN_URI,
    Multipmidi::instantiate,
    Multipmidi::connect_port,
    Multipmidi::activate,
    Multipmidi::run,
    Multipmidi::deactivate,
    Multipmidi::cleanup,
    Multipmidi::extension_data
};


LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    if (index == 0) return &Descriptor;
    else return NULL;
}

/**********************************************************************************************************************************************************/

LV2_Handle Multipmidi::instantiate(const LV2_Descriptor* descriptor, double samplerate, const char* bundle_path, const LV2_Feature* const* features)
{
    Multipmidi *plugin = new Multipmidi();
    
    plugin->samplerate = samplerate;

    int i;
	for (i=0; features[i]; ++i) {
		if (!strcmp (features[i]->URI, LV2_URID__map)) {
			plugin->map = (LV2_URID_Map*)features[i]->data;
		} else if (!strcmp (features[i]->URI, LV2_LOG__log)) {
			plugin->log = (LV2_Log_Log*)features[i]->data;
		}
	}
	
	lv2_log_logger_init (&plugin->logger, plugin->map, plugin->log);
    
    if (!plugin->map) {
		lv2_log_error (&plugin->logger,
                       "Multipmidi.lv2 error: Host does not support urid:map\n");
		free (plugin);
		return NULL;
	}

    for (uint16_t i=0; i<=0xfff; ++i){
        plugin->_note_is_on[i] = false;
        plugin->_note_comes_from_impact[i] = false;
        plugin->_note_on_frame[i] = 0;
    }

    plugin->_kick_on = false;
    plugin->_kick_on_frame = 0;

    plugin->_muting = false;

    plugin->_frame_count = 0;
    plugin->_demuted = false;

    lv2_atom_forge_init (&plugin->forge, plugin->map);
    map_mem_uris(plugin->map, &plugin->uris);
    
    return (LV2_Handle)plugin;
}

/*******************************/

void Multipmidi::activate(LV2_Handle instance)
{
    // TODO: include the activate function code here
}

/****************************/

void Multipmidi::deactivate(LV2_Handle instance)
{
    // TODO: include the deactivate function code here
}
        

/***************************/

void Multipmidi::connect_port(LV2_Handle instance, uint32_t port, void *data)
{
    Multipmidi *plugin;
    plugin = (Multipmidi *) instance;

    switch (port)
    {
        case SEQ_IN:
            plugin->seq_in = (const LV2_Atom_Sequence*) data;
            break;
        case IMPACT_IN:
            plugin->impact_in = (const LV2_Atom_Sequence*) data;
            break;
        case KICK_IN:
            plugin->kick_in = (const LV2_Atom_Sequence*) data;
            break;
        case MIDI_OUT:
            plugin->midi_out = (LV2_Atom_Sequence*) data;
            break;
        case KICK_SPACING:
            plugin->kick_spacing = (float*) data;
            break;
        case MUTE:
            plugin->mute = (float*) data;
            break;
        case OPEN_TIME:
            plugin->open_time = (float*) data;
            break;
        case KS_DEMUTE:
            plugin->ks_demute = (float*) data;
            break;
        case VELO_1:
            plugin->velocity_1 = (float*) data;
            break;
    }
        
}

/********************/

void Multipmidi::run(LV2_Handle instance, uint32_t n_samples)
{
    Multipmidi *plugin;
    plugin = (Multipmidi *) instance;
    plugin->run_it(n_samples);
}

/**********************************************************************************************************************************************************/

void Multipmidi::cleanup(LV2_Handle instance)
{
    delete ((Multipmidi *) instance);
}

/**********************************************************************************************************************************************************/

const void* Multipmidi::extension_data(const char* uri)
{
    return NULL;
}

bool Multipmidi::note_allowed(
        uint16_t note_mem, uint32_t frame, uint32_t frame_spacing, bool from_impact){
    /* check if the note is allowed to be played depending if it comes from
    impact or seq, and if it has just been played, or if a note in the same mute group
    (snare, hihat) has just been played */

    uint64_t frame_count = _frame_count + frame;

    if (frame_count - _note_on_frame[note_mem] <= frame_spacing
            && _note_comes_from_impact[note_mem]){
        return false;
    }

    /* Check SNARE mute group*/
    for (uint8_t i=0; i<6;++i){
        if (note_mem == _snare_mute_group[i]){
            for (uint8_t j=0; j<6;++j){
                if (frame_count - _note_on_frame[_snare_mute_group[j]] <= frame_spacing
                        && (from_impact || _note_comes_from_impact[_snare_mute_group[j]])){
                    return false;
                }
            }
            break;
        }
    }

    /* Check HiHat mute group*/
    for (uint8_t i=0; i<6;++i){
        if (note_mem == _hihat_mute_group[i]){
            for (uint8_t j=0; j<6;++j){
                if (frame_count - _note_on_frame[_hihat_mute_group[j]] <= frame_spacing
                        && (from_impact || _note_comes_from_impact[_hihat_mute_group[j]])){
                    return false;
                }
            }
            break;
        }
    }
    return true;
}

void Multipmidi::run_it(uint32_t n_samples){
    /* set midi_out port for midi messages*/
    const uint32_t capacity = midi_out->atom.size;
    lv2_atom_forge_set_buffer (&forge, (uint8_t*)midi_out, capacity);
    lv2_atom_forge_sequence_head (&forge, &frame, 0);

    uint8_t* orig_msg;
    uint8_t full_command;
    uint8_t command;
    uint8_t chan;

    uint32_t frame_spacing = *kick_spacing * 0.001 * samplerate;
    uint32_t frame_open_time = *open_time * 0.001 * samplerate;
    float velo_ratio_1 = *velocity_1 / 127.0;

    LV2_Atom_Event* ev;

    // check if we need to demute the plugin
    if (! _kick_on && _muting){
        ev = lv2_atom_sequence_begin (&(seq_in)->body);
        while (!lv2_atom_sequence_is_end (&(seq_in)->body,
                                          (seq_in)->atom.size, ev)) {
            if (ev->body.type == uris.midi_MidiEvent){
                orig_msg = ((uint8_t*)(ev+1));
                if ((orig_msg[0] & 0xF0) == EVENT_NOTE_ON
                        && (orig_msg[0] & 0x0F) <= 1 // channel <= 2nd
                        && orig_msg[1] < 43){        // note < 43
                    _demuted = true;
                    _muting = false;
                    break;
                }
            }
            ev = lv2_atom_sequence_next (ev);
        }
    }

    /* Set kick_on var true or false with note on/off */
    LV2_Atom_Event* kick_ev = lv2_atom_sequence_begin (&(kick_in)->body);
    while (!lv2_atom_sequence_is_end (&(kick_in)->body,
                                      (kick_in)->atom.size,
                                      kick_ev)) {
        if (kick_ev->body.type == uris.midi_MidiEvent){
            orig_msg = ((uint8_t*)(kick_ev+1));
            if ((orig_msg[0] & 0xF0) == 0x90){
                _kick_on = true;
                _kick_on_frame = _frame_count + kick_ev->time.frames;
                _muting = false;
            } else if ((orig_msg[0] & 0xF0) == 0x80){
                _kick_on = false;
                if (*ks_demute < 0.5){
                    _muting = false;
                }
            }
        }
        kick_ev = lv2_atom_sequence_next(kick_ev);
    }

    uint16_t note_mem;

    /* remember note coming from impact */
    LV2_Atom_Event* imp_ev = lv2_atom_sequence_begin (&(impact_in)->body);
    while (!lv2_atom_sequence_is_end (&(impact_in)->body,
                                      (impact_in)->atom.size,
                                      imp_ev)) {
        if (imp_ev->body.type == uris.midi_MidiEvent){
            orig_msg = ((uint8_t*)(imp_ev+1));
            
            if ((orig_msg[0] & 0xF0) == 0xF0){
                /* Ignore system messages */
                imp_ev = lv2_atom_sequence_next(imp_ev);
                continue;
            }

            chan = orig_msg[0] & 0x0F;
            note_mem = chan * 0x100 + orig_msg[1];

            if ((orig_msg[0] & 0xF0) == 0x90){
                if (! note_allowed(note_mem, 0, frame_spacing, true)){
                    imp_ev = lv2_atom_sequence_next(imp_ev);
                    continue;
                }
                _note_is_on[note_mem] = true;
                _note_on_frame[note_mem] = _frame_count + imp_ev->time.frames;
                _note_comes_from_impact[note_mem] = true;
            } else if ((orig_msg[0] & 0xF0) == 0x80){
                _note_is_on[note_mem] = false;
            }

            LV2_Atom midiatom;
            midiatom.type = uris.midi_MidiEvent;
            midiatom.size = 3;
            
            uint8_t msg[3];
            msg[0] = orig_msg[0];
            msg[1] = orig_msg[1]; /* Note number */
            if ((orig_msg[0] & 0xF0) == 0x90) msg[2] = orig_msg[2] * velo_ratio_1; /* Velocity */
            else msg[2] = orig_msg[2];

            /* Because midi messages must be written in frames order,
               and because this is a live plugin, choose is to send this impact on frame 0.*/
            if (0 == lv2_atom_forge_frame_time (&forge, 0)) return;
            // if (0 == lv2_atom_forge_frame_time (&forge, imp_ev->time.frames)) return;
            if (0 == lv2_atom_forge_raw (&forge, &midiatom, sizeof (LV2_Atom))) return;
            if (0 == lv2_atom_forge_raw (&forge, msg, 3)) return;
            lv2_atom_forge_pad (&forge, sizeof (LV2_Atom) + 3);
        }
        imp_ev = lv2_atom_sequence_next(imp_ev);
    }

    /* process midi events coming from seq input */
    ev = lv2_atom_sequence_begin (&(seq_in)->body);
    while (!lv2_atom_sequence_is_end (&(seq_in)->body, (seq_in)->atom.size, ev)) {
        if (ev->body.type == uris.midi_MidiEvent){
            orig_msg = ((uint8_t*)(ev+1));
            full_command = orig_msg[0];

            if ((full_command & 0xF0) == 0xF0){
                ev = lv2_atom_sequence_next (ev);
                continue;
            }

            command = full_command & ~0x0F;
            chan = full_command & 0x0F;

            if (_muting && command == EVENT_NOTE_ON){
                ev = lv2_atom_sequence_next (ev);
                continue;
            }

            note_mem = chan * 0x100 + orig_msg[1];

            /* Ignore the note under conditions*/
            if (command == EVENT_NOTE_ON){
                if (! note_allowed(note_mem, ev->time.frames, frame_spacing, false)){
                    ev = lv2_atom_sequence_next (ev);
                    continue;
                }

                /* Store the note*/
                _note_is_on[note_mem] = true;
                _note_comes_from_impact[note_mem] = false;
                _note_on_frame[note_mem] = _frame_count + ev->time.frames;

            } else if (command == 0x80){
                _note_is_on[note_mem] = false;
            }

            LV2_Atom midiatom;
            midiatom.type = uris.midi_MidiEvent;
            midiatom.size = 3;
            
            uint8_t msg[3];
            msg[0] = full_command;
            msg[1] = orig_msg[1]; /* Note number */
            if (command == 0x90) msg[2] = orig_msg[2] * velo_ratio_1; /* Velocity */
            else msg[2] = orig_msg[2];

            if (0 == lv2_atom_forge_frame_time (&forge, ev->time.frames)) return;
            if (0 == lv2_atom_forge_raw (&forge, &midiatom, sizeof (LV2_Atom))) return;
            if (0 == lv2_atom_forge_raw (&forge, msg, 3)) return;
            lv2_atom_forge_pad (&forge, sizeof (LV2_Atom) + 3);
        }
        ev = lv2_atom_sequence_next (ev);
    }

    _frame_count += n_samples;

    if (_kick_on && _frame_count - _kick_on_frame >= frame_open_time){
        _muting = true;
    }
}