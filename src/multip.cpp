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

#include "lo/lo.h"
#include "lo/lo_cpp.h"
/**********************************************************************************************************************************************************/
#define MIN(a,b) ( (a) < (b) ? (a) : (b) )
#define MAX(a,b) ( (a) > (b) ? (a) : (b) )
#define RAIL(v, min, max) (MIN((max), MAX((min), (v))))
#define ROUND(v) (uint32_t(v + 0.5f))

#define PLUGIN_URI "http://plujain/plugins/multip"
enum {
    MIDI_IN,
    MIDI_OUT,
    PLAY_KICK,
    KICK_SPACING,
    STOP_TIME,
    AVERAGE_TEMPO,
    N_TAPS,
    RESTART_CYCLE,
    MOVING_TEMPO,
    DEMUTER_NOTE,
    BIG_SEQUENCE,
    SEQ_OSC_PORT,
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


class Multip
{
public:
    Multip() {};
    ~Multip() {};
    static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double samplerate, const char* bundle_path, const LV2_Feature* const* features);
    static void activate(LV2_Handle instance);
    static void deactivate(LV2_Handle instance);
    static void connect_port(LV2_Handle instance, uint32_t port, void *data);
    static void run(LV2_Handle instance, uint32_t n_samples);
    static void cleanup(LV2_Handle instance);
    static const void* extension_data(const char* uri);
    std::string port_str();
    
    const LV2_Atom_Sequence *midi_in;
    LV2_Atom_Sequence *midi_out;
    float *play_kick;
    float *kick_spacing;
    float *stop_time;
    float *average_tempo;
    float *n_taps;
    float *restart_cycle;
    float *moving_tempo;
    float *demuter_note;
    float *big_sequence;
    float *seq_osc_port;
    
    double samplerate;
    uint32_t pre_mute_frames;
    
    float velocity_factor;
    uint64_t frames_since_last_order_kick;
    uint64_t frames_since_last_kick;
    uint64_t frames_since_last_random_change;
    bool muting;
    bool playing;
    bool after_long_mute;
    uint8_t n_taps_done;
    float current_tempo;

    uint8_t playing_velo_row;
    uint8_t playing_random_row;
    uint8_t last_big_sequence;

    
    /* Host Time */
	bool     host_info;
	float    host_bpm;
	double   bar_beats;
	float    host_speed;
	int      host_div;
    
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
    Multip::instantiate,
    Multip::connect_port,
    Multip::activate,
    Multip::run,
    Multip::deactivate,
    Multip::cleanup,
    Multip::extension_data
};

/**********************************************************************************************************************************************************/

static void
update_position (Multip* plugin, const LV2_Atom_Object* obj)
{
    /* code taken from x42 step sequencer */ 
	const PluginURIs* uris = &plugin->uris;

	LV2_Atom* bar   = NULL;
	LV2_Atom* beat  = NULL;
	LV2_Atom* bunit = NULL;
	LV2_Atom* bpb   = NULL;
	LV2_Atom* bpm   = NULL;
	LV2_Atom* speed = NULL;

	lv2_atom_object_get (
			obj,
			uris->time_bar, &bar,
			uris->time_barBeat, &beat,
			uris->time_beatUnit, &bunit,
			uris->time_beatsPerBar, &bpb,
			uris->time_beatsPerMinute, &bpm,
			uris->time_speed, &speed,
			NULL);

	if (   bpm   && bpm->type == uris->atom_Float
			&& bpb   && bpb->type == uris->atom_Float
			&& bar   && bar->type == uris->atom_Long
			&& beat  && beat->type == uris->atom_Float
			&& bunit && bunit->type == uris->atom_Int
			&& speed && speed->type == uris->atom_Float)
	{
		float    _bpb   = ((LV2_Atom_Float*)bpb)->body;
		int64_t  _bar   = ((LV2_Atom_Long*)bar)->body;
		float    _beat  = ((LV2_Atom_Float*)beat)->body;

		plugin->host_div   = ((LV2_Atom_Int*)bunit)->body;
		plugin->host_bpm   = ((LV2_Atom_Float*)bpm)->body;
		plugin->host_speed = ((LV2_Atom_Float*)speed)->body;

		plugin->bar_beats  = _bar * _bpb + _beat; // * host_div / 4.0 // TODO map host metrum
		plugin->host_info  = true;
	}
}
/**********************************************************************************************************************************************************/

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    if (index == 0) return &Descriptor;
    else return NULL;
}

