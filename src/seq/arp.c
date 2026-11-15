/*
 * CARBON Arpeggiator
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
#include "arp.h"
#include "arp_progs.h"
#include "seq_engine.h"
#include "song.h"
#include "scale.h"
#include "../config.h"
#include "../midi/midi_protocol.h"
#include "../midi/midi_utils.h"
#include "../util/log.h"
#include "../util/seq_utils.h"
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

// access to programs
extern struct arp_prog aprog[SEQ_NUM_TRACKS];

// settings
#define ARP_MAX_HELD_NOTES 8
#define ARP_MAX_PLAYING_NOTES 8
#define ARP_NOTE_SLOT_FREE (-32768)

// arp data
struct arp_state {
    int seq_enable;  // 0 = disable, 1 = enable - affects clock syncing
    int arp_enable;  // 0 = disable, 1 = enable
    int type;  // arp type
    int gate_time;  // arp gate time
    int step_size;  // arp step size in ticks
    int chord_mode, chord_fresh;
    int16_t held_notes[ARP_MAX_HELD_NOTES];  // held notes
    int8_t held_order[ARP_MAX_HELD_NOTES];  // held note order
    int8_t held_velo;  // held note velocity
    uint8_t held_note_count;  // number of held notes
    int16_t snapshot_notes[ARP_MAX_HELD_NOTES];  // snapshot of held notes
    int8_t snapshot_order[ARP_MAX_HELD_NOTES];  // snapshot of held note order
    int16_t playing_notes[ARP_MAX_PLAYING_NOTES];  // playing note
    uint8_t playing_note_count;  // number of playing notes
    int play_note_timeout;  // tick timeout for playing note
    // arp execution state
    int seq_clock_count;  // divider counter for sequencer running
    int freerun_clock_count;  // divider counter for freerunning
    int pc;  // current program counter location
    int x;  // the register used for temp storage during program run
    int regs[ARP_PROG_NUM_REGS];  // temp registers for programs to use
    int note_offset;  // the offset for the notes generated
};
struct arp_state astate[SEQ_NUM_TRACKS];

// local functions
void arp_reset_program(int track);
void arp_execute_step(int track);
int arp_find_lowest_note(int track);
int arp_find_highest_note(int track);
int arp_find_lower_note(int track, int note);
int arp_find_higher_note(int track, int note);
int arp_find_oldest_note(int track);
int arp_find_newest_note(int track);
int arp_find_older_note(int track, int note);
int arp_find_newer_note(int track, int note);
int arp_find_random_note(int track, int note);
void arp_start_note(int track, int note);
void arp_stop_all_notes(int track);
void arp_timeout_notes(int track);
int arp_find_label(int track, int label);
void arp_take_snapshot(int track);

// Synchronous, bounded collection uses the production arp interpreter.
static int chord_collect_track = -1, chord_count;
static int chord_notes[4];
static void arp_collect_chord_note(int note) {
    for(int i=0;i<chord_count;i++) if(chord_notes[i]==note) return;
    if(chord_count<4) chord_notes[chord_count++]=note;
}
// Range is encoded in groups of four except the repeat family.
static int arp_chord_range(int type) {
    if(type < ARP_TYPE_REPEAT1_0) return type % 4 + 1;
    if(type >= ARP_TYPE_UP_LOW1) return (type-ARP_TYPE_UP_LOW1) % 4 + 1;
    return 1; // Repeat ratios are rhythms, not octave ranges.
}
static void arp_single_note_chord(int track, int note) {
    int scene=seq_engine_get_current_scene();
    int root=seq_engine_get_effective_root(scene);
    int scale=seq_engine_get_effective_tonality(scene,track);
    int transpose=root>=0 ? seq_engine_get_internal_cc(track,17,song_get_transpose(scene,track)) : 0;
    // Build in the same scale domain as output, then remove the transpose
    // already applied here. Output applies its normal transformation once.
    int shifted=note+transpose;
    int pc=((shifted-(root>=0?root:0))%12+12)%12;
    int anchor=shifted-pc+scale_quantize(pc,scale);
    int third=4, fifth=7;
    if(scale!=SCALE_CHROMATIC) {
        int degree=0;third=fifth=12;
        for(int offset=1;offset<12;offset++) {
            int p=((anchor+offset-(root>=0?root:0))%12+12)%12;
            if(scale_quantize(p,scale)==p) {
                degree++;
                if(degree==2) third=offset;
                if(degree==4) fifth=offset;
            }
        }
    }
    int type=astate[track].type, range=arp_chord_range(type);
    int down=(type>=ARP_TYPE_DOWN1 && type<=ARP_TYPE_DOWN4) || type>=ARP_TYPE_DOWN_HIGH1;
    int offsets[4]={0,third,fifth,12};
    if(range==2) {offsets[1]=fifth;offsets[2]=12;offsets[3]=12+fifth;}
    if(range==3) {offsets[1]=fifth;offsets[2]=12;offsets[3]=24;}
    if(range==4) {offsets[1]=12;offsets[2]=24;offsets[3]=36;}
    if(down) {
        if(range==1) {offsets[1]=fifth-12;offsets[2]=third-12;offsets[3]=-12;}
        if(range==2) {offsets[1]=fifth-12;offsets[2]=-12;offsets[3]=fifth-24;}
        if(range==3) {offsets[1]=fifth-12;offsets[2]=-12;offsets[3]=-24;}
        if(range==4) {offsets[1]=-12;offsets[2]=-24;offsets[3]=-36;}
    }
    for(int i=0;i<4;i++) arp_collect_chord_note(anchor-transpose+offsets[i]);
}
int arp_chord_enabled(int track) {
    return track>=0 && track<SEQ_NUM_TRACKS && astate[track].arp_enable && astate[track].chord_mode;
}
void arp_play_chord(int track, const int *notes, int count, int velocity, int gate_ticks) {
    if(!arp_chord_enabled(track) || count<=0 || count>SEQ_TRACK_POLY) return;
    arp_stop_all_notes(track);
    struct arp_state saved = astate[track];
    arp_clear_input(track);
    chord_count=0;
    int single=1;
    for(int i=0;i<count;i++) {
        if(notes[i]!=notes[0]) single=0;
        astate[track].held_notes[i]=notes[i];
        astate[track].held_order[i]=i+1;
    }
    astate[track].held_note_count=count;
    arp_reset_program(track);
    chord_collect_track=track;
    // At most 16 ordinary arp advances, each bounded by ARP_MAX_LOOP_COUNT.
    // This is a musical sample, not an unbounded attempt to finish a cycle.
    if(single && song_get_track_type(track)!=SONG_TRACK_TYPE_DRUM)
        arp_single_note_chord(track,notes[0]);
    else for(int i=0;i<16 && chord_count<4;i++) arp_execute_step(track);
    chord_collect_track=-1;
    astate[track]=saved;
    astate[track].held_velo=velocity;
    for(int i=0;i<chord_count;i++) arp_start_note(track,chord_notes[i]);
    astate[track].play_note_timeout=gate_ticks<1?1:gate_ticks;
    astate[track].chord_fresh=1;
}

// init the arp
void arp_init(void) {
    int track, i;
    arp_progs_init();

    // clear arp for all tracks
    for(track = 0; track < SEQ_NUM_TRACKS; track ++) {
        // reset playing notes
        for(i = 0; i < ARP_MAX_PLAYING_NOTES; i ++) {
            astate[track].playing_notes[i] = ARP_NOTE_SLOT_FREE;
        }
        astate[track].playing_note_count = 0;
        astate[track].play_note_timeout = 0;
        astate[track].gate_time = 1;
        astate[track].chord_mode = astate[track].chord_fresh = 0;

        // reset stuff
        arp_set_seq_enable(0);
        arp_set_arp_enable(track, 0);  // off - causes stuff to be reset
        arp_set_type(track, ARP_TYPE_UP1);
        arp_set_speed(track, SEQ_UTILS_STEP_16TH);
        arp_set_gate_time(track,
            seq_utils_step_len_to_ticks(SEQ_UTILS_STEP_32ND));
    }
}

// run the arp - called on each clock tick
void arp_run(int tick_count) {
    int track;

    // process arp for each track
    for(track = 0; track < SEQ_NUM_TRACKS; track ++) {
        // sequencer position was reset
        if(tick_count == 0) {
            astate[track].seq_clock_count = 0;
        }

        // Sequenced Chord attacks are batched after the step's eligible slots.
        // Never free-run the arp program in this mode (including live input).
        if(arp_chord_enabled(track)) {
            if(astate[track].chord_fresh) astate[track].chord_fresh=0;
            else arp_timeout_notes(track);
            continue;
        }
        // run when arp is enabled
        if(astate[track].arp_enable) {
            // run the step
            if((astate[track].seq_enable && astate[track].seq_clock_count == 0) ||
                    (!astate[track].seq_enable && astate[track].freerun_clock_count == 0)) {
                // no notes are held down
                if(astate[track].held_note_count == 0) {
                    if(astate[track].playing_note_count > 0) {
                        arp_stop_all_notes(track);
                    }
                    arp_reset_program(track);
                }
                // run the step
                else {
                    arp_execute_step(track);
                }
            }
            // timeout notes
            else {
                arp_timeout_notes(track);
            }
        }

        // sequencer is running
        if(astate[track].seq_enable) {
            astate[track].seq_clock_count ++;
            if(astate[track].seq_clock_count >= astate[track].step_size) {
                astate[track].seq_clock_count = 0;
            }
        }
        // arp is freerunning
        else {
            astate[track].freerun_clock_count ++;
            if(astate[track].freerun_clock_count >= astate[track].step_size) {
                astate[track].freerun_clock_count = 0;
            }
        }
    }
}

// set whether the external sequencer is playing or not
// this affects whether we resync the arp start to our own clock
void arp_set_seq_enable(int enable) {
    int track;
    // enable and disable seq on all tracks
    for(track = 0; track < SEQ_NUM_TRACKS; track ++) {
        if(enable) {
            astate[track].seq_enable = 1;
        }
        else {
            astate[track].seq_enable = 0;
        }
    }
}

// set the arp enable - 0 = off, 1 = on
void arp_set_arp_enable(int track, int enable) {
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("asae - track invalid: %d", track);
        return;
    }

    // start
    if(enable) {
        arp_clear_input(track);
        arp_reset_program(track);
        astate[track].arp_enable = 1;
    }
    // stop
    else {
        arp_clear_input(track);
        arp_stop_all_notes(track);
        astate[track].arp_enable = 0;
    }
}

// handle a input from the keyboard - we only get notes if we're enabled
void arp_handle_input(int track, struct midi_msg *msg) {
    int i, max_order;
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("ahi - track invalid: %d", track);
        return;
    }
    // handle each kind of message we care about
    switch(msg->status & 0xf0) {
        case MIDI_NOTE_OFF:
            // remove the note from the list
            for(i = 0; i < ARP_MAX_HELD_NOTES; i ++) {
                if(astate[track].held_notes[i] == msg->data0) {
                    astate[track].held_notes[i] = ARP_NOTE_SLOT_FREE;
                    astate[track].held_note_count --;
                    break;
                }
            }
            break;
        case MIDI_NOTE_ON:
            max_order = 0;
            // search for max order
            for(i = 0; i < ARP_MAX_HELD_NOTES; i ++) {
                if(astate[track].held_notes[i] != ARP_NOTE_SLOT_FREE) {
                    if(astate[track].held_order[i] > max_order) {
                        max_order = astate[track].held_order[i];
                    }
                }
            }
            // add the note to the list
            for(i = 0; i < ARP_MAX_HELD_NOTES; i ++) {
                if(astate[track].held_notes[i] == ARP_NOTE_SLOT_FREE) {
                    astate[track].held_notes[i] = msg->data0;
                    // we use the first note pressed for the velocity
                    if(astate[track].held_note_count == 0) {
                        astate[track].held_velo = msg->data1;
                    }
                    astate[track].held_order[i] = max_order + 1;
                    astate[track].held_note_count ++;
                    break;
                }
            }
            // if this is our first note then we need to reset the
            // freerunning clock counter so we start playing right away
            if(astate[track].held_note_count == 1) {
                astate[track].freerun_clock_count = 0;
            }
            break;
        default:
            break;
    }
}

// clear all input notes and stop notes - live input went away
void arp_clear_input(int track) {
    int i;
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("aci - track invalid: %d", track);
        return;
    }

    // clear the held note list
    for(i = 0; i < ARP_MAX_HELD_NOTES; i ++) {
        astate[track].held_notes[i] = ARP_NOTE_SLOT_FREE;
        astate[track].held_order[i] = 0;
    }
    astate[track].held_note_count = 0;
    // turn off all playing notes
    arp_stop_all_notes(track);
}

// set the arp type
void arp_set_type(int track, int type) {
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("ast - track invalid: %d", track);
        return;
    }
    if(type < 0 || type >= ARP_NUM_TYPES) {
        log_error("ast - type invalid: %d", type);
        return;
    }
    if(astate[track].chord_mode) arp_stop_all_notes(track);
    astate[track].type = type;
    arp_reset_program(track);
    arp_progs_load(track, astate[track].type);  // load program
}

// set the arp speed
void arp_align_rate(int track) {
    if(track < 0 || track >= SEQ_NUM_TRACKS) return;
    arp_stop_all_notes(track);
    astate[track].seq_clock_count = 0;
    astate[track].freerun_clock_count = 0;
}

void arp_set_speed(int track, int speed) {
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("ass - track invalid: %d", track);
        return;
    }
    if(speed < 0 || speed >= SEQ_UTILS_ARP_SPEEDS) {
        log_error("ass - speed invalid: %d", speed);
        return;
    }
    int chord = speed == SEQ_UTILS_ARP_CHORD;
    if(chord != astate[track].chord_mode) {
        arp_clear_input(track);
        arp_reset_program(track);
        astate[track].chord_fresh=0;
    }
    astate[track].chord_mode=chord;
    astate[track].step_size = chord ? 1 : seq_utils_step_len_to_ticks(speed);
}

// set the gate time - ticks
void arp_set_gate_time(int track, int time) {
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("asagt - track invalid: %d", track);
        return;
    }
    if(time < ARP_GATE_TIME_MIN || time > ARP_GATE_TIME_MAX) {
        log_error("asagt - time invalid: %d", time);
        return;
    }
    astate[track].gate_time = time;
}

//
// local functions
//
// reset the program
void arp_reset_program(int track) {
    int i;
    astate[track].pc = 0;
    astate[track].x = 0;
    astate[track].note_offset = 0;
    // clear all registers
    for(i = 0; i < ARP_PROG_NUM_REGS; i ++) {
        astate[track].regs[i] = 0;
    }
    // clear the note snapshot
    for(i = 0; i < ARP_MAX_HELD_NOTES; i ++) {
        astate[track].snapshot_notes[i] = ARP_NOTE_SLOT_FREE;
        astate[track].snapshot_order[i] = 0;
    }
}

// execute the step
void arp_execute_step(int track) {
    int loop_count = 0;
    int arg, temp, note;

    while(loop_count < ARP_MAX_LOOP_COUNT) {
        arg = aprog[track].prog[astate[track].pc][ARP_PROG_ARG];

        // process the instruction
        switch(aprog[track].prog[astate[track].pc][ARP_PROG_INST]) {
            case AP_NOP:
                // do nothing
                break;
            case AP_SNAPSHOT:
                arp_take_snapshot(track);
                break;
            case AP_FIND_LOWEST_NOTE:
                note = arp_find_lowest_note(track);
                // no note found - jump to label from argument
                if(note == ARP_NOTE_SLOT_FREE) {
                    temp = arp_find_label(track, arg);
                    // label not found - reset program
                    if(temp == -1) {
                        arp_reset_program(track);
                        return;
                    }
                    astate[track].pc = temp;
                }
                // note was found
                else {
                    astate[track].x = note;
                }
                break;
            case AP_FIND_HIGHEST_NOTE:
                note = arp_find_highest_note(track);
                // no note found - jump to label from argument
                if(note == ARP_NOTE_SLOT_FREE) {
                    temp = arp_find_label(track, arg);
                    // label not found - reset program
                    if(temp == -1) {
                        arp_reset_program(track);
                        return;
                    }
                    astate[track].pc = temp;
                    break;
                }
                // note was found
                else {
                    astate[track].x = note;
                }
                break;
            case AP_FIND_LOWER_NOTE:
                note = arp_find_lower_note(track, astate[track].x);
                // no note found - jump to label from argument
                if(note == ARP_NOTE_SLOT_FREE) {
                    temp = arp_find_label(track, arg);
                    // label not found - reset program
                    if(temp == -1) {
                        arp_reset_program(track);
                        return;
                    }
                    astate[track].pc = temp;
                    break;
                }
                // note was found
                else {
                    astate[track].x = note;
                }
                break;
            case AP_FIND_HIGHER_NOTE:
                note = arp_find_higher_note(track, astate[track].x);
                // no note found - jump to label from argument
                if(note == ARP_NOTE_SLOT_FREE) {
                    temp = arp_find_label(track, arg);
                    // label not found - reset program
                    if(temp == -1) {
                        arp_reset_program(track);
                        return;
                    }
                    astate[track].pc = temp;
                    break;
                }
                // note was found
                else {
                    astate[track].x = note;
                }
                break;
            case AP_FIND_OLDEST_NOTE:
                note = arp_find_oldest_note(track);
                // no note found - jump to label from argument
                if(note == ARP_NOTE_SLOT_FREE) {
                    temp = arp_find_label(track, arg);
                    // label not found - reset program
                    if(temp == -1) {
                        arp_reset_program(track);
                        return;
                    }
                    astate[track].pc = temp;
                }
                // note was found
                else {
                    astate[track].x = note;
                }
                break;
            case AP_FIND_NEWEST_NOTE:
                note = arp_find_newest_note(track);
                // no note found - jump to label from argument
                if(note == ARP_NOTE_SLOT_FREE) {
                    temp = arp_find_label(track, arg);
                    // label not found - reset program
                    if(temp == -1) {
                        arp_reset_program(track);
                        return;
                    }
                    astate[track].pc = temp;
                    break;
                }
                // note was found
                else {
                    astate[track].x = note;
                }
                break;
            case AP_FIND_OLDER_NOTE:
                note = arp_find_older_note(track, astate[track].x);
                // no note found - jump to label from argument
                if(note == ARP_NOTE_SLOT_FREE) {
                    temp = arp_find_label(track, arg);
                    // label not found - reset program
                    if(temp == -1) {
                        arp_reset_program(track);
                        return;
                    }
                    astate[track].pc = temp;
                    break;
                }
                // note was found
                else {
                    astate[track].x = note;
                }
                break;
            case AP_FIND_NEWER_NOTE:
                note = arp_find_newer_note(track, astate[track].x);
                // no note found - jump to label from argument
                if(note == ARP_NOTE_SLOT_FREE) {
                    temp = arp_find_label(track, arg);
                    // label not found - reset program
                    if(temp == -1) {
                        arp_reset_program(track);
                        return;
                    }
                    astate[track].pc = temp;
                    break;
                }
                // note was found
                else {
                    astate[track].x = note;
                }
                break;
            case AP_FIND_RANDOM_NOTE:
                note = arp_find_random_note(track, astate[track].x);
                // no note found - jump to label from argument
                if(note == ARP_NOTE_SLOT_FREE) {
                    temp = arp_find_label(track, arg);
                    // label not found - reset program
                    if(temp == -1) {
                        arp_reset_program(track);
                        return;
                    }
                    astate[track].pc = temp;
                    break;
                }
                // note was found
                else {
                    astate[track].x = note;
                }
                break;
            case AP_PLAY_NOTE:
                arp_start_note(track, astate[track].x + astate[track].note_offset);
                break;
            case AP_WAIT:
                astate[track].pc ++;
                return;  // we need to return here to pause execution
            case AP_PLAY_NOTE_AND_WAIT:
                if(chord_collect_track != track) arp_stop_all_notes(track);
                arp_start_note(track, astate[track].x + astate[track].note_offset);
                astate[track].pc ++;
                return;  // we need to return here to pause execution
            case AP_LABEL:
                // no action - these instructions are scanned by the jump routine
                break;
            case AP_JUMP:
                temp = arp_find_label(track, arg);
                // label not found - reset program
                if(temp == -1) {
                    arp_reset_program(track);
                    return;
                }
                astate[track].pc = temp;
                break;
            case AP_LOADL:
                astate[track].x = arg;
                break;
            case AP_LOADF:
                switch(arg) {
                    case ARP_REG_NOTE_OFFSET:
                        astate[track].x = astate[track].note_offset;
                        break;
                    default:  // load a temp register
                        if(arg >= 0 && arg < ARP_PROG_NUM_REGS) {
                            astate[track].x = astate[track].regs[arg];
                        }
                        break;
                }
                break;
            case AP_STOREF:
                switch(arg) {
                    case ARP_REG_NOTE_OFFSET:
                        astate[track].note_offset = astate[track].x;
                        break;
                    default:  // load a temp register
                        if(arg >= 0 && arg < ARP_PROG_NUM_REGS) {
                            astate[track].regs[arg] = astate[track].x;
                        }
                        break;
                }
                break;
            case AP_ADDL:
                astate[track].x += arg;
                break;
            case AP_SUBL:
                astate[track].x -= arg;
                break;
            case AP_MULL:
                astate[track].x *= arg;
                break;
            case AP_ADDF:
                if(arg >= 0 && arg < ARP_PROG_NUM_REGS) {
                    astate[track].x += astate[track].regs[arg];
                }
                break;
            case AP_SUBF:
                if(arg >= 0 && arg < ARP_PROG_NUM_REGS) {
                    astate[track].x -= astate[track].regs[arg];
                }
                break;
            case AP_MULF:
                if(arg >= 0 && arg < ARP_PROG_NUM_REGS) {
                    astate[track].x *= astate[track].regs[arg];
                }
                break;
            case AP_JZ:
                if(astate[track].x == 0) {
                    temp = arp_find_label(track, arg);
                    // label not found - reset program
                    if(temp == -1) {
                        arp_reset_program(track);
                        return;
                    }
                    astate[track].pc = temp;
                }
                break;
            case AP_RAND:
                astate[track].x = rand() % arg;
                break;
        }
        astate[track].pc ++;
        // see if we ran off the end of the program
        if(astate[track].pc == ARP_PROG_MAX_PROG_LEN) {
            arp_reset_program(track);
            return;
        }
        loop_count ++;
    }
}

// find the lowest snapshot note - returns note num or -1 if not found
int arp_find_lowest_note(int track) {
    int i, min = 32767;
    if(astate[track].held_note_count == 0) {
        return ARP_NOTE_SLOT_FREE;
    }
    for(i = 0; i < ARP_MAX_HELD_NOTES; i ++) {
        if(astate[track].snapshot_notes[i] != ARP_NOTE_SLOT_FREE &&
                astate[track].snapshot_notes[i] < min) {
            min = astate[track].snapshot_notes[i];
        }
    }
    if(min == 32767) {
        return ARP_NOTE_SLOT_FREE;
    }
    return min;
}

// find the highest snapshot note - returns note num or -1 if not found
int arp_find_highest_note(int track) {
    int i, max = ARP_NOTE_SLOT_FREE;
    if(astate[track].held_note_count == 0) {
        return ARP_NOTE_SLOT_FREE;
    }
    for(i = 0; i < ARP_MAX_HELD_NOTES; i ++) {
        if(astate[track].snapshot_notes[i] != ARP_NOTE_SLOT_FREE &&
                astate[track].snapshot_notes[i] > max) {
            max = astate[track].snapshot_notes[i];
        }
    }
    return max;
}

// find a snapshot note lower than note - returns note num or -1 if not found
int arp_find_lower_note(int track, int note) {
    int i, next = ARP_NOTE_SLOT_FREE;
    if(astate[track].held_note_count == 0) {
        return ARP_NOTE_SLOT_FREE;
    }
    for(i = 0; i < ARP_MAX_HELD_NOTES; i ++) {
        if(astate[track].snapshot_notes[i] != ARP_NOTE_SLOT_FREE &&
                astate[track].snapshot_notes[i] > next &&
                astate[track].snapshot_notes[i] < note) {
            next = astate[track].snapshot_notes[i];
        }
    }
    return next;
}

// find a snapshot note higher than note - returns note num or -1 if not found
int arp_find_higher_note(int track, int note) {
    int i, next = 32767;
    if(astate[track].held_note_count == 0) {
        return ARP_NOTE_SLOT_FREE;
    }
    for(i = 0; i < ARP_MAX_HELD_NOTES; i ++) {
        if(astate[track].snapshot_notes[i] != ARP_NOTE_SLOT_FREE &&
                astate[track].snapshot_notes[i] < next &&
                astate[track].snapshot_notes[i] > note) {
            next = astate[track].snapshot_notes[i];
        }
    }
    if(next == 32767) {
        return ARP_NOTE_SLOT_FREE;
    }
    return next;
}

// find the oldest snapshot note - returns note num or -1 if not found
int arp_find_oldest_note(int track) {
    int i, min = 255, note;
    if(astate[track].held_note_count == 0) {
        return ARP_NOTE_SLOT_FREE;
    }
    for(i = 0; i < ARP_MAX_HELD_NOTES; i ++) {
        if(astate[track].snapshot_notes[i] != ARP_NOTE_SLOT_FREE &&
                astate[track].snapshot_order[i] < min) {
            min = astate[track].snapshot_order[i];
            note = astate[track].snapshot_notes[i];
        }
    }
    if(min == 255) {
        return ARP_NOTE_SLOT_FREE;
    }
    return note;
}

// find the newest snapshot note - returns note num or -1 if not found
int arp_find_newest_note(int track) {
    int i, max = -1, note;
    if(astate[track].held_note_count == 0) {
        return ARP_NOTE_SLOT_FREE;
    }
    for(i = 0; i < ARP_MAX_HELD_NOTES; i ++) {
        if(astate[track].snapshot_notes[i] != ARP_NOTE_SLOT_FREE &&
                astate[track].snapshot_order[i] > max) {
            max = astate[track].snapshot_order[i];
            note = astate[track].snapshot_notes[i];
        }
    }
    if(max == -1) {
        return ARP_NOTE_SLOT_FREE;
    }
    return note;
}

// find a snapshot note older than note - returns note num or -1 if not found
int arp_find_older_note(int track, int note) {
    int i, next, order, old_note;
    if(astate[track].held_note_count == 0) {
        return ARP_NOTE_SLOT_FREE;
    }
    // find the order of the existing note
    order = 0;
    for(i = 0; i < ARP_MAX_HELD_NOTES; i ++) {
        if(astate[track].snapshot_notes[i] == note) {
            order = astate[track].snapshot_order[i];
        }
    }
    if(order == 0) {
        return ARP_NOTE_SLOT_FREE;
    }
    // find an older note
    next = 0;
    old_note = ARP_NOTE_SLOT_FREE;
    for(i = 0; i < ARP_MAX_HELD_NOTES; i ++) {
        if(astate[track].snapshot_notes[i] != ARP_NOTE_SLOT_FREE &&
                astate[track].snapshot_order[i] > next &&
                astate[track].snapshot_order[i] < order) {
            next = astate[track].snapshot_order[i];
            old_note = astate[track].snapshot_notes[i];
        }
    }
    if(next == -1) {
        return ARP_NOTE_SLOT_FREE;
    }
    if(old_note == ARP_NOTE_SLOT_FREE) {
        return ARP_NOTE_SLOT_FREE;
    }
    return old_note;
}

// find a snapshot note newer than note - returns note num or -1 if not found
int arp_find_newer_note(int track, int note) {
    int i, next, order, new_note;
    if(astate[track].held_note_count == 0) {
        return ARP_NOTE_SLOT_FREE;
    }
    // find the order of the existing note
    order = 0;
    for(i = 0; i < ARP_MAX_HELD_NOTES; i ++) {
        if(astate[track].snapshot_notes[i] == note) {
            order = astate[track].snapshot_order[i];
        }
    }
    if(order == 0) {
        return ARP_NOTE_SLOT_FREE;
    }
    // find a newer note
    next = 255;
    new_note = ARP_NOTE_SLOT_FREE;
    for(i = 0; i < ARP_MAX_HELD_NOTES; i ++) {
        if(astate[track].snapshot_notes[i] != ARP_NOTE_SLOT_FREE &&
                astate[track].snapshot_order[i] < next &&
                astate[track].snapshot_order[i] > order) {
            next = astate[track].snapshot_order[i];
            new_note = astate[track].snapshot_notes[i];
        }
    }
    if(next == 255) {
        return ARP_NOTE_SLOT_FREE;
    }
    if(new_note == ARP_NOTE_SLOT_FREE) {
        return ARP_NOTE_SLOT_FREE;
    }
    return new_note;
}

// find a random snapshot note - returns note num or -1 if not found
int arp_find_random_note(int track, int note) {
    int i, rand_num_notes;
    int16_t rand_notes[ARP_MAX_HELD_NOTES];
    if(astate[track].held_note_count == 0) {
        return ARP_NOTE_SLOT_FREE;
    }
    // make a list of held notes in order
    rand_num_notes = 0;
    for(i = 0; i < ARP_MAX_HELD_NOTES; i ++) {
        if(astate[track].snapshot_notes[i] != ARP_NOTE_SLOT_FREE) {
            rand_notes[rand_num_notes] = astate[track].snapshot_notes[i];
            rand_num_notes ++;
        }
    }
    if(!rand_num_notes) return ARP_NOTE_SLOT_FREE;
    return rand_notes[rand() % rand_num_notes];
}

// start a note playing
void arp_start_note(int track, int note) {
    if(chord_collect_track == track) {arp_collect_chord_note(note);return;}
    struct midi_msg msg;
    int i;
    // Preserve legacy arp octave wrapping; named roots retain signed pitch.
    if(astate[track].chord_mode) {
        // Chord octaves must not wrap from one end of MIDI range to the other.
        if(note<0 || note>127) return;
    } else if(seq_engine_get_effective_root(seq_engine_get_current_scene()) < 0 ||
            song_get_track_type(track) == SONG_TRACK_TYPE_DRUM) note &= 0x7f;
    for(i = 0; i < ARP_MAX_PLAYING_NOTES; i ++) {
        if(astate[track].playing_notes[i] == ARP_NOTE_SLOT_FREE) {
            midi_utils_enc_note_on(&msg, 0, 0, note, astate[track].held_velo);
            seq_engine_arp_start_note(track, &msg);
            astate[track].playing_notes[i] = note;
            astate[track].playing_note_count ++;
            astate[track].play_note_timeout = astate[track].gate_time;
            return;
        }
    }
}

// stop all notes that are playing
void arp_stop_all_notes(int track) {
    struct midi_msg msg;
    int i;
    for(i = 0; i < ARP_MAX_PLAYING_NOTES; i ++) {
        if(astate[track].playing_notes[i] != ARP_NOTE_SLOT_FREE) {
            midi_utils_enc_note_off(&msg, 0, 0, astate[track].playing_notes[i], 0x40);
            seq_engine_arp_stop_note(track, &msg);
            astate[track].playing_notes[i] = ARP_NOTE_SLOT_FREE;
            astate[track].playing_note_count --;
        }
    }
    astate[track].play_note_timeout = 0;
}

// timeout notes
void arp_timeout_notes(int track) {
    if(astate[track].play_note_timeout) {
        astate[track].play_note_timeout --;
        if(astate[track].play_note_timeout == 0) {
            arp_stop_all_notes(track);
        }
    }
}

// find a label - returns instruction number of -1 if not found
int arp_find_label(int track, int label) {
    int i;
    for(i = 0; i < ARP_PROG_MAX_PROG_LEN; i ++) {
        if(aprog[track].prog[i][ARP_PROG_INST] == AP_LABEL) {
            if(aprog[track].prog[i][ARP_PROG_ARG] == label) {
                return i;
            }
        }
    }
    return -1;
}

// take a snapshot of all held notes
void arp_take_snapshot(int track) {
    int i;
    for(i = 0; i < ARP_MAX_HELD_NOTES; i ++) {
        astate[track].snapshot_notes[i] = astate[track].held_notes[i];
        astate[track].snapshot_order[i] = astate[track].held_order[i];
    }
}
