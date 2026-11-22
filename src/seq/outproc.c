/*
 * CARBON MIDI Output Processing
 *
 * Written by: Andrew Kilpatrick
 * Copyright 2015: Kilpatrick Audio
 *
 * CARBON is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CARBON is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CARBON.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include "outproc.h"
#include "scale.h"
#include "glide.h"
#include "../midi/midi_clock.h"
#include "seq_engine.h"
#include "song.h"
#include "../midi/midi_stream.h"
#include "../config.h"
#include "../util/log.h"
#include "../util/seq_utils.h"

// Each destination has an input identity and the actual sounding note.
#define OUTPROC_MAX_NOTES (16 * SEQ_NUM_TRACK_OUTPUTS)
struct outproc_note {
    struct midi_msg input;
    struct midi_msg output;
    uint32_t order;
    uint8_t destination;  // output A/B identity, independent of later remapping
};
struct outproc_state {
    struct outproc_note notes[SEQ_NUM_TRACKS][OUTPROC_MAX_NOTES];
    int current_transpose[SEQ_NUM_TRACKS];
    int current_tonality[SEQ_NUM_TRACKS];
    int current_root[SEQ_NUM_TRACKS];
    int damper[SEQ_NUM_TRACKS];
    uint32_t next_order;
    int8_t glide_owner[MIDI_PORT_NUM_TRACK_OUTPUTS][16];
    uint8_t glide_used[MIDI_PORT_NUM_TRACK_OUTPUTS][16];
    int8_t glide_cleanup_owner[MIDI_PORT_NUM_TRACK_OUTPUTS][16];
    int attack_duration, attack_curve;
};
struct outproc_state opstate;

int outproc_preview_pitch(int track, int note) {
    int scene = seq_engine_get_current_scene();
    int root = seq_engine_get_effective_root(scene);
    int scale = song_get_track_type(track) == SONG_TRACK_TYPE_DRUM
        ? opstate.current_tonality[track] : seq_engine_get_effective_tonality(scene, track);
    if(root >= 0 &&
            song_get_track_type(track) != SONG_TRACK_TYPE_DRUM) {
        return scale_quantize_root(note + seq_engine_get_internal_cc(track, 17,
            opstate.current_transpose[track]), scale, root);
    }
    if(!seq_utils_check_note_range(note)) return -1;
    return scale_quantize(note, scale) +
        opstate.current_transpose[track];
}

void outproc_reset_glide_owners(void) {
    for(int p = 0; p < MIDI_PORT_NUM_TRACK_OUTPUTS; p++)
        for(int c = 0; c < 16; c++) opstate.glide_owner[p][c] = -1;
}

void outproc_init(void) {
    int track, i;
    opstate.next_order = 0;
    opstate.attack_duration = 0;
    opstate.attack_curve = 64;
    for(int p = 0; p < MIDI_PORT_NUM_TRACK_OUTPUTS; p++)
        for(int c = 0; c < 16; c++) {
            opstate.glide_owner[p][c] = -1;
            opstate.glide_used[p][c] = 0;
            opstate.glide_cleanup_owner[p][c] = -1;
        }
    for(track = 0; track < SEQ_NUM_TRACKS; track ++) {
        for(i = 0; i < OUTPROC_MAX_NOTES; i ++)
            opstate.notes[track][i].input.status = 0;
        opstate.current_transpose[track] = 0;
        opstate.current_tonality[track] = SCALE_CHROMATIC;
        opstate.current_root[track] = SONG_ROOT_OFF;
        opstate.damper[track] = 0;
    }
}

void outproc_stop_all_notes(int track) {
    int i;
    struct midi_msg off;
    if(track < 0 || track >= SEQ_NUM_TRACKS) return;
    for(int p = 0; p < MIDI_PORT_NUM_TRACK_OUTPUTS; p++) {
        for(int c = 0; c < 16; c++) {
            if(opstate.glide_cleanup_owner[p][c] != track) continue;
            if(p == MIDI_PORT_CV_OUT) {
                off.port = p; off.len = 0; off.status = 0x60 | c;
                off.data0 = 0; off.data1 = 64;
            } else midi_utils_enc_control_change(&off, p, c, 65, 0);
            // If congested, keep cleanup ownership so a later stop/attack
            // can still turn glide off. No retry loop in the realtime path.
            if(midi_stream_send_msg(&off) != 0) continue;
            opstate.glide_owner[p][c] = -1;
            opstate.glide_used[p][c] = 0;
            opstate.glide_cleanup_owner[p][c] = -1;
        }
    }
    if(opstate.damper[track]) {
        midi_utils_enc_control_change(&off, 0, 0, MIDI_CONTROLLER_DAMPER, 0);
        outproc_deliver_msg(0, track, &off, OUTPROC_DELIVER_BOTH, OUTPROC_OUTPUT_RAW);
        opstate.damper[track] = 0;
    }
    for(i = 0; i < OUTPROC_MAX_NOTES; i ++) {
        if(opstate.notes[track][i].input.status) {
            off = opstate.notes[track][i].output;
            if(off.status) {
                midi_utils_note_on_to_off(&off);
                midi_stream_send_msg(&off);
            }
            opstate.notes[track][i].input.status = 0;
        }
    }
}

// Root/scale/scene harmony changes apply to new attacks. Existing voices
// retain their sounding pitch until release, including pedal-held voices.
// MIDI re-triggering would introduce an unrequested attack (not a retune).
void outproc_pitch_changed(int scene, int track) {
    int transpose, tonality, root;
    if(song_scene_copy_in_progress()) return;
    if(scene != seq_engine_get_current_scene() || track < 0 || track >= SEQ_NUM_TRACKS) return;
    transpose = song_get_transpose(scene, track);
    tonality = song_get_track_type(track) == SONG_TRACK_TYPE_DRUM
        ? song_get_tonality(scene, track) : seq_engine_get_effective_tonality(scene, track);
    root = seq_engine_get_effective_root(scene);
    // DRUM pitches select instruments: stored transpose/scale stay in the
    // song (restored on VOICE) but never move the output.
    if(song_get_track_type(track) == SONG_TRACK_TYPE_DRUM) {
        transpose = 0;
        tonality = SCALE_CHROMATIC;
    }
    // Legacy transpose editing revoices held notes, as before, but uses
    // saved output destinations/pitches rather than recomputing note-offs.
    if(root < 0 && opstate.current_root[track] < 0 &&
            tonality == opstate.current_tonality[track] &&
            transpose != opstate.current_transpose[track]) {
        int i, pitch;
        struct midi_msg msg;
        for(i = 0; i < OUTPROC_MAX_NOTES; i ++) {
            struct outproc_note *note = &opstate.notes[track][i];
            if(!note->input.status || !note->output.status) continue;
            msg = note->output;
            midi_utils_note_on_to_off(&msg);
            midi_stream_send_msg(&msg);
            pitch = scale_quantize(note->input.data0, tonality) + transpose;
            if(!seq_utils_check_note_range(pitch)) {
                note->input.status = 0;
                continue;
            }
            note->output.data0 = pitch;
            midi_stream_send_msg(&note->output);
        }
    }
    // Otherwise do not touch sounding voices or pedal state. Their stored
    // outputs remain authoritative for note-offs and poly pressure.
    opstate.current_transpose[track] = transpose;
    opstate.current_tonality[track] = tonality;
    opstate.current_root[track] = root;
}
void outproc_transpose_changed(int scene, int track) {
    outproc_pitch_changed(scene, track);
}
void outproc_tonality_changed(int scene, int track) {
    outproc_pitch_changed(scene, track);
}
void outproc_root_changed(void) {
    int track;
    for(track = 0; track < SEQ_NUM_TRACKS; track ++)
        outproc_pitch_changed(seq_engine_get_current_scene(), track);
}

static int outproc_find_note(int track, int destination, struct midi_msg *msg) {
    int i, found = -1;
    struct midi_msg identity = *msg;
    identity.status = MIDI_NOTE_OFF | (msg->status & 0x0f);
    for(i = 0; i < OUTPROC_MAX_NOTES; i ++)
        if(opstate.notes[track][i].input.status &&
                opstate.notes[track][i].destination == destination &&
                midi_utils_compare_note_msg(&opstate.notes[track][i].input, &identity) &&
                (found < 0 || (int32_t)(opstate.notes[track][i].order -
                 opstate.notes[track][found].order) < 0)) found = i;
    return found;
}
static int outproc_enqueue(int track, int destination, struct midi_msg *input, struct midi_msg *output) {
    int i;
    for(i = 0; i < OUTPROC_MAX_NOTES; i ++) {
        if(!opstate.notes[track][i].input.status) {
            opstate.notes[track][i].input = *input;
            opstate.notes[track][i].output = *output;
            opstate.notes[track][i].destination = destination;
            opstate.notes[track][i].order = opstate.next_order++;
            return i;
        }
    }
    return -1;
}

/* Ownership persists until track output cleanup. A lower numbered requester
 * can take over; higher numbered glide requests cannot change its settings.
 * Notes themselves are not suppressed by glide arbitration. */