/**********************************************************************************************************************************************************/

LV2_Handle Multip::instantiate(const LV2_Descriptor* descriptor, double samplerate, const char* bundle_path, const LV2_Feature* const* features)
{
    Multip *plugin = new Multip();
    
    plugin->samplerate = samplerate;

    plugin->pre_mute_frames = 0.050 * samplerate;
    plugin->velocity_factor = 1.0;
    plugin->frames_since_last_kick = 0;
    plugin->frames_since_last_order_kick = 0;
    plugin->frames_since_last_random_change = 0;
    plugin->muting = false;
    plugin->playing = false;
    plugin->after_long_mute = false;

    plugin->playing_velo_row = 2;
    plugin->playing_random_row = 2;
    plugin->last_big_sequence = 0;
    
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
                       "Multip.lv2 error: Host does not support urid:map\n");
		free (plugin);
		return NULL;
	}
    
    lv2_atom_forge_init (&plugin->forge, plugin->map);
    map_mem_uris(plugin->map, &plugin->uris);
    
    return (LV2_Handle)plugin;
}

/*******************************/

void Multip::activate(LV2_Handle instance)
{
    // TODO: include the activate function code here
}

/****************************/

void Multip::deactivate(LV2_Handle instance)
{
    // TODO: include the deactivate function code here
}
        

/***************************/

void Multip::connect_port(LV2_Handle instance, uint32_t port, void *data)
{
    Multip *plugin;
    plugin = (Multip *) instance;

    switch (port)
    {
        case MIDI_IN:
            plugin->midi_in = (const LV2_Atom_Sequence*) data;
            break;
        case MIDI_OUT:
            plugin->midi_out = (LV2_Atom_Sequence*) data;
            break;
        case PLAY_KICK:
            plugin->play_kick = (float*) data;
            break;
        case KICK_SPACING:
            plugin->kick_spacing = (float*) data;
            break;
        case STOP_TIME:
            plugin->stop_time = (float*) data;
            break;
        case AVERAGE_TEMPO:
            plugin->average_tempo = (float*) data;
            break;
        case N_TAPS:
            plugin->n_taps = (float*) data;
            break;
        case RESTART_CYCLE:
            plugin->restart_cycle = (float*) data;
            break;
        case MOVING_TEMPO:
            plugin->moving_tempo = (float*) data;
            break;
        case DEMUTER_NOTE:
            plugin->demuter_note = (float*) data;
            break;
        case BIG_SEQUENCE:
            plugin->big_sequence = (float*) data;
            break;
        case SEQ_OSC_PORT:
            plugin->seq_osc_port = (float*) data;
            break;
    }
        
}

/********************/


