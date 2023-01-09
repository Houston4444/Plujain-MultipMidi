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
enum {MIDI_IN, MIDI_OUT, KICK_IN, PLAY_KICK, STOP_TIME, PLUGIN_PORT_COUNT};

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
    
    const LV2_Atom_Sequence *midi_in;
    LV2_Atom_Sequence *midi_out;
    const LV2_Atom_Sequence *kick_in;
    float *in_1;
    float *in_2;
    float *out;
    float *active;
    
    double samplerate;
    
    float velocity_factor;
    uint64_t frames_since_last_kick;
    bool muting;

    bool ex_active_state;
    
    int period_count;
    int period_length;
//     int perior
    bool attacking;
    
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
    plugin->ex_active_state = false;
    
    plugin->period_count = 0;
    plugin->period_length = 10000;
//     plugin->perior = 0;
    plugin->attacking = false;

    plugin->velocity_factor = 1.0;
    plugin->frames_since_last_kick = 0;
    plugin->muting = false;
    
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
            // plugin->in_1 = (float*) data;
            plugin->midi_in = (const LV2_Atom_Sequence*) data;
            break;
        case MIDI_OUT:
            // plugin->in_2 = (float*) data;
            plugin->midi_out = (LV2_Atom_Sequence*) data;
            break;
        case KICK_IN:
            plugin->kick_in = (const LV2_Atom_Sequence*) data;
            break;
        case PLAY_KICK:
            // plugin->out = (float*) data;
            break;
        case STOP_TIME:
            // plugin->active = (float*) data;
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

    /* process control events (for host transport) */
    LV2_Atom_Event* ev = lv2_atom_sequence_begin (&(plugin->kick_in)->body);
    while (!lv2_atom_sequence_is_end (&(plugin->kick_in)->body, (plugin->kick_in)->atom.size, ev)) {
        if (ev->body.type == plugin->uris.midi_MidiEvent) {
            if (((const uint8_t*)(ev+1))[0] == 0x90){
                // lo::Address a ("localhost", "2354");
                // a.send("/play");
                plugin->velocity_factor = ((const uint8_t*)(ev+1))[2] / 127.0;
                plugin->muting = true;
            } else if (((const uint8_t*)(ev+1))[0] == 0x80){
                plugin->muting = false;
            }
        }

        if (((const uint8_t*)(ev+1))[0] & 0x90 or ((const uint8_t*)(ev+1))[0] & 0x80){
            LV2_Atom midiatom;
            midiatom.type = plugin->uris.midi_MidiEvent;
            midiatom.size = 3;
            
            uint8_t msg[3];
            msg[0] = ((const uint8_t*)(ev+1))[0]; /* Note on or off with channel*/
            msg[1] = ((const uint8_t*)(ev+1))[1]; /* Note number */
            msg[2] = ((const uint8_t*)(ev+1))[2]; /* Velocity */

            if (plugin->frames_since_last_kick > 3000){
                if (0 == lv2_atom_forge_frame_time (&plugin->forge, ev->time.frames)) return;
                if (0 == lv2_atom_forge_raw (&plugin->forge, &midiatom, sizeof (LV2_Atom))) return;
                if (0 == lv2_atom_forge_raw (&plugin->forge, msg, 3)) return;
                lv2_atom_forge_pad (&plugin->forge, sizeof (LV2_Atom) + 3);
                plugin->frames_since_last_kick = 0;
            }

        }
        ev = lv2_atom_sequence_next (ev);
    }

    LV2_Atom_Event* m_ev = lv2_atom_sequence_begin (&(plugin->midi_in)->body);
    while (!lv2_atom_sequence_is_end (&(plugin->midi_in)->body, (plugin->midi_in)->atom.size, m_ev)) {
        if (m_ev->body.type == plugin->uris.midi_MidiEvent) {
            if ((((const uint8_t*)(m_ev+1))[0] & 0x80 or ((const uint8_t*)(m_ev+1))[0] & 0x90)
                    and not plugin->muting){
                LV2_Atom midiatom;
                midiatom.type = plugin->uris.midi_MidiEvent;
                midiatom.size = 3;
                
                uint8_t msg[3];
                msg[0] = ((const uint8_t*)(m_ev+1))[0]; /* Note on or off with channel*/
                msg[1] = ((const uint8_t*)(m_ev+1))[1]; /* Note number */
                msg[2] = ((const uint8_t*)(m_ev+1))[2] * plugin->velocity_factor; /* Velocity */

                if (msg[1] == 36 and plugin->frames_since_last_kick < 3000){
                    ;
                } else {
                    if (0 == lv2_atom_forge_frame_time (&plugin->forge, m_ev->time.frames)) return;
                    if (0 == lv2_atom_forge_raw (&plugin->forge, &midiatom, sizeof (LV2_Atom))) return;
                    if (0 == lv2_atom_forge_raw (&plugin->forge, msg, 3)) return;
                    lv2_atom_forge_pad (&plugin->forge, sizeof (LV2_Atom) + 3);
                }

                if (msg[1] == 36){
                    plugin->frames_since_last_kick = 0;
                }

            }
        }
        m_ev = lv2_atom_sequence_next (m_ev);
    }

    plugin->frames_since_last_kick += n_samples;
 

    // bool active_state = bool(*plugin->active > 0.5f);
    
    // lo::Address a ("localhost", "2354");
    // a.send("/play");

    // for ( uint32_t i = 0; i < n_samples; i++)
    // {
    //     if (active_state){
    //         float current_value = float(plugin->in[i]);
    //         if (plugin->attacking){
    //             if (plugin->period_count < 200){
    //                 plugin->out[i] = (1 - plugin->period_count/float(200)) * current_value;
    //             } else {
    //                 plugin->out[i] = (plugin->period_count -200)/float(plugin->period_length -200) * current_value;
    //             }
                
    //             plugin->period_count++;
    //             if (plugin->period_count == plugin->period_length){
    //                 plugin->attacking = false;
    //             }
                
    //         } else {
    //             if (current_value > 0.2){
    //                 ;
    //             }
                    
    //     } else {
    //         plugin->out[i] = plugin->in_1[i];
    //     }
        
        
    // }
       
    // plugin->ex_active_state = active_state;
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
