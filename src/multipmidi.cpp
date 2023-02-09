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


#define PLUGIN_URI "http://plujain/plugins/multipmidi"
enum {
    MIDI_IN,
    MIDI_OUT,
    KICK_SPACING,
    MUTE,
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
    
    const LV2_Atom_Sequence *midi_in;
    LV2_Atom_Sequence *midi_out;
    float *kick_spacing;
    float *mute;
    float *velocity_1;
    
    double samplerate;

    uint32_t note_on_frame[0xfff];
    bool note_is_on[0xfff];
    bool note_comes_from_high[0xfff];

    uint64_t frame_count;
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
        plugin->note_is_on[i] = false;
        plugin->note_comes_from_high[i] = false;
        plugin->note_on_frame[i] = 0;
    }

    plugin->frame_count = 0;

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
        case MIDI_IN:
            plugin->midi_in = (const LV2_Atom_Sequence*) data;
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
    
    /* set midi_out port for midi messages*/
    const uint32_t capacity = plugin->midi_out->atom.size;
    lv2_atom_forge_set_buffer (&plugin->forge, (uint8_t*)plugin->midi_out, capacity);
    lv2_atom_forge_sequence_head (&plugin->forge, &plugin->frame, 0);

    uint8_t* orig_msg;
    uint8_t full_command;
    uint8_t command;
    uint8_t chan;

    uint32_t frame_spacing = *plugin->kick_spacing * 0.001 * plugin->samplerate;
    float velo_ratio_1 = *plugin->velocity_1 / 127.0;

    /* process control kick events 
        note 36 on the last channel */
    LV2_Atom_Event* ev = lv2_atom_sequence_begin (&(plugin->midi_in)->body);
    while (!lv2_atom_sequence_is_end (&(plugin->midi_in)->body, (plugin->midi_in)->atom.size, ev)) {
        if (ev->body.type == plugin->uris.midi_MidiEvent){
            orig_msg = ((uint8_t*)(ev+1));
            full_command = orig_msg[0];
            uint8_t new_chan;

            if ((full_command & 0xF0) == 0xF0){
                ev = lv2_atom_sequence_next (ev);
                continue;
            }

            command = full_command & ~0x0F;
            chan = full_command & 0x0F;
            if (chan >= 0x08){
                new_chan = chan - 0x08;
            } else {
                new_chan = chan;
            }

            if (*plugin->mute > 0.5 && command == 0x90){
                ev = lv2_atom_sequence_next (ev);
                continue;
            }

            uint16_t note_mem = new_chan * 0x100 + orig_msg[1];
            if (command == 0x90){
                uint64_t frame_count = plugin->frame_count + ev->time.frames;
                if (plugin->note_is_on[note_mem]
                        && frame_count - plugin->note_on_frame[note_mem] <= frame_spacing
                        && (chan >= 0x08 || plugin->note_comes_from_high[note_mem])){
                    ev = lv2_atom_sequence_next (ev);
                    continue;
                }

                plugin->note_is_on[note_mem] = true;
                plugin->note_comes_from_high[note_mem] = bool(chan >= 0x08);
                plugin->note_on_frame[note_mem] = frame_count;

            } else if (command == 0x80){
                // plugin->note_is_on[note_mem] = false;
                ;
            }

            LV2_Atom midiatom;
            midiatom.type = plugin->uris.midi_MidiEvent;
            midiatom.size = 3;
            
            uint8_t msg[3];
            msg[0] = command | new_chan; /* Note on or off with channel revert*/
            msg[1] = orig_msg[1]; /* Note number */
            if (command == 0x90) msg[2] = orig_msg[2] * velo_ratio_1; /* Velocity */
            else msg[2] = orig_msg[2];

            if (0 == lv2_atom_forge_frame_time (&plugin->forge, ev->time.frames)) return;
            if (0 == lv2_atom_forge_raw (&plugin->forge, &midiatom, sizeof (LV2_Atom))) return;
            if (0 == lv2_atom_forge_raw (&plugin->forge, msg, 3)) return;
            lv2_atom_forge_pad (&plugin->forge, sizeof (LV2_Atom) + 3);

            // kick_sample = ev->time.frames;
        }
        ev = lv2_atom_sequence_next (ev);
    }

    plugin->frame_count += n_samples;
    // /* Process all other MIDI messages */
    // LV2_Atom_Event* m_ev = lv2_atom_sequence_begin (&(plugin->midi_in)->body);
    // while (!lv2_atom_sequence_is_end (&(plugin->midi_in)->body, (plugin->midi_in)->atom.size, m_ev)) {
    //     if (m_ev->body.type == plugin->uris.midi_MidiEvent) {
    //         orig_msg = ((uint8_t*)(m_ev+1));
    //         full_command = orig_msg[0];

    //         if ((full_command & 0xF0) == 0xF0){
    //             command = full_command;
    //             chan = 0;
    //         } else {
    //             command = full_command & ~0x0F;
    //             chan = full_command & 0x0F;
    //         }

    //         if (chan == 0x0F){
    //             m_ev = lv2_atom_sequence_next (m_ev);
    //             continue;
    //         }

    //         if (command == 0x90
    //                 and (not plugin->muting
    //                      or plugin->frames_since_last_order_kick < plugin->pre_mute_frames)){
    //             LV2_Atom midiatom;
    //             midiatom.type = plugin->uris.midi_MidiEvent;
    //             midiatom.size = 3;
                
    //             uint8_t msg[3];
    //             msg[0] = full_command; /* Note on or off with channel*/
    //             msg[1] = orig_msg[1]; /* Note number */
    //             msg[2] = orig_msg[2] * plugin->velocity_factor; /* Velocity */

    //             if (msg[1] == 36
    //                 and *plugin->play_kick > 0.5
    //                 and plugin->frames_since_last_order_kick < plugin->samplerate * (*plugin->kick_spacing / 1000.0)){
    //                 ;
    //             } else if (plugin->after_long_mute
    //                        && (chan >= uint8_t(*plugin->drum_channel_max + 0.5)
    //                            || msg[1] >= uint16_t(*plugin->demuter_note + 0.5))){
    //                 ;
    //             } else {
    //                 if (0 == lv2_atom_forge_frame_time (&plugin->forge, m_ev->time.frames)) return;
    //                 if (0 == lv2_atom_forge_raw (&plugin->forge, &midiatom, sizeof (LV2_Atom))) return;
    //                 if (0 == lv2_atom_forge_raw (&plugin->forge, msg, 3)) return;
    //                 lv2_atom_forge_pad (&plugin->forge, sizeof (LV2_Atom) + 3);
    //             }

    //             if (msg[1] == 36){
    //                 kick_sample = m_ev->time.frames;
    //                 plugin->frames_since_last_kick = 0;
    //             }

    //             if (plugin->after_long_mute
    //                     && command == 0x90
    //                     && chan < uint8_t(*plugin->drum_channel_max + 0.5)
    //                     && msg[1] < uint16_t(*plugin->demuter_note + 0.5)){
    //                 plugin->after_long_mute = false;
    //             }

    //         } else if (command != 0x90){
    //             LV2_Atom midiatom;
    //             midiatom.type = plugin->uris.midi_MidiEvent;
    //             midiatom.size = m_ev->body.size;

    //             if (0 == lv2_atom_forge_frame_time (&plugin->forge, m_ev->time.frames)) return;
    //             if (0 == lv2_atom_forge_raw (&plugin->forge, &midiatom, sizeof (LV2_Atom))) return;
    //             if (0 == lv2_atom_forge_raw (&plugin->forge, (m_ev+1), 3)) return;
    //             lv2_atom_forge_pad (&plugin->forge, sizeof (LV2_Atom) + 3);
    //         }
    //     }
    //     m_ev = lv2_atom_sequence_next (m_ev);
    // }
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