static void outproc_prepare_glide(int track, int port, int channel, struct midi_msg *attack) {
    struct midi_msg control;
    int duration = opstate.attack_duration;
    int owner = opstate.glide_owner[port][channel];
    if(duration && (owner < 0 || track < owner))
        opstate.glide_owner[port][channel] = owner = track;
    if(owner >= 0 && owner != track) return;
    if(!duration && !opstate.glide_used[port][channel]) return;
    opstate.glide_used[port][channel] = 1;
    opstate.glide_cleanup_owner[port][channel] = track;
    if(port == MIDI_PORT_CV_OUT) {
        attack->len = GLIDE_CV_FLAG | opstate.attack_curve;
        attack->data0 |= duration << 7;
    } else {
        if(duration) {
            /* Receiver-specific CC5 time: monotonic choice mapping, not ms. */
            midi_utils_enc_control_change(&control, port, channel, 5,
                (duration * 127 + 4) / 8);
            midi_stream_send_msg(&control);
        }
        midi_utils_enc_control_change(&control, port, channel, 65, duration ? 127 : 0);
        midi_stream_send_msg(&control);
    }
}

void outproc_deliver_glide(int scene, int track, struct midi_msg *msg, int duration, int curve) {
    opstate.attack_duration = duration >= 0 && duration <= GLIDE_MAX ? duration : 0;
    opstate.attack_curve = curve >= 0 && curve <= 127 ? curve : GLIDE_LINEAR;
    outproc_deliver_msg(scene, track, msg, OUTPROC_DELIVER_BOTH, OUTPROC_OUTPUT_PROCESSED);
    opstate.attack_duration = 0;
    opstate.attack_curve = GLIDE_LINEAR;
}