void Multip::run(LV2_Handle instance, uint32_t n_samples)
{
    Multip *plugin;
    plugin = (Multip *) instance;
    
    /* set midi_out port for midi messages*/
    const uint32_t capacity = plugin->midi_out->atom.size;
    lv2_atom_forge_set_buffer (&plugin->forge, (uint8_t*)plugin->midi_out, capacity);
    lv2_atom_forge_sequence_head (&plugin->forge, &plugin->frame, 0);

    uint32_t kick_sample = 0;
    uint32_t order_kick_sample = 0;
    uint32_t random_change_sample = 0;
    uint8_t big_sequence = *plugin->big_sequence + 0.5;

    if (big_sequence != plugin->last_big_sequence){
        lo::Address a ("localhost", plugin->port_str());
        a.send("/sequence/queue", "si", "off", plugin->last_big_sequence * 2);
        a.send("/sequence/queue", "si", "off", 1 + plugin->last_big_sequence * 2);
        a.send("/sequence/queue", "siii", "on", big_sequence * 2 , 0, 1);
        a.send("/sequence/queue", "siii", "on", 1 + big_sequence * 2 , 0, 1);
        a.send("/sequence/queue", "sii", "on", big_sequence * 2, plugin->playing_velo_row);
        a.send("/sequence/queue", "sii", "on", 1 + big_sequence * 2, plugin->playing_random_row);
    }

    /* process control kick events 
        note 36 on the last channel */
    LV2_Atom_Event* ev = lv2_atom_sequence_begin (&(plugin->midi_in)->body);
    while (!lv2_atom_sequence_is_end (&(plugin->midi_in)->body, (plugin->midi_in)->atom.size, ev)) {
        if (ev->body.type == plugin->uris.midi_MidiEvent
                and (((const uint8_t*)(ev+1))[0] == 0x9F
                     or ((const uint8_t*)(ev+1))[0] == 0x8F)) {
            if (((const uint8_t*)(ev+1))[0] == 0x9F){
                lo::Address a ("localhost", plugin->port_str());
                if (not plugin->playing){
                    uint8_t n_taps = *plugin->n_taps + 0.5;
                    bool start_play = false;

                    switch(n_taps){
                        case 0:
                            break;
                        case 1:
                            plugin->current_tempo = *plugin->average_tempo;
                            a.send("/bpm", "f", *plugin->average_tempo);
                            start_play = true;
                            break;
                        default:
                            plugin->n_taps_done += 1;

                            float bpm = (60.0
                                         / (plugin->frames_since_last_order_kick
                                            / plugin->samplerate));
                            bool valid_bpm = true;

                            if (bpm < *plugin->average_tempo / 5.0){
                                valid_bpm = false;
                            } else if (bpm < *plugin->average_tempo / 3.5){
                                bpm *= 4;
                            } else if (bpm < *plugin->average_tempo / 2.5){
                                bpm *= 3;
                            } else if (bpm < *plugin->average_tempo / 1.75){
                                bpm *= 2;
                            } else if (bpm < *plugin->average_tempo / 1.25){
                                bpm *= 1.5;
                            } else if (bpm < *plugin->average_tempo / 0.75){
                                ;
                            } else if (bpm < *plugin->average_tempo / 0.4){
                                bpm *= 0.5;
                            } else {
                                valid_bpm = false;
                            }

                            if (! valid_bpm) plugin->n_taps_done = 1;

                            if (valid_bpm && plugin->n_taps_done >= n_taps){
                                plugin->current_tempo = bpm;
                                a.send("/bpm", "f", bpm);
                                start_play = true;      
                            }
                            break;
                    }

                    if (start_play){
                        printf("ouhouh start play\n");
                        a.send("/panic");
                        a.send("/sequence", "siii", "on", big_sequence * 2, 0, 1);
                        a.send("/sequence", "siii", "on", 1 + big_sequence * 2, 0, 1);
                        a.send("/sequence", "sii", "on", big_sequence * 2, plugin->playing_velo_row);
                        a.send("/sequence", "sii", "on", 1 + big_sequence * 2, plugin->playing_random_row);
                        a.send("/play");
                        plugin->playing = true;
                    }

                } else if (*plugin->moving_tempo > 0.0){
                    float bpm = (60.0
                                    / (plugin->frames_since_last_order_kick
                                    / plugin->samplerate));
                    bool valid_bpm = true;

                    if (bpm < plugin->current_tempo / 5.0){
                        valid_bpm = false;
                    } else if (bpm < plugin->current_tempo / 3.5){
                        bpm *= 4;
                    } else if (bpm < plugin->current_tempo / 2.5){
                        bpm *= 3;
                    } else if (bpm < plugin->current_tempo / 1.75){
                        bpm *= 2;
                    } else if (bpm < plugin->current_tempo / 1.25){
                        bpm *= 1.5;
                    } else if (bpm < plugin->current_tempo / 0.75){
                        ;
                    } else if (bpm < plugin->current_tempo / 0.4){
                        bpm *= 0.5;
                    } else {
                        valid_bpm = false;
                    }

                    if (valid_bpm){
                        float new_ratio = *plugin->moving_tempo / 100.0;
                        float new_bpm = new_ratio * bpm + (1.0 - new_ratio) * plugin->current_tempo;
                        plugin->current_tempo = new_bpm;
                        lo::Address a ("localhost", plugin->port_str());
                        a.send("/bpm", "f", new_bpm);
                    }
                }

                if (*plugin->restart_cycle > 0.5){
                    lo::Address a ("localhost", plugin->port_str());
                    a.send("/play");
                }

                order_kick_sample = ev->time.frames;
                plugin->frames_since_last_order_kick = 0;
                plugin->velocity_factor = ((const uint8_t*)(ev+1))[2] / 127.0;
                plugin->muting = true;
                plugin->after_long_mute = false;

                /* choose velocity dependant sequence */
                uint8_t velo_row;

                if (((const uint8_t*)(ev+1))[2] <= 32){
                    velo_row = 2;
                } else if (((const uint8_t*)(ev+1))[2] <= 64){
                    velo_row = 3;
                } else if (((const uint8_t*)(ev+1))[2] <= 96){
                    velo_row = 4;
                } else {
                    velo_row = 5;
                }

                if (velo_row != plugin->playing_velo_row){
                    a.send("/sequence", "sii", "off", big_sequence * 2,
                           plugin->playing_velo_row);
                    a.send("/sequence", "sii", "on", big_sequence * 2, velo_row);
                    // printf("Velo row %d\n", velo_row);
                    plugin->playing_velo_row = velo_row;
                }

            } else if (((const uint8_t*)(ev+1))[0] == 0x8F){
                plugin->muting = false;
                if (plugin->frames_since_last_order_kick > 4800){
                    plugin->after_long_mute = true;
                }
            }

            LV2_Atom midiatom;
            midiatom.type = plugin->uris.midi_MidiEvent;
            midiatom.size = 3;
            
            uint8_t msg[3];
            msg[0] = ((const uint8_t*)(ev+1))[0]; /* Note on or off with channel*/
            msg[1] = ((const uint8_t*)(ev+1))[1]; /* Note number */
            msg[2] = ((const uint8_t*)(ev+1))[2]; /* Velocity */

            if (*plugin->play_kick > 0.5
                    and plugin->frames_since_last_kick
                        > plugin->samplerate * (*plugin->kick_spacing / 1000.0)){
                if (0 == lv2_atom_forge_frame_time (&plugin->forge, ev->time.frames)) return;
                if (0 == lv2_atom_forge_raw (&plugin->forge, &midiatom, sizeof (LV2_Atom))) return;
                if (0 == lv2_atom_forge_raw (&plugin->forge, msg, 3)) return;
                lv2_atom_forge_pad (&plugin->forge, sizeof (LV2_Atom) + 3);

                kick_sample = ev->time.frames;
                plugin->frames_since_last_kick = 0;
            }

        }
        ev = lv2_atom_sequence_next (ev);
    }

    uint8_t full_command;
    uint8_t command;
    uint8_t chan;

    /* Process all other MIDI messages */
    LV2_Atom_Event* m_ev = lv2_atom_sequence_begin (&(plugin->midi_in)->body);
    while (!lv2_atom_sequence_is_end (&(plugin->midi_in)->body, (plugin->midi_in)->atom.size, m_ev)) {
        if (m_ev->body.type == plugin->uris.midi_MidiEvent) {
            full_command = ((const uint8_t*)(m_ev+1))[0];

            if (full_command & 0xf0){
                command = full_command;
                chan = 0;
            } else {
                command = full_command & ~0x0F;
                chan = full_command & 0x0F;
            }

            if (chan == 0x0F){
                m_ev = lv2_atom_sequence_next (m_ev);
                continue;
            }

            if (command == 0x90
                    and (not plugin->muting
                         or plugin->frames_since_last_order_kick < plugin->pre_mute_frames)){
                if (chan != 0x0F){
                    LV2_Atom midiatom;
                    midiatom.type = plugin->uris.midi_MidiEvent;
                    midiatom.size = 3;
                    
                    uint8_t msg[3];
                    msg[0] = ((const uint8_t*)(m_ev+1))[0]; /* Note on or off with channel*/
                    msg[1] = ((const uint8_t*)(m_ev+1))[1]; /* Note number */
                    msg[2] = ((const uint8_t*)(m_ev+1))[2] * plugin->velocity_factor; /* Velocity */

                    if (msg[1] == 36
                        and *plugin->play_kick > 0.5
                        and plugin->frames_since_last_order_kick < plugin->samplerate * (*plugin->kick_spacing / 1000.0)){
                        ;
                    } else if (msg[1] == 30 and plugin->frames_since_last_random_change > 24000){
                        /* manage random sequences*/
                        srand((unsigned) time(NULL));
                        uint8_t random_row = plugin->playing_random_row;

                        while (random_row == plugin->playing_random_row){
                            random_row = 2 + (rand() % 5);
                        }

                        random_change_sample = ev->time.frames;
                        lo::Address a ("localhost", plugin->port_str());
                        a.send("/sequence", "sii", "off", 1 + big_sequence * 2,
                                plugin->playing_random_row);
                        a.send("/sequence", "sii", "on", 1 + big_sequence * 2,
                                random_row);
                        plugin->playing_random_row = random_row;
                        plugin->frames_since_last_random_change = 0;

                    } else if (plugin->after_long_mute && msg[1]
                                >= uint16_t(*plugin->demuter_note + 0.5)){
                        ;
                    } else {
                        if (0 == lv2_atom_forge_frame_time (&plugin->forge, m_ev->time.frames)) return;
                        if (0 == lv2_atom_forge_raw (&plugin->forge, &midiatom, sizeof (LV2_Atom))) return;
                        if (0 == lv2_atom_forge_raw (&plugin->forge, msg, 3)) return;
                        lv2_atom_forge_pad (&plugin->forge, sizeof (LV2_Atom) + 3);
                    }

                    if (msg[1] == 36){
                        kick_sample = m_ev->time.frames;
                        plugin->frames_since_last_kick = 0;
                    }

                    if (plugin->after_long_mute
                            && (msg[0] & ~0x0F) == 0x90
                            && msg[1] < uint16_t(*plugin->demuter_note + 0.5)){
                        plugin->after_long_mute = false;
                    }
                }

            } else if (command != 0x90){
                LV2_Atom midiatom;
                midiatom.type = plugin->uris.midi_MidiEvent;
                midiatom.size = m_ev->body.size;

                if (0 == lv2_atom_forge_frame_time (&plugin->forge, m_ev->time.frames)) return;
                if (0 == lv2_atom_forge_raw (&plugin->forge, &midiatom, sizeof (LV2_Atom))) return;
                if (0 == lv2_atom_forge_raw (&plugin->forge, (m_ev+1), 3)) return;
                lv2_atom_forge_pad (&plugin->forge, sizeof (LV2_Atom) + 3);
            }
        }
        m_ev = lv2_atom_sequence_next (m_ev);
    }

    if (plugin->playing and plugin->muting
            and plugin->frames_since_last_order_kick > (plugin->samplerate * (*plugin->stop_time / 1000.0))){
        lo::Address a ("localhost", plugin->port_str());
        a.send("/stop");
        plugin->playing = false;
        plugin->n_taps_done = 0;
    }

    plugin->frames_since_last_kick += n_samples - kick_sample;
    plugin->frames_since_last_order_kick += n_samples - order_kick_sample;
    plugin->frames_since_last_random_change += n_samples - random_change_sample;
    plugin->last_big_sequence = big_sequence;

}

/**********************************************************************************************************************************************************/

void Multip::cleanup(LV2_Handle instance)
{
    delete ((Multip *) instance);
}

/**********************************************************************************************************************************************************/

const void* Multip::extension_data(const char* uri)
{
    return NULL;
}

std::string Multip::port_str(){
    return std::to_string(uint16_t(*seq_osc_port));
}