// deliver a track message to assigned outputs
void outproc_deliver_msg(int scene, int track, struct midi_msg *msg, 
        int deliver, int process) {
    struct midi_msg send_msg;
    struct midi_msg input;
    int out, port, channel, temp, slot;

    if(track < 0 || track >= SEQ_NUM_TRACKS) return;
    if((msg->status & 0xf0) == MIDI_NOTE_ON && !msg->data1) {
        input = *msg;
        midi_utils_rewrite_note_off(&input);
        outproc_deliver_msg(scene, track, &input, deliver, process);
        return;
    }
    // generate message for each output port for this track
    for(out = 0; out < SEQ_NUM_TRACK_OUTPUTS; out ++) {
        // skip ports that we don't want
        if(deliver == OUTPROC_DELIVER_A && out == 1) {
            continue;
        }
        else if(deliver == OUTPROC_DELIVER_B && out == 0) {
            continue;
        }
    
        // Release/pressure follows original identity and saved destination,
        // even if routing or harmony changed since the attack.
        if(process == OUTPROC_OUTPUT_PROCESSED &&
                ((msg->status & 0xf0) == MIDI_NOTE_OFF ||
                 (msg->status & 0xf0) == MIDI_POLY_KEY_PRESSURE)) {
            slot = outproc_find_note(track, out, msg);
            if(slot < 0) continue;
            send_msg = opstate.notes[track][slot].output;
            if((msg->status & 0xf0) == MIDI_NOTE_OFF) {
                opstate.notes[track][slot].input.status = 0;
                if(!send_msg.status) continue;
                midi_utils_note_on_to_off(&send_msg);
            }
            else {
                if(!send_msg.status) continue;
                send_msg.status = MIDI_POLY_KEY_PRESSURE | (send_msg.status & 0x0f);
            }
            send_msg.data1 = msg->data1;
            midi_stream_send_msg(&send_msg);
            continue;
        }
        // get port mapping
        port = song_get_midi_port_map(track, out);
        if(port == SONG_PORT_DISABLE) {
            continue;  // port not assigned
        }
        // get channel mapping; reject malformed routes before array access.
        channel = song_get_midi_channel_map(track, out);
        if(port < 0 || port >= MIDI_PORT_NUM_TRACK_OUTPUTS || channel < 0 || channel > 15) continue;
        
        // handle event types    
        switch(msg->status & 0xf0) {
            case MIDI_NOTE_OFF:
                midi_utils_enc_note_off(&send_msg, port, channel, msg->data0, msg->data1);
                // Processed releases are handled before routing lookup above.
                midi_stream_send_msg(&send_msg);
                break;
            case MIDI_NOTE_ON:
                // Reserve enough room before emitting CC5/CC65/Note On.
                // Output producers run serially in the RT task; consumers
                // can only increase room. Never emit a partial glide attack.
                temp = 1;
                if(port != MIDI_PORT_CV_OUT &&
                        (opstate.attack_duration || opstate.glide_used[port][channel]) &&
                        (opstate.glide_owner[port][channel] < 0 ||
                         opstate.glide_owner[port][channel] == track ||
                         (opstate.attack_duration && track < opstate.glide_owner[port][channel])))
                    temp += opstate.attack_duration ? 2 : 1;
                if(midi_stream_free_slots(port) < temp) {
                    // Retain silent FIFO identity for the eventual release.
                    if(process == OUTPROC_OUTPUT_PROCESSED) {
                        send_msg = *msg; send_msg.status = 0;
                        outproc_enqueue(track, out, msg, &send_msg);
                    }
                    continue;
                }
                midi_utils_enc_note_on(&send_msg, port, channel, msg->data0, msg->data1);
                // do note processing / queue
                if(process == OUTPROC_OUTPUT_PROCESSED) {
                    input = *msg;
                    temp = outproc_preview_pitch(track, send_msg.data0);
                    // note became invalid
                    if(!seq_utils_check_note_range(temp)) {
                        // Keep a silent identity so its release cannot consume a
                        // later repeated attack of the same input note.
                        send_msg.status = 0;
                        outproc_enqueue(track, out, &input, &send_msg);
                        continue;
                    }
                    send_msg.data0 = temp;
                    slot = outproc_enqueue(track, out, &input, &send_msg);
                    if(slot < 0) continue;
                }
                if(!seq_utils_check_note_range(send_msg.data0)) continue;
                outproc_prepare_glide(track, port, channel, &send_msg);
                midi_stream_send_msg(&send_msg);
                break;
            case MIDI_POLY_KEY_PRESSURE:
                midi_utils_enc_key_pressure(&send_msg, port, channel, msg->data0, msg->data1);
                midi_stream_send_msg(&send_msg);                
                break;
            case MIDI_CONTROL_CHANGE:
                if(msg->data0 == MIDI_CONTROLLER_DAMPER) opstate.damper[track] = msg->data1 >= 64;
                midi_utils_enc_control_change(&send_msg, port, channel, msg->data0, msg->data1);
                midi_stream_send_msg(&send_msg);
                break;
            case MIDI_PROGRAM_CHANGE:
                midi_utils_enc_program_change(&send_msg, port, channel, msg->data0);
                midi_stream_send_msg(&send_msg);
                break;
            case MIDI_CHANNEL_PRESSURE:
                midi_utils_enc_channel_pressure(&send_msg, port, channel, msg->data0);
                midi_stream_send_msg(&send_msg);
                break;
            case MIDI_PITCH_BEND:
                midi_utils_enc_pitch_bend(&send_msg, port, channel, (msg->data0 | 
                    (msg->data1 << 7)) - 8192);
                midi_stream_send_msg(&send_msg);
                break;
            default:
                break;
        }
    }
}
