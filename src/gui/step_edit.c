/*
 * CARBON Step Editor
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
#include "step_edit.h"
#include "scene_mask_ui.h"
#include "../seq/conditional.h"
#include "../seq/glide.h"
#include "gui.h"
#include "panel_menu.h"
#include "../config.h"
#include "../gfx.h"
#include "../seq/outproc.h"
#include "../seq/arp.h"
#include "../seq/scale.h"
#include "../seq/midi_ctrl.h"
#include "../seq/seq_ctrl.h"
#include "../seq/seq_engine.h"
#include "../seq/song.h"
#include "../util/log.h"
#include "../util/panel_utils.h"
#include "../util/seq_utils.h"
#include "../util/state_change.h"
#include "../util/state_change_events.h"
#include <stdio.h>
#include <string.h>

// step adjust modes
#define STEP_EDIT_ADJUST_NOTE 0
#define STEP_EDIT_ADJUST_VELO 1
#define STEP_EDIT_ADJUST_GATE_TIME 2
#define STEP_EDIT_ADJUST_START_DELAY 3

// constants
#define STEP_EDIT_EVENT_POS_ALL -1
#define STEP_EDIT_EVENT_POS_REGION -2  // all playing steps (motion start/length)
#define STEP_EDIT_NOTE_SLOT_FREE -1
#define STEP_EDIT_CCI_HINT_MS 2000

struct step_edit_state {
    int enable;  // 0 = disabled, 1 = enabled
    int edit_timeout;  // timeout counter for editing
    int scene;  // selected edit scene
    int track;  // selected edit track
    int step_pos;  // current step pos for editing
    int event_pos;  // the event edit pos or STEP_EDIT_EVENT_POS_ALL for global
    int undo_captured; // one checkpoint for the entire visit to this step
    int cci_hint_timeout;  // milliseconds remaining for title help
    int cci_hint_controller;
    int cci_hint_value;
    int cci_hint_show_value;
    int last_adjust_type;  // the last type of adjustment
    int region_filter;  // Region pitch filter: -1 = All, else MIDI pitch (not stored)
    // preview events
    int8_t playing_notes[SEQ_TRACK_POLY];  // playing notes
    uint16_t playing_note_timeouts[SEQ_TRACK_POLY];  // timeouts for playing notes
    // recording input events
    int8_t recording_notes[SEQ_TRACK_POLY];  // keep track of input notes
};
struct step_edit_state sedits;
static void step_edit_region_filter_step(int change);
static int step_details_preference;
void step_edit_update_display(void);

// Lane view (third Step Edit view, DRUM tracks): fixed rows while visiting.
#define STEP_EDIT_LANES 10
static struct {
    int on;                       // lane view active
    int lanes[STEP_EDIT_LANES];   // pitch per row, bottom first; -1 = empty row
    int cursor;                   // row index (0 = bottom)
    int moved;                    // last Transpose moved a lane (status note)
} slanes;
static int step_edit_lanes_active(void) {
    return slanes.on && sedits.enable && sedits.event_pos != STEP_EDIT_EVENT_POS_REGION &&
        song_get_track_type(sedits.track) == SONG_TRACK_TYPE_DRUM;
}
static void step_edit_lanes_build(void);
static void step_edit_lanes_publish(void);
static void step_edit_lanes_cursor_to_step(void);

// Long-press Edit: compact -> expanded -> lanes (DRUM only) -> compact.
void step_edit_toggle_details(void) {
    if(!sedits.enable) return;
    int drum = song_get_track_type(sedits.track) == SONG_TRACK_TYPE_DRUM &&
        sedits.event_pos != STEP_EDIT_EVENT_POS_REGION;
    if(slanes.on && !drum) slanes.on = 0;  // lanes kept from a DRUM track
    if(slanes.on) {slanes.on = 0; step_details_preference = 0;}
    else if(step_details_preference && drum) {
        slanes.on = 1; step_details_preference = 0;
        sedits.event_pos = STEP_EDIT_EVENT_POS_ALL;
        step_edit_lanes_build();
        step_edit_lanes_cursor_to_step();
    }
    else step_details_preference = !step_details_preference;
    sedits.edit_timeout=panel_menu_get_timeout();
    step_edit_update_display();
}

// local functions
void step_edit_adjust_step(int change, int mode, int single, int shift);
void step_edit_update_display(void);
static void step_edit_update_title(void);
static void step_edit_show_cci_hint(void);
static void step_edit_region_overlay(int on);
static int step_edit_region_adjust(int change, int mode);
static void step_edit_region_display(void);
static int step_edit_prepare_undo(void) {
    if(!song_snapshot_idle() || seq_ctrl_get_record_mode() != SEQ_CTRL_RECORD_IDLE) return -1;
    if(!sedits.undo_captured) {
        if(seq_engine_undo_capture() < 0) return -1;
        sedits.undo_captured = 1;
    }
    // Further edits supersede a queued restore, but retain the entry checkpoint.
    seq_engine_undo_cancel();
    return 0;
}
void step_edit_play_step(void);
void step_edit_stop_notes(void);
void step_edit_send_note_on(int note, int velocity);
void step_edit_send_note_off(int note);
void step_edit_send_cc(int controller, int value);
void step_edit_send_program_change(int program);
void step_edit_clear_recording_notes(void);
void step_edit_add_recording_note(int note);
void step_edit_remove_recording_note(int note);
int step_edit_get_num_recording_notes(void);
int step_edit_does_step_contain_event(int type, int data0);

// init the step edit mode
void step_edit_init(void) {
    step_details_preference=0;
    slanes.on=0;
    sedits.enable = 0;
    sedits.undo_captured = 0;
    sedits.edit_timeout = 0;
    sedits.cci_hint_timeout = 0;
    sedits.scene = 0;
    sedits.track = 0;
    sedits.step_pos = 0;
    sedits.event_pos = STEP_EDIT_EVENT_POS_ALL;
    sedits.region_filter = -1;
    sedits.last_adjust_type = STEP_EDIT_ADJUST_NOTE;
    // register events
    state_change_register(step_edit_handle_state_change, SCEC_CTRL);
    state_change_register(step_edit_handle_state_change, SCEC_ENG);
}

// run step edit timer task
void step_edit_timer_task(void) {
    if(sedits.enable && sedits.cci_hint_timeout > 0) {
        if(--sedits.cci_hint_timeout == 0 ||
                (sedits.cci_hint_show_value &&
                 (sedits.cci_hint_controller == MIDI_CTRL_CC_EUCLIDEAN_ROTATION ||
                  sedits.cci_hint_controller == MIDI_CTRL_CC_EUCLIDEAN_DENSITY) &&
                 sedits.cci_hint_timeout % 100 == 0))
            step_edit_update_title();
    }
    // time out step edit mode
    if(sedits.edit_timeout) {
        sedits.edit_timeout --;
        if(sedits.edit_timeout == 0) {
            step_edit_set_enable(0);
        }
    }
}

// the clock ticked - do note timeouts for preview
void step_edit_run(uint32_t tick_count) {
    int i;

    // time out note notes
    for(i = 0; i < SEQ_TRACK_POLY; i ++) {
        if(sedits.playing_note_timeouts[i]) {
            sedits.playing_note_timeouts[i] --;
            // time to turn off the note
            if(sedits.playing_note_timeouts[i] == 0 &&
                    sedits.playing_notes[i] != STEP_EDIT_NOTE_SLOT_FREE) {
                step_edit_send_note_off(sedits.playing_notes[i]);
                sedits.playing_notes[i] = STEP_EDIT_NOTE_SLOT_FREE;
            }
        }
    }
}

// handle state change
void step_edit_handle_state_change(int event_type, int *data, int data_len) {
    switch(event_type) {
        case SCE_ENG_CURRENT_SCENE:
        case SCE_CTRL_FIRST_TRACK:
            // if we're already enabled we should reset it
            if(sedits.enable) {
                step_edit_set_enable(0);
                step_edit_set_enable(1);
            }
            break;
    }
}

// handle a input from the keyboard
void step_edit_handle_input(struct midi_msg *msg) {
    int i;
    struct track_event event = {.delay = SEQ_START_DELAY_INHERIT};
    // we only care about MIDI if we're enabled (keyboard entry is per step)
    if(!sedits.enable || sedits.event_pos == STEP_EDIT_EVENT_POS_REGION) {
        return;
    }

    // handle incoming MIDI types
    switch(msg->status & 0xf0) {
        case MIDI_NOTE_ON:
            if(step_edit_prepare_undo() < 0) return;
            sedits.edit_timeout = panel_menu_get_timeout();
            // if this is the first note we need to clear other notes off the step
            if(step_edit_get_num_recording_notes() == 0) {
                for(i = 0; i < SEQ_TRACK_POLY; i ++) {
                    // no data on step
                    if(song_get_step_event(sedits.scene, sedits.track,
                            sedits.step_pos, i, &event) == -1) {
                        continue;
                    }
                    // for voice tracks we clear the whole step
                    if(event.type == SONG_EVENT_NOTE &&
                            song_get_track_type(sedits.track) == SONG_TRACK_TYPE_VOICE) {
                        song_clear_step_event(sedits.scene, sedits.track,
                            sedits.step_pos, i);
                    }
                }
            }
            // add / edit the new note
            event.delay = SEQ_START_DELAY_INHERIT;
            event.type = SONG_EVENT_NOTE;
            event.data0 = msg->data0;
            event.data1 = msg->data1;
            event.length = seq_utils_step_len_to_ticks(
                song_get_step_length(sedits.scene, sedits.track));
            song_add_step_event(sedits.scene, sedits.track,
                sedits.step_pos, &event);
            if(step_edit_lanes_active()) {
                int row, empty = -1, known = 0;
                for(row = 0; row < STEP_EDIT_LANES; row++) {
                    if(slanes.lanes[row] == msg->data0) known = 1;
                    if(slanes.lanes[row] < 0 && empty < 0) empty = row;
                }
                if(!known && empty >= 0) slanes.lanes[empty] = msg->data0;
            }
            step_edit_update_display();
            step_edit_add_recording_note(msg->data0);
            break;
        case MIDI_NOTE_OFF:
            sedits.edit_timeout = panel_menu_get_timeout();
            step_edit_remove_recording_note(msg->data0);
            break;
        case MIDI_CONTROL_CHANGE:
            if(step_edit_prepare_undo() < 0) return;
            sedits.edit_timeout = panel_menu_get_timeout();
            // add / edit the new CC
            event.type = SONG_EVENT_CC;
            event.data0 = msg->data0;
            event.data1 = msg->data1;
            song_add_step_event(sedits.scene, sedits.track,
                sedits.step_pos, &event);
            step_edit_update_display();
            break;
    }
}

// get whether step edit mode is enabled
int step_edit_get_enable(void) {
    return sedits.enable;
}

// start and stop step edit mode
void step_edit_set_enable(int enable) {
    sedits.cci_hint_timeout = 0;
    // start edit
    if(enable) {
        // we're already editing - just trigger the note
        if(sedits.enable) {
            step_edit_adjust_cursor(0, 0);  // causes the step to be played / displayed
            sedits.edit_timeout = panel_menu_get_timeout();
        }
        // start editing from scratch
        else {
            sedits.undo_captured = 0;
            if(step_edit_prepare_undo() < 0) return;
            sedits.enable = 1;
            sedits.edit_timeout = panel_menu_get_timeout();
            sedits.scene = seq_ctrl_get_scene();
            sedits.track = seq_ctrl_get_first_track();
            if(sedits.event_pos == STEP_EDIT_EVENT_POS_REGION)
                sedits.event_pos = STEP_EDIT_EVENT_POS_ALL;  // a new visit starts on a step
            sedits.region_filter = -1;
            if(slanes.on) {step_edit_lanes_build(); step_edit_lanes_cursor_to_step();}
            panel_menu_set_mode(PANEL_MENU_NONE);  // disable any menu mode
            gui_grid_clear_overlay();
            gui_grid_set_overlay_enable(1);
            gui_clear_status_text_all();
            gui_set_status_override(1);  // take over status display
            step_edit_adjust_cursor(0, 0);  // causes the step to be played / displayed
            step_edit_clear_recording_notes();
        }
    }
    // stop edit - only if we're actually enabled
    else if(sedits.enable) {
        sedits.enable = 0;
        gui_lane_edit_set(0, 0, 0, 0, 0);
        gui_step_details_set(0);
        sedits.undo_captured = 0;
        sedits.edit_timeout = 0;
        step_edit_stop_notes();
        gui_grid_set_overlay_enable(0);
        gui_clear_status_text_all();  // XXX get rid of this
        gui_set_status_override(0);  // give back status display
    }
}

int step_edit_suspend_for_undo(void) {
    int enabled = sedits.enable;
    if(enabled) step_edit_set_enable(0); // release preview notes through OLD routes
    return enabled;
}

void step_edit_resume_after_undo(void) {
    // Undo already restored the entry checkpoint. Do not capture it again or
    // call adjust_cursor/set_enable, which would audition while stopped.
    sedits.enable = 1;
    sedits.undo_captured = 1;
    sedits.scene = seq_ctrl_get_scene();
    sedits.edit_timeout = panel_menu_get_timeout();
    sedits.cci_hint_timeout = 0;
    step_edit_clear_recording_notes();
    gui_grid_clear_overlay();
    gui_grid_set_overlay_enable(1);
    // Region visits stay in Region: restore the blue region overlay.
    if(sedits.event_pos == STEP_EDIT_EVENT_POS_REGION) step_edit_region_overlay(1);
    else gui_grid_set_overlay_color(sedits.step_pos, GUI_OVERLAY_HIGH);
    gui_set_status_override(1);
    step_edit_update_display();
}

int step_edit_scene_mask(void) {
    struct track_event event;
    if(!sedits.enable || sedits.event_pos < 0 || sedits.event_pos >= SEQ_TRACK_POLY) return -1;
    if(song_get_step_event(sedits.scene, sedits.track, sedits.step_pos, sedits.event_pos, &event) < 0 ||
            event.type != SONG_EVENT_SCENE) return -1;
    return event.data0 & 63;
}
void step_edit_toggle_scene(int scene) {
    struct track_event event;
    if(scene < 0 || scene >= SEQ_NUM_SCENES || step_edit_scene_mask() < 0) return;
    if(song_get_step_event(sedits.scene, sedits.track, sedits.step_pos, sedits.event_pos, &event) < 0) return;
    if(step_edit_prepare_undo() < 0) return;
    event.data0 ^= 1u << scene;
    song_set_step_event(sedits.scene, sedits.track, sedits.step_pos, sedits.event_pos, &event);
    sedits.edit_timeout = panel_menu_get_timeout();
    step_edit_update_display(); // no preview/Note On when toggling a mask
}

// adjust the cursor position
//
// Lane view editing (DRUM). Rows are fixed for the visit; a new hit goes to
// slot 1, before any condition, shifting events right up to the first free
// slot. A full step drops slot 6 silently.
//
static void step_edit_lanes_build(void) {
    uint16_t hits[128] = {0};
    unsigned char take[128] = {0};
    struct track_event event;
    int used = 0, n = 0;
    for(int step = 0; step < SEQ_NUM_STEPS; step++)
        for(int slot = 0; slot < SEQ_TRACK_POLY; slot++)
            if(song_get_step_event(sedits.scene, sedits.track, step, slot, &event) == 0 &&
                    event.type == SONG_EVENT_NOTE && event.data0 < 128) {
                if(!hits[event.data0]) used++;
                hits[event.data0]++;
            }
    int kick = hits[36] ? 36 : hits[35] ? 35 : -1;   // same choice as the overview
    if(kick >= 0) {take[kick] = 1; n++;}
    while(n < STEP_EDIT_LANES && n < used) {
        int best = -1;
        for(int p = 0; p < 128; p++) if(hits[p] && !take[p] && (best < 0 || hits[p] > hits[best])) best = p;
        take[best] = 1; n++;
    }
    n = 0;
    for(int p = 0; p < 128; p++) if(take[p]) slanes.lanes[n++] = p;
    while(n < STEP_EDIT_LANES) slanes.lanes[n++] = -1;
    slanes.cursor = 0;
}
// Open on the lowest hit of the current step (else keep the row).
static void step_edit_lanes_cursor_to_step(void) {
    struct track_event event;
    for(int row = 0; row < STEP_EDIT_LANES; row++) {
        if(slanes.lanes[row] < 0) continue;
        for(int slot = 0; slot < SEQ_TRACK_POLY; slot++)
            if(song_get_step_event(sedits.scene, sedits.track, sedits.step_pos, slot, &event) == 0 &&
                    event.type == SONG_EVENT_NOTE && event.data0 == slanes.lanes[row]) {
                slanes.cursor = row;
                return;
            }
    }
}
static void step_edit_lanes_publish(void) {
    gui_lane_edit_set(step_edit_lanes_active(), sedits.track, slanes.lanes,
        slanes.cursor, sedits.step_pos);
}
static int step_edit_lanes_pitch(void) {
    return slanes.lanes[seq_utils_clamp(slanes.cursor, 0, STEP_EDIT_LANES - 1)];
}
// First slot holding a Note at pitch on the cursor step, or -1.
static int step_edit_lanes_find(int pitch, struct track_event *event) {
    for(int slot = 0; slot < SEQ_TRACK_POLY; slot++)
        if(song_get_step_event(sedits.scene, sedits.track, sedits.step_pos, slot, event) == 0 &&
                event->type == SONG_EVENT_NOTE && event->data0 == pitch) return slot;
    return -1;
}
static void step_edit_lanes_insert(struct track_event *hit) {
    struct track_event ev[SEQ_TRACK_POLY], tmp;
    int stop = SEQ_TRACK_POLY - 1;
    for(int slot = 0; slot < SEQ_TRACK_POLY; slot++) {
        if(song_get_step_event(sedits.scene, sedits.track, sedits.step_pos, slot, &tmp) != 0)
            tmp.type = SONG_EVENT_NULL;
        ev[slot] = tmp;
    }
    for(int slot = 0; slot < SEQ_TRACK_POLY; slot++)
        if(ev[slot].type == SONG_EVENT_NULL) {stop = slot; break;}
    for(int slot = stop; slot > 0; slot--) ev[slot] = ev[slot - 1];  // slot 6 drops if full
    ev[0] = *hit;
    for(int slot = 0; slot <= stop; slot++) {
        if(ev[slot].type == SONG_EVENT_NULL)
            song_clear_step_event(sedits.scene, sedits.track, sedits.step_pos, slot);
        else song_set_step_event(sedits.scene, sedits.track, sedits.step_pos, slot, &ev[slot]);
    }
}
//
// Lane Fill/Rot session (Shift + Pattern / Shift + Transpose in the lane
// view). Every turn is computed from a snapshot of the playing range taken
// at the first turn, so turning back restores the lane exactly:
//   lane = rotate(fill(own hits, Fill), Rot)
// Fill > 0 lays E(Fill, n) over the own hits, aligned to match most of them
// (own hits always stay); Fill < 0 thins own hits, weakest beat first.
// Filled hits: 10% below the own hits' mean velocity (100 if none). Full
// steps are never stolen: a hit that finds no free slot is "blocked". The
// session restarts when the range content no longer matches (hand edit,
// recording, undo, other row).
//
static struct {
    int valid, pitch, start, len, fill, rot, hits, blocked;
    uint32_t sum;
    struct track_event steps[SEQ_NUM_STEPS][SEQ_TRACK_POLY];
    int8_t own_slot[SEQ_NUM_STEPS];  // slot of the own hit per range index, -1 none
} slfill;
static void step_edit_region(int *start, int *length);
static uint32_t step_edit_lane_fill_sum(int pitch, int start, int len) {
    struct track_event e;
    uint32_t h = 2166136261u ^ (uint32_t)(pitch | start << 8 | len << 16);
    for(int i = 0; i < len; i++)
        for(int slot = 0; slot < SEQ_TRACK_POLY; slot++) {
            if(song_get_step_event(sedits.scene, sedits.track, (start + i) % SEQ_NUM_STEPS, slot, &e) != 0)
                e = (struct track_event){0};
            uint32_t v[3] = {e.type | e.data0 << 8 | e.data1 << 16 | (uint32_t)e.delay << 24, e.length, (uint32_t)slot};
            for(int k = 0; k < 3; k++) h = (h ^ v[k]) * 16777619u;
        }
    return h;
}
static int step_edit_lane_fill_current(int pitch) {
    int start, len;
    if(!slfill.valid || slfill.pitch != pitch) return 0;
    step_edit_region(&start, &len);
    return start == slfill.start && len == slfill.len &&
        step_edit_lane_fill_sum(pitch, start, len) == slfill.sum;
}
static void step_edit_lane_fill_begin(int pitch) {
    struct track_event e;
    step_edit_region(&slfill.start, &slfill.len);
    slfill.pitch = pitch; slfill.fill = slfill.rot = 0; slfill.blocked = 0; slfill.hits = 0;
    for(int i = 0; i < slfill.len; i++) {
        int step = (slfill.start + i) % SEQ_NUM_STEPS;
        slfill.own_slot[i] = -1;
        for(int slot = 0; slot < SEQ_TRACK_POLY; slot++) {
            if(song_get_step_event(sedits.scene, sedits.track, step, slot, &e) != 0)
                e = (struct track_event){0};
            slfill.steps[i][slot] = e;
            if(slfill.own_slot[i] < 0 && e.type == SONG_EVENT_NOTE && e.data0 == pitch) {
                slfill.own_slot[i] = slot; slfill.hits++;
            }
        }
    }
    slfill.valid = 1;
}
// Beat strength of a range index (16th grid): thinning removes weakest first.
static int step_edit_lane_strength(int i) {
    return i == 0 ? 5 : !(i % 8) ? 4 : !(i % 4) ? 3 : !(i % 2) ? 2 : 1;
}
// Compute and write the lane for the current Fill/Rot.
static void step_edit_lane_fill_apply(void) {
    int n = slfill.len, own = 0, vsum = 0, gate = 0;
    int8_t src[SEQ_NUM_STEPS];          // per index: own source index, -2 filled, -1 none
    for(int i = 0; i < n; i++) {
        src[i] = slfill.own_slot[i] >= 0 ? i : -1;
        if(src[i] >= 0) {
            struct track_event *e = &slfill.steps[i][(int)slfill.own_slot[i]];
            own++; vsum += e->data1; if(!gate) gate = e->length;
        }
    }
    if(slfill.fill < 0) {
        for(int k = 0; k < -slfill.fill; k++) {
            int worst = -1;
            for(int i = n - 1; i >= 0; i--)
                if(src[i] >= 0 && (worst < 0 || step_edit_lane_strength(i) < step_edit_lane_strength(worst)))
                    worst = i;
            if(worst < 0) break;
            src[worst] = -1;
        }
    }
    else if(slfill.fill > 0) {
        int k = slfill.fill < n ? slfill.fill : n, best = 0, best_match = -1;
        for(int r = 0; r < n; r++) {
            int match = 0;
            for(int i = 0; i < n; i++)
                if(((((i - r + n) % n) * k) % n) < k && src[i] >= 0) match++;
            if(match > best_match) {best_match = match; best = r;}
        }
        for(int i = 0; i < n; i++)
            if(((((i - best + n) % n) * k) % n) < k && src[i] < 0) src[i] = -2;
    }
    int vel = own ? (vsum * 9 + own * 5) / (own * 10) : STEP_EDIT_NEW_NOTE_VELOCITY;
    if(vel < 1) vel = 1;
    if(!gate) gate = seq_utils_step_len_to_ticks(
        song_get_step_length(seq_ctrl_get_scene(), sedits.track)) >> 1;
    int rot = ((slfill.rot % n) + n) % n;
    slfill.hits = slfill.blocked = 0;
    for(int i = 0; i < n; i++) {
        struct track_event want[SEQ_TRACK_POLY], cur;
        int step = (slfill.start + i) % SEQ_NUM_STEPS, from = (i - rot + n) % n;
        memcpy(want, slfill.steps[i], sizeof(want));
        if(slfill.own_slot[i] >= 0) want[(int)slfill.own_slot[i]].type = SONG_EVENT_NULL;
        if(src[from] != -1) {
            struct track_event hit;
            if(src[from] >= 0) hit = slfill.steps[from][(int)slfill.own_slot[from]];
            else hit = (struct track_event){.type = SONG_EVENT_NOTE, .data0 = slfill.pitch,
                .data1 = vel, .delay = SEQ_START_DELAY_INHERIT, .length = gate};
            // Unmoved own hit returns to its own slot; others go to slot 1,
            // shifting up to the first free slot. Never steal.
            if(src[from] >= 0 && from == i && want[(int)slfill.own_slot[i]].type == SONG_EVENT_NULL) {
                want[(int)slfill.own_slot[i]] = hit; slfill.hits++;
            }
            else {
                int hole = -1;
                for(int slot = 0; slot < SEQ_TRACK_POLY; slot++)
                    if(want[slot].type == SONG_EVENT_NULL) {hole = slot; break;}
                if(hole < 0) slfill.blocked++;
                else {
                    for(int slot = hole; slot > 0; slot--) want[slot] = want[slot - 1];
                    want[0] = hit; slfill.hits++;
                }
            }
        }
        for(int slot = 0; slot < SEQ_TRACK_POLY; slot++) {
            int has = song_get_step_event(sedits.scene, sedits.track, step, slot, &cur) == 0;
            if(want[slot].type == SONG_EVENT_NULL) {
                if(has) song_clear_step_event(sedits.scene, sedits.track, step, slot);
            }
            else if(!has || memcmp(&cur, &want[slot], sizeof(cur)))
                song_set_step_event(sedits.scene, sedits.track, step, slot, &want[slot]);
        }
    }
    slfill.sum = step_edit_lane_fill_sum(slfill.pitch, slfill.start, slfill.len);
}
static void step_edit_lane_fill_turn(int dfill, int drot) {
    int pitch = slanes.lanes[seq_utils_clamp(slanes.cursor, 0, STEP_EDIT_LANES - 1)];
    sedits.edit_timeout = panel_menu_get_timeout();
    slanes.moved = 0;
    if(pitch < 0) {step_edit_update_display(); return;}
    if(!step_edit_lane_fill_current(pitch)) step_edit_lane_fill_begin(pitch);
    int own = 0;
    for(int i = 0; i < slfill.len; i++) own += slfill.own_slot[i] >= 0;
    int fill = seq_utils_clamp(slfill.fill + dfill, -own, slfill.len);
    if(fill == slfill.fill && !drot) {step_edit_update_display(); return;}
    if(step_edit_prepare_undo() < 0) return;
    step_edit_stop_notes();
    slfill.fill = fill;
    slfill.rot = (slfill.rot + drot) % slfill.len;
    step_edit_lane_fill_apply();
    step_edit_update_display();
}
// mode: STEP_EDIT_ADJUST_VELO (add/velocity, shift = remove), GATE_TIME,
// START_DELAY (own delay), NOTE (choose the drum of an empty row).
static void step_edit_lanes_adjust(int change, int mode, int shift) {
    struct track_event event;
    int pitch = step_edit_lanes_pitch(), slot;
    sedits.edit_timeout = panel_menu_get_timeout();
    if(!change) {step_edit_update_display(); return;}
    if(mode == STEP_EDIT_ADJUST_NOTE) {
        // Shift + Transpose: rotate this lane (lane Fill/Rot session).
        if(shift) {
            step_edit_lane_fill_turn(0, change);
            return;
        }
        // Next pitch in that direction not used by another row or by any
        // note on the track (lanes never merge); empty rows start at GM 35/81.
        unsigned char used[128] = {0};
        for(int step = 0; step < SEQ_NUM_STEPS; step++)
            for(int s2 = 0; s2 < SEQ_TRACK_POLY; s2++)
                if(song_get_step_event(sedits.scene, sedits.track, step, s2, &event) == 0 &&
                        event.type == SONG_EVENT_NOTE && event.data0 < 128) used[event.data0] = 1;
        for(int r = 0; r < STEP_EDIT_LANES; r++)
            if(r != slanes.cursor && slanes.lanes[r] >= 0) used[slanes.lanes[r]] = 1;
        int dir = change > 0 ? 1 : -1, p = pitch < 0 ? (dir > 0 ? 34 : 82) : pitch;
        for(int n = change > 0 ? change : -change; n > 0; n--) {
            int q = p + dir;
            while(q >= 0 && q <= 127 && used[q] && q != pitch) q += dir;
            if(q < 0 || q > 127) break;
            p = q;
        }
        if(p < 0 || p > 127 || p == pitch) {step_edit_update_display(); return;}
        if(pitch >= 0) {
            // Move every hit of this lane, all 64 steps (one undo point).
            int moved = 0;
            for(int step = 0; step < SEQ_NUM_STEPS && !moved; step++)
                for(int s2 = 0; s2 < SEQ_TRACK_POLY; s2++)
                    if(song_get_step_event(sedits.scene, sedits.track, step, s2, &event) == 0 &&
                            event.type == SONG_EVENT_NOTE && event.data0 == pitch) {moved = 1; break;}
            if(moved) {
                if(step_edit_prepare_undo() < 0) return;
                step_edit_stop_notes();
                for(int step = 0; step < SEQ_NUM_STEPS; step++)
                    for(int s2 = 0; s2 < SEQ_TRACK_POLY; s2++)
                        if(song_get_step_event(sedits.scene, sedits.track, step, s2, &event) == 0 &&
                                event.type == SONG_EVENT_NOTE && event.data0 == pitch) {
                            event.data0 = p;
                            song_set_step_event(sedits.scene, sedits.track, step, s2, &event);
                        }
                slanes.moved = 1;
            }
        }
        slanes.lanes[slanes.cursor] = p;
        sedits.last_adjust_type = STEP_EDIT_ADJUST_NOTE;
        step_edit_update_display();
        return;
    }
    slanes.moved = 0;
    if(pitch < 0) {step_edit_update_display(); return;}
    slot = step_edit_lanes_find(pitch, &event);
    if(mode == STEP_EDIT_ADJUST_VELO && shift) {
        if(slot < 0) return;
        if(step_edit_prepare_undo() < 0) return;
        step_edit_stop_notes();
        while((slot = step_edit_lanes_find(pitch, &event)) >= 0)
            song_clear_step_event(sedits.scene, sedits.track, sedits.step_pos, slot);
    }
    else if(mode == STEP_EDIT_ADJUST_VELO && slot < 0) {
        if(step_edit_prepare_undo() < 0) return;
        step_edit_stop_notes();
        struct track_event hit = {.type = SONG_EVENT_NOTE, .data0 = pitch,
            .data1 = STEP_EDIT_NEW_NOTE_VELOCITY, .delay = SEQ_START_DELAY_INHERIT,
            .length = seq_utils_step_len_to_ticks(
                song_get_step_length(seq_ctrl_get_scene(), sedits.track)) >> 1};
        step_edit_lanes_insert(&hit);
        step_edit_play_step();
    }
    else if(slot >= 0) {
        if(step_edit_prepare_undo() < 0) return;
        step_edit_stop_notes();
        for(slot = 0; slot < SEQ_TRACK_POLY; slot++) {
            if(song_get_step_event(sedits.scene, sedits.track, sedits.step_pos, slot, &event) != 0 ||
                    event.type != SONG_EVENT_NOTE || event.data0 != pitch) continue;
            if(mode == STEP_EDIT_ADJUST_VELO)
                event.data1 = seq_utils_clamp(event.data1 + change * 2, 1, 127);
            else if(mode == STEP_EDIT_ADJUST_GATE_TIME)
                event.length = seq_utils_clamp(event.length +
                    seq_utils_warp_change(event.length, change, 10),
                    STEP_EDIT_SHORTEST_NOTE, STEP_EDIT_LONGEST_NOTE);
            else if(mode == STEP_EDIT_ADJUST_START_DELAY)
                event.delay = seq_utils_delay_adjust(event.delay, change, 1);
            song_set_step_event(sedits.scene, sedits.track, sedits.step_pos, slot, &event);
        }
        if(mode == STEP_EDIT_ADJUST_VELO) step_edit_play_step();
    }
    sedits.last_adjust_type = mode;
    step_edit_update_display();
}
static void step_edit_lanes_display(void) {
    char line[GFX_LABEL_LEN], name[8], a[16], b[16];
    struct track_event event;
    int pitch = step_edit_lanes_pitch();
    gui_step_details_set(0);
    snprintf(line, sizeof(line), "Lanes T%d Step %02d", sedits.track + 1, sedits.step_pos + 1);
    gui_set_status_text(0, line);
    if(pitch < 0) {
        gui_set_status_text(1, "-- empty row");
        gui_set_status_text(2, "Transpose: choose drum");
    }
    else {
        panel_utils_drum_name(name, pitch);
        int n = strlen(name);
        while(n > 0 && name[n - 1] == ' ') name[--n] = 0;
        snprintf(line, sizeof(line), "%s %d%s", name, pitch, slanes.moved ? " (lane moved)" : "");
        gui_set_status_text(1, line);
        if(step_edit_lanes_find(pitch, &event) < 0) {
            gui_set_status_text(2, "No hit  Value: add");
        }
        else {
            panel_utils_gate_time_to_str(a, event.length);
            snprintf(line, sizeof(line), "Vel %d Gate %.6s", event.data1, a);
            gui_set_status_text(2, line);
            if(event.delay == SEQ_START_DELAY_INHERIT) strcpy(b, "Step");
            else if(seq_utils_delay_relative_label(event.delay))
                snprintf(b, sizeof(b), "%s", seq_utils_delay_relative_label(event.delay));
            else if(!event.delay) strcpy(b, "OFF");
            else panel_utils_gate_time_to_str(b, event.delay);
            snprintf(line, sizeof(line), "Dly %.6s Shift+Val: del", b);
            gui_set_status_text(3, line);
        }
    }
    if(pitch >= 0 && step_edit_lane_fill_current(pitch)) {
        // Lines 2-3 show the lane session instead of the cursor hit.
        int n = slfill.len, rot = ((slfill.rot % n) + n) % n;
        if(slfill.fill > 0)
            snprintf(line, sizeof(line), "Fill +%d E%d/%d Rot %d", slfill.fill, slfill.fill, n, rot);
        else snprintf(line, sizeof(line), "Fill %d Rot %d", slfill.fill, rot);
        gui_set_status_text(2, line);
        if(slfill.blocked) snprintf(line, sizeof(line), "%d hits  %d blocked", slfill.hits, slfill.blocked);
        else snprintf(line, sizeof(line), "%d hits", slfill.hits);
        gui_set_status_text(3, line);
    }
    step_edit_lanes_publish();
}

void step_edit_adjust_cursor(int change, int shift) {
    int val;
    // Lane view: Shift + Motion Start moves the lane cursor up/down.
    if(shift && step_edit_lanes_active()) {
        sedits.edit_timeout = panel_menu_get_timeout();
        // clockwise moves down (row 0 is the bottom row)
        slanes.cursor = seq_utils_clamp(slanes.cursor - change, 0, STEP_EDIT_LANES - 1);
        slanes.moved = 0;
        step_edit_update_display();
        return;
    }
    sedits.edit_timeout = panel_menu_get_timeout();
    sedits.cci_hint_timeout = 0;

    // select event pos
    if(shift) {
        int was_region = sedits.event_pos == STEP_EDIT_EVENT_POS_REGION;
        sedits.event_pos = seq_utils_clamp(sedits.event_pos + change,
            STEP_EDIT_EVENT_POS_REGION, SEQ_TRACK_POLY - 1);
        int is_region = sedits.event_pos == STEP_EDIT_EVENT_POS_REGION;
        if(was_region != is_region) {
            sedits.region_filter = -1;  // a filter never outlives its Region visit
            step_edit_stop_notes();
            sedits.undo_captured = 0;  // region edits get their own undo point
            step_edit_region_overlay(is_region);
        }
        step_edit_show_cci_hint();
        step_edit_update_display();
    }
    // select step
    else {
        // Moving to a step leaves region editing.
        if(sedits.event_pos == STEP_EDIT_EVENT_POS_REGION) {
            step_edit_region_overlay(0);
            sedits.region_filter = -1;
            sedits.event_pos = STEP_EDIT_EVENT_POS_ALL;
            sedits.undo_captured = 0;
        }
        val = sedits.step_pos + change;
        if(val < 0) {
            val = SEQ_NUM_STEPS - 1;
        }
        else if(val >= SEQ_NUM_STEPS) {
            val = 0;
        }
        if(val != sedits.step_pos) {
            // Refuse navigation if we cannot protect the new step's entry state.
            if(seq_engine_undo_capture() < 0) return;
            sedits.undo_captured = 1;
            step_edit_clear_recording_notes();
        }
        // disable old step
        step_edit_stop_notes();
        gui_grid_set_overlay_color(sedits.step_pos, GUI_OVERLAY_BLANK);
        // enable new step
        sedits.step_pos = val;
        // display / play step
        step_edit_update_display();
        step_edit_play_step();
        gui_grid_set_overlay_color(sedits.step_pos, GUI_OVERLAY_HIGH);
    }
}

// adjust the step note
void step_edit_adjust_note(int change, int shift) {
    struct track_event event = {.delay = SEQ_START_DELAY_INHERIT};
    sedits.edit_timeout = panel_menu_get_timeout();
    if(step_edit_lanes_active()) {step_edit_lanes_adjust(change, STEP_EDIT_ADJUST_NOTE, shift); return;}

    // Region: Shift + Transpose steps the pitch filter (All, then pitches).
    if(shift && sedits.event_pos == STEP_EDIT_EVENT_POS_REGION) {
        step_edit_region_filter_step(change);
        return;
    }
    // shift + transpose cycles the selected event type
    if(shift && sedits.event_pos >= 0 &&
            song_get_step_event(sedits.scene, sedits.track, sedits.step_pos,
            sedits.event_pos, &event) != -1) {
        if(step_edit_prepare_undo() < 0) return;
        static const uint8_t types[] = {SONG_EVENT_NOTE, SONG_EVENT_CC,
            SONG_EVENT_INTERNAL_CC, SONG_EVENT_PROGCHG, SONG_EVENT_SCENE, SONG_EVENT_PROBABILITY,
            SONG_EVENT_EUCLIDEAN, SONG_EVENT_PRE, SONG_EVENT_NON_PRE, SONG_EVENT_HARMONY, SONG_EVENT_INVERSION, SONG_EVENT_GLIDE};
        int index;
        for(index = 0; index < 12; index ++) {
            if(types[index] == event.type) break;
        }
        index = (index + (change > 0 ? 1 : 11)) % 12;
        event.type = types[index];
        event.delay = SEQ_START_DELAY_INHERIT;
        event.data0 = 0;
        event.data1 = 0;
        event.length = 0;
        switch(event.type) {
            case SONG_EVENT_SCENE:
                event.data0 = 63; // start transparent: all scenes pass
                break;
            case SONG_EVENT_GLIDE:
                event.data0 = 4; // 1/16
                event.data1 = GLIDE_LINEAR;
                break;
            case SONG_EVENT_NOTE:
                event.data0 = STEP_EDIT_NEW_NOTE;
                event.data1 = STEP_EDIT_NEW_NOTE_VELOCITY;
                event.length = seq_utils_step_len_to_ticks(
                    song_get_step_length(sedits.scene, sedits.track)) >> 1;
                break;
            case SONG_EVENT_CC:
                event.data0 = STEP_EDIT_NEW_CC;
                event.data1 = STEP_EDIT_NEW_CC_VAL;
                break;
            case SONG_EVENT_INTERNAL_CC:
                event.data0 = MIDI_CTRL_CC_EUCLIDEAN_DENSITY;
                event.data1 = STEP_EDIT_NEW_CC_VAL;
                break;
            case SONG_EVENT_HARMONY:
                event.data0 = SONG_HARMONY_SCALE_KEEP;
                event.data1 = SONG_HARMONY_ROOT_KEEP;
                break;
            case SONG_EVENT_PROBABILITY:
                event.data0 = 50;
                break;
            default:
                // PC=1, Euclidean=1:2 rotation 0, Pre/non-Pre unused.
                break;
        }
        song_set_step_event(sedits.scene, sedits.track, sedits.step_pos,
            sedits.event_pos, &event);
        sedits.cci_hint_timeout = 0;
        step_edit_show_cci_hint();
        step_edit_update_display();
        return;
    }

    if(sedits.event_pos == STEP_EDIT_EVENT_POS_REGION) {
        if(shift) step_edit_region_filter_step(change);
        else step_edit_region_adjust(change, STEP_EDIT_ADJUST_NOTE);
    }
    else if(sedits.event_pos == STEP_EDIT_EVENT_POS_ALL) {
        step_edit_adjust_step(change, STEP_EDIT_ADJUST_NOTE, 0, shift);
    }
    else {
        step_edit_adjust_step(change, STEP_EDIT_ADJUST_NOTE, 1, shift);
    }
}

// adjust the step velocity
void step_edit_adjust_velocity(int change, int shift) {
    if(step_edit_lanes_active()) {step_edit_lanes_adjust(change, STEP_EDIT_ADJUST_VELO, shift); return;}
    sedits.edit_timeout = panel_menu_get_timeout();
    if(sedits.event_pos == STEP_EDIT_EVENT_POS_REGION) {
        if(!shift) step_edit_region_adjust(change, STEP_EDIT_ADJUST_VELO);
    }
    else if(sedits.event_pos == STEP_EDIT_EVENT_POS_ALL) {
        step_edit_adjust_step(change, STEP_EDIT_ADJUST_VELO, 0, shift);
    }
    else {
        step_edit_adjust_step(change, STEP_EDIT_ADJUST_VELO, 1, shift);
    }
}

// adjust the step gate time
void step_edit_adjust_gate_time(int change, int shift) {
    if(step_edit_lanes_active()) {
        if(!shift) step_edit_lanes_adjust(change, STEP_EDIT_ADJUST_GATE_TIME, 0);
        return;
    }
    sedits.edit_timeout = panel_menu_get_timeout();
    if(sedits.event_pos == STEP_EDIT_EVENT_POS_REGION) {
        if(!shift) step_edit_region_adjust(change, STEP_EDIT_ADJUST_GATE_TIME);
    }
    else if(sedits.event_pos == STEP_EDIT_EVENT_POS_ALL) {
        step_edit_adjust_step(change, STEP_EDIT_ADJUST_GATE_TIME, 0, shift);
    }
    else {
        step_edit_adjust_step(change, STEP_EDIT_ADJUST_GATE_TIME, 1, shift);
    }
}

// adjust the start delay
void step_edit_adjust_start_delay(int change, int shift) {
    if(step_edit_lanes_active()) {
        if(!shift) step_edit_lanes_adjust(change, STEP_EDIT_ADJUST_START_DELAY, 0);
        return;
    }
    if(sedits.event_pos == STEP_EDIT_EVENT_POS_REGION) {
        sedits.edit_timeout = panel_menu_get_timeout();
        if(!shift) step_edit_region_adjust(change, STEP_EDIT_ADJUST_START_DELAY);
        return;
    }
    if(change && step_edit_prepare_undo() < 0) return;
    sedits.edit_timeout = panel_menu_get_timeout();
    step_edit_stop_notes();
    if(sedits.event_pos == STEP_EDIT_EVENT_POS_ALL) {
        int old = song_get_start_delay(sedits.scene, sedits.track, sedits.step_pos);
        song_set_start_delay(sedits.scene, sedits.track, sedits.step_pos,
            seq_utils_delay_adjust(old, change, 0));
    } else {
        struct track_event event;
        if(song_get_step_event(sedits.scene, sedits.track, sedits.step_pos,
                sedits.event_pos, &event) == 0 && event.type == SONG_EVENT_NOTE) {
            event.delay = seq_utils_delay_adjust(event.delay, change, 1);
            song_set_step_event(sedits.scene, sedits.track, sedits.step_pos,
                sedits.event_pos, &event);
        }
    }
    sedits.last_adjust_type = STEP_EDIT_ADJUST_START_DELAY;
    step_edit_update_display();
}

// adjust the ratchet mode
void step_edit_adjust_ratchet_mode(int change, int shift) {
    if(step_edit_lanes_active()) {
        // Lane view: Shift + Pattern = lane Fill (Euclidean over this lane);
        // ratchet stays in the compact/expanded views.
        if(shift && change) step_edit_lane_fill_turn(change, 0);
        return;
    }
    if(sedits.event_pos == STEP_EDIT_EVENT_POS_REGION) return;  // not a region parameter
    if(change && step_edit_prepare_undo() < 0) return;
    sedits.edit_timeout = panel_menu_get_timeout();
    song_set_ratchet_mode(sedits.scene, sedits.track, sedits.step_pos,
        seq_utils_clamp(song_get_ratchet_mode(sedits.scene, sedits.track,
        sedits.step_pos) + change,
        SEQ_RATCHET_MIN, SEQ_RATCHET_MAX));
    // A single-hit audition would misrepresent ratcheting. Update silently;
    // normal sequenced playback remains the source of the repeated preview.
    step_edit_update_display();
}

// clear the current step
void step_edit_clear_step(void) {
    if(sedits.event_pos == STEP_EDIT_EVENT_POS_REGION) return;  // no region-wide clear
    if(step_edit_prepare_undo() < 0) return;
    sedits.cci_hint_timeout = 0;
    sedits.edit_timeout = panel_menu_get_timeout();
    step_edit_stop_notes();
    // clear specific event (lane view always clears the whole step)
    if(sedits.event_pos != STEP_EDIT_EVENT_POS_ALL && !step_edit_lanes_active()) {
        song_clear_step_event(sedits.scene, sedits.track,
            sedits.step_pos, sedits.event_pos);
    }
    // clear all events on step
    else {
        song_clear_step(sedits.scene, sedits.track, sedits.step_pos);
    }
    // display / play step
    step_edit_update_display();
}

//
// local functions
//
// One detent per distinct playback setting, using a stable representative raw
// value for each bucket. External CCs retain their ordinary raw-value editor.
static int step_edit_adjust_cci_value(int controller, int raw, int change) {
    int direction = change > 0 ? 1 : -1;
    int steps = seq_utils_clamp(change > 0 ? change : -change, 0, 128);
    int decoded, next, candidate;
    if(controller == MIDI_CTRL_CC_GLIDE_CURVE) return seq_utils_clamp(raw + change, 0, 127);
    if(controller == MIDI_CTRL_CC_GLIDE) return seq_utils_clamp(raw + change, 0, GLIDE_MAX);
    if(!change) return raw;
    if(controller == MIDI_CTRL_CC_EUCLIDEAN_ROTATION) {
        // Usability compromise: retain intermediate fractions for all targets,
        // rather than skipping buckets based on the first target's length.
        return seq_utils_clamp(raw + change * 2, 0, 127);
    }
    if(!midi_ctrl_internal_cc_supported(controller))
        return seq_utils_clamp(raw + change * 2, 0, 127);
    while(steps-- > 0) {
        decoded = midi_ctrl_internal_cc_to_value(controller, raw, SEQ_NUM_STEPS);
        candidate = raw + direction;
        while(candidate >= 0 && candidate <= 127 &&
                midi_ctrl_internal_cc_to_value(controller, candidate, SEQ_NUM_STEPS) == decoded)
            candidate += direction;
        if(candidate < 0 || candidate > 127) break;
        next = midi_ctrl_internal_cc_to_value(controller, candidate, SEQ_NUM_STEPS);
        // Canonical first raw value makes forward/backward navigation stable.
        while(candidate > 0 &&
                midi_ctrl_internal_cc_to_value(controller, candidate - 1, SEQ_NUM_STEPS) == next)
            candidate--;
        raw = candidate;
    }
    return raw;
}

// adjust all events on a step
void step_edit_adjust_step(int change, int mode, int single, int shift) {
    if(change && step_edit_prepare_undo() < 0) return;
    int inhibit_preview = step_edit_scene_mask() >= 0;
    struct track_event event = {.delay = SEQ_START_DELAY_INHERIT};
    int i, start, num, temp;

    // stop playing before editing
    step_edit_stop_notes();

    // create a note or a CC depending on what has been turned
    // if there are no events or the current edit pos is blank
    if(song_get_num_step_events(sedits.scene, sedits.track, sedits.step_pos) == 0 ||
            (sedits.event_pos != STEP_EDIT_EVENT_POS_ALL &&
            song_get_step_event(sedits.scene, sedits.track, sedits.step_pos,
            sedits.event_pos, &event) == -1)) {
        // A blank slot may retain payload from a deleted Note.
        event.delay = SEQ_START_DELAY_INHERIT;
        // figure out single slot position
        if(single && sedits.event_pos != STEP_EDIT_EVENT_POS_ALL) {
            start = sedits.event_pos;
        }
        // otherwise insert the event on the first slot
        else {
            start = 0;
        }
        // if shift was pressed we insert a CC
        if(shift) {
            // create a new CC value
            event.type = SONG_EVENT_CC;
            // prevent inserting a duplicate value on another slot
            if(step_edit_does_step_contain_event(SONG_EVENT_CC, STEP_EDIT_NEW_CC) != -1) {
                event.data0 = STEP_EDIT_NEW_CC + 1;
            }
            else {
                event.data0 = STEP_EDIT_NEW_CC;
            }
            event.data1 = STEP_EDIT_NEW_CC_VAL;
            inhibit_preview = 1;  // user must retrigger edit to hear
        }
        // otherwise insert a note
        else {
            // create a new note
            event.type = SONG_EVENT_NOTE;
            // prevent inserting a duplicate value on another slot
            if(step_edit_does_step_contain_event(SONG_EVENT_NOTE, STEP_EDIT_NEW_NOTE) != -1) {
                event.data0 = STEP_EDIT_NEW_NOTE + 1;
            }
            else {
                event.data0 = STEP_EDIT_NEW_NOTE;
            }
            event.data1 = STEP_EDIT_NEW_NOTE_VELOCITY;
            // default to half the step len
            event.length = seq_utils_step_len_to_ticks(
                song_get_step_length(seq_ctrl_get_scene(), sedits.track)) >> 1;
        }
        song_set_step_event(sedits.scene, sedits.track, sedits.step_pos,
            start, &event);
    }
    // otherwise let's edit the existing events
    else {
        // figure out edit pos for single
        if(single && sedits.event_pos != STEP_EDIT_EVENT_POS_ALL) {
            start = sedits.event_pos;
            num = 1;
        }
        // otherwise edit all
        else {
            start = 0;
            num = SEQ_TRACK_POLY;
        }

        // modify events and put them back into the step
        for(i = start; i < (start + num); i ++) {
            // get the slot event
            if(song_get_step_event(sedits.scene, sedits.track,
                    sedits.step_pos, i, &event) == -1) {
                continue;
            }
            // edit the contents
            switch(mode) {
                case STEP_EDIT_ADJUST_NOTE:  // note number or selected event parameter
                    // Group transpose is musical pitch editing, not a bulk edit
                    // of unrelated control parameters (Prb, CC, Hrm, etc.).
                    if(!single && event.type != SONG_EVENT_NOTE) continue;
                    // note
                    if(event.type == SONG_EVENT_NOTE) {
                        event.data0 += change;
                        // the new value would collide with another slot
                        if(step_edit_does_step_contain_event(SONG_EVENT_NOTE, event.data0) != -1) {
                            if(change > 0) {
                                event.data0 ++;
                            }
                            else {
                                event.data0 --;
                            }
                        }
                        // note went out of range
                        if(event.data0 < STEP_EDIT_LOWEST_NOTE ||
                                event.data0 > STEP_EDIT_HIGHEST_NOTE) {
                            continue;
                        }
                        sedits.last_adjust_type = STEP_EDIT_ADJUST_NOTE;
                    }
                    // external CC
                    else if(event.type == SONG_EVENT_CC) {
                        event.data0 += change;
                        // the new value would collide with another slot
                        if(step_edit_does_step_contain_event(event.type, event.data0) != -1) {
                            if(change > 0) {
                                event.data0 ++;
                            }
                            else {
                                event.data0 --;
                            }
                        }
                        inhibit_preview = 1;  // user must retrigger edit to hear
                        // CC went out of range
                        if(event.data0 < 0 || event.data0 > 127) {
                            continue;
                        }
                    }
                    // Internal CC: 16-29 plus 34-35; external-only 30-33 are skipped.
                    else if(event.type == SONG_EVENT_INTERNAL_CC) {
                        int index = event.data0 >= MIDI_CTRL_CC_GLIDE ? event.data0 - MIDI_CTRL_CC_GLIDE + 14 : event.data0 - 16;
                        index = seq_utils_clamp(index + change, 0, 15);
                        event.data0 = index >= 14 ? MIDI_CTRL_CC_GLIDE + index - 14 : index + 16;
                        if(event.data0 == MIDI_CTRL_CC_GLIDE) event.data1 = seq_utils_clamp(event.data1, 0, GLIDE_MAX);
                        if(single) {
                            sedits.cci_hint_controller = event.data0;
                            sedits.cci_hint_show_value = 0;
                            sedits.cci_hint_timeout = STEP_EDIT_CCI_HINT_MS;
                        }
                        inhibit_preview = 1;
                    }
                    else if(event.type == SONG_EVENT_GLIDE) {
                        event.data0 = seq_utils_clamp(event.data0 + change, 0, GLIDE_MAX);
                        sedits.last_adjust_type = STEP_EDIT_ADJUST_NOTE;
                        inhibit_preview = 1;
                    }
                    else if(event.type == SONG_EVENT_INVERSION) {
                        event.data0 = seq_utils_clamp(event.data0 + change, 0, SONG_INVERSION_MAX);
                        inhibit_preview = 1;
                    }
                    else if(event.type == SONG_EVENT_HARMONY) {
                        event.data0 = seq_utils_clamp(event.data0 + change,
                            SONG_HARMONY_SCALE_KEEP, SONG_HARMONY_SCALE_MAX);
                        inhibit_preview = 1;
                    }
                    // probability occupies the first numeric field only
                    else if(event.type == SONG_EVENT_PROBABILITY) {
                        event.data0 = seq_utils_clamp(event.data0 + change, 0, 100);
                        inhibit_preview = 1;
                    }
                    else if(event.type == SONG_EVENT_EUCLIDEAN) {
                        unsigned a, b;
                        event.data0 = seq_utils_clamp(event.data0 + change, 0, 27);
                        conditional_pair(event.data0, &a, &b);
                        event.data1 %= b;
                        inhibit_preview = 1;
                    }
                    // program change
                    else if(event.type == SONG_EVENT_PROGCHG) {
                        event.data0 = seq_utils_clamp(event.data0 + change, 0, 127);
                        inhibit_preview = 1;  // user must retrigger edit to hear
                    }
                    // put back modified event
                    song_set_step_event(sedits.scene, sedits.track,
                        sedits.step_pos, i, &event);
                    break;
                case STEP_EDIT_ADJUST_VELO:  // note velocity and CC value
                    // note or CC
                    if(event.type == SONG_EVENT_NOTE) {
                        event.data1 = seq_utils_clamp(event.data1 + change * 2, 0, 127);
                    }
                    // external or internal CC
                    else if(event.type == SONG_EVENT_CC ||
                            event.type == SONG_EVENT_INTERNAL_CC) {
                        event.data1 = event.type == SONG_EVENT_INTERNAL_CC
                            ? step_edit_adjust_cci_value(event.data0, event.data1, change)
                            : seq_utils_clamp(event.data1 + change * 2, 0, 127);
                        if(single && event.type == SONG_EVENT_INTERNAL_CC) {
                            sedits.cci_hint_controller = event.data0;
                            sedits.cci_hint_value = event.data1;
                            sedits.cci_hint_show_value = 1;
                            sedits.cci_hint_timeout = STEP_EDIT_CCI_HINT_MS;
                        }
                        inhibit_preview = 1;  // user must retrigger edit to hear
                    }
                    else if(event.type == SONG_EVENT_GLIDE) {
                        event.data1 = seq_utils_clamp(event.data1 + change, 0, 127);
                        inhibit_preview = 1;
                    }
                    else if(event.type == SONG_EVENT_INVERSION) {
                        event.data1 = seq_utils_clamp(event.data1 + change, 0, 1);
                        inhibit_preview = 1;
                    }
                    else if(event.type == SONG_EVENT_HARMONY) {
                        event.data1 = seq_utils_clamp(event.data1 + change,
                            SONG_HARMONY_ROOT_KEEP, SONG_HARMONY_ROOT_MAX);
                        inhibit_preview = 1;
                    }
                    else if(event.type == SONG_EVENT_EUCLIDEAN) {
                        unsigned a, b;
                        conditional_pair(event.data0, &a, &b);
                        event.data1 = seq_utils_clamp(event.data1 + change, 0, b - 1);
                        inhibit_preview = 1;
                    }
                    sedits.last_adjust_type = STEP_EDIT_ADJUST_VELO;
                    // put back modified event
                    song_set_step_event(sedits.scene, sedits.track,
                        sedits.step_pos, i, &event);
                    break;
                case STEP_EDIT_ADJUST_GATE_TIME:
                    if(event.type == SONG_EVENT_NOTE) {
                        event.length = seq_utils_clamp(event.length +
                            seq_utils_warp_change(event.length, change, 10),
                            STEP_EDIT_SHORTEST_NOTE, STEP_EDIT_LONGEST_NOTE);
                    }
                    sedits.last_adjust_type = STEP_EDIT_ADJUST_GATE_TIME;
                    inhibit_preview = 1;  // user must retrigger edit to hear
                    // put back modified event
                    song_set_step_event(sedits.scene, sedits.track,
                        sedits.step_pos, i, &event);
                    break;
                case STEP_EDIT_ADJUST_START_DELAY:
                    if(event.type == SONG_EVENT_NOTE) {
                        temp = song_get_start_delay(sedits.scene,
                            sedits.track, sedits.step_pos);
                        song_set_start_delay(sedits.scene, sedits.track,
                            sedits.step_pos,
                            seq_utils_clamp(temp +
                            seq_utils_warp_change(temp, change, 10),
                            SEQ_START_DELAY_MIN, SEQ_START_DELAY_MAX));
                    }
                    sedits.last_adjust_type = STEP_EDIT_ADJUST_START_DELAY;
                    inhibit_preview = 1;  // user must retrigger edit to hear
                    break;
            }
        }
    }
    // display / play step
    step_edit_update_display();
    if(!inhibit_preview) {
        step_edit_play_step();
    }
}

// CCi descriptions match the manual's internal stepped automation table.
static const char *step_edit_cci_name(int controller) {
    static const char *const names[] = {
        "Step length", "Track transpose", "Track mute", "Motion start",
        "Motion length", "Motion direction", "Gate time", "Pattern type",
        "Arp enable", "Arp type", "Arp speed", "Arp gate time",
        "Euclidean density", "Euclidean rotation"
    };
    if(controller == MIDI_CTRL_CC_GLIDE) return "Glide Duration";
    if(controller == MIDI_CTRL_CC_GLIDE_CURVE) return "Glide Curve";
    if(controller < MIDI_CTRL_CC_STEP_LENGTH || controller > MIDI_CTRL_CC_EUCLIDEAN_ROTATION)
        return NULL;
    return names[controller - MIDI_CTRL_CC_STEP_LENGTH];
}

static void step_edit_show_cci_hint(void) {
    struct track_event event = {.delay = SEQ_START_DELAY_INHERIT};
    if(sedits.event_pos < 0) return;
    if(song_get_step_event(sedits.scene, sedits.track, sedits.step_pos,
            sedits.event_pos, &event) == -1 || event.type != SONG_EVENT_INTERNAL_CC ||
            !step_edit_cci_name(event.data0)) return;
    sedits.cci_hint_controller = event.data0;
    sedits.cci_hint_value = event.data1;
    sedits.cci_hint_show_value = 0;
    sedits.cci_hint_timeout = STEP_EDIT_CCI_HINT_MS;
}

// Both Euclidean hints use the first receiving track, not the source length.
static int step_edit_first_bias_target(int *length) {
    int target;
    for(target = 0; target < SEQ_NUM_TRACKS; target++) {
        if(target == sedits.track ||
                song_get_bias_track(sedits.scene, target) != sedits.track) continue;
        *length = sedits.scene == seq_engine_get_current_scene()
            ? seq_engine_get_effective_motion_length(target)
            : song_get_motion_length(sedits.scene, target);
        *length = seq_utils_clamp(*length, 1, SEQ_NUM_STEPS);
        return target;
    }
    return -1;
}

// Decode through the same conversion as playback; never alter stored MIDI values.
static void step_edit_cci_value_title(char *title, int controller, int raw) {
    char value[32];
    int decoded = midi_ctrl_internal_cc_to_value(controller, raw, SEQ_NUM_STEPS);
    const char *label = "Value";
    switch(controller) {
        case MIDI_CTRL_CC_GLIDE_CURVE:
            snprintf(title, GFX_LABEL_LEN, "CCi 35: Curve %d %s", raw,
                raw == 64 ? "Linear" : raw < 64 ? "Slow Start" : "Fast Start");
            return;
        case MIDI_CTRL_CC_GLIDE:
            snprintf(title, GFX_LABEL_LEN, "CCi 34: Glide %s", glide_label(raw));
            return;
        case MIDI_CTRL_CC_STEP_LENGTH:
        case MIDI_CTRL_CC_ARP_SPEED:
            if(controller == MIDI_CTRL_CC_ARP_SPEED && decoded == SEQ_UTILS_ARP_CHORD)
                strcpy(value, "Chord");
            else panel_utils_step_len_to_str(value, decoded);
            label = controller == MIDI_CTRL_CC_STEP_LENGTH ? "Step length" : "Arp speed";
            break;
        case MIDI_CTRL_CC_TRACK_TRANSPOSE:
            label = "Transpose";
            snprintf(value, sizeof(value), "%+d semis", decoded);
            break;
        case MIDI_CTRL_CC_TRACK_MUTE:
        case MIDI_CTRL_CC_ARP_ENABLE:
            label = controller == MIDI_CTRL_CC_TRACK_MUTE ? "Mute" : "Arp enable";
            snprintf(value, sizeof(value), "%s", decoded ? "On" : "Off");
            break;
        case MIDI_CTRL_CC_MOTION_DIR:
            label = "Direction";
            snprintf(value, sizeof(value), "%s", decoded ? "Reverse" : "Forward");
            break;
        case MIDI_CTRL_CC_MOTION_START:
        case MIDI_CTRL_CC_MOTION_LENGTH:
            label = controller == MIDI_CTRL_CC_MOTION_START ? "Start" : "Length";
            snprintf(value, sizeof(value), "%d", decoded + (controller == MIDI_CTRL_CC_MOTION_START));
            break;
        case MIDI_CTRL_CC_GATE_TIME:
            label = "Gate time";
            // Match gui_update_gate_time_override, including max=200%.
            snprintf(value, sizeof(value), "%d%%", (decoded + 1) * 200 / 256);
            break;
        case MIDI_CTRL_CC_PATTERN_TYPE:
            label = "Pattern type";
            snprintf(value, sizeof(value), "%d", decoded + 1);
            break;
        case MIDI_CTRL_CC_ARP_TYPE:
            label = "Arp type";
            arp_type_to_name(value, decoded);
            break;
        case MIDI_CTRL_CC_ARP_GATE_TIME:
            label = "Arp gate";
            snprintf(value, sizeof(value), "%d ticks", decoded);
            break;
        case MIDI_CTRL_CC_EUCLIDEAN_DENSITY: {
            int length, target = step_edit_first_bias_target(&length);
            if(target < 0)
                snprintf(title, GFX_LABEL_LEN, "CCi 28: %d%% No Bias target", decoded);
            else if(decoded == 100)
                // 100% restores the selected pattern, not an all-hits mask.
                snprintf(title, GFX_LABEL_LEN, "CCi 28: T%d 100%% Pattern", target + 1);
            else
                snprintf(title, GFX_LABEL_LEN, "CCi 28: T%d %d%% %d/%d",
                    target + 1, decoded, (decoded * length + 50) / 100, length);
            return;
        }
        case MIDI_CTRL_CC_EUCLIDEAN_ROTATION: {
            int length, target = step_edit_first_bias_target(&length);
            if(target >= 0) {
                decoded = midi_ctrl_internal_cc_to_value(controller, raw, length);
                snprintf(title, GFX_LABEL_LEN, "CCi 29: T%d Rot %d/%d",
                    target + 1, decoded, length);
                return;
            }
            snprintf(title, GFX_LABEL_LEN, "CCi 29: No Bias target");
            return;
        }
        default:
            snprintf(value, sizeof(value), "%d", raw);
            break;
    }
    snprintf(title, GFX_LABEL_LEN, "CCi %d: %s %s", controller, label, value);
}

//
// region editing: every playing step of the track (stored motion start and
// length of the edit scene, wrapping). Pattern/Euclid masks are ignored.
// Notes only; steps are shared by all scenes. One undo point per region visit.
//
static void step_edit_region(int *start, int *length) {
    *start = seq_utils_clamp(song_get_motion_start(sedits.scene, sedits.track), 0, SEQ_NUM_STEPS - 1);
    *length = seq_utils_clamp(song_get_motion_length(sedits.scene, sedits.track), 1, SEQ_NUM_STEPS);
}
static void step_edit_region_overlay(int on) {
    int start, length;
    step_edit_region(&start, &length);
    for(int i = 0; i < length; i++)
        gui_grid_set_overlay_color((start + i) % SEQ_NUM_STEPS, on ? GUI_OVERLAY_MED : GUI_OVERLAY_BLANK);
    if(!on) gui_grid_set_overlay_color(sedits.step_pos, GUI_OVERLAY_HIGH);
}
// Returns 1 if anything changed. Every value stops at its limit (Note 0-127,
// Vel 0-127, Gate and Dly at their single-step limits), so pushing a region
// against a limit levels it to one value, which can then be moved as a set.
// Vel/Gate use the single-step rules per note. Dly changes the shared step
// delay on steps that contain at least one Note.
// With a pitch filter only notes at that pitch change: Dly then edits their
// per-note delay, and a Note move jumps over pitches used in the region;
// the filter follows the moved notes.
static int step_edit_region_pitches(int *list);
// Filtered lane move: one free pitch per detent, jumping over pitches used
// in the region, so a lane moves as one piece and never merges with another.
static int step_edit_region_free_pitch(int from, int change) {
    int list[128], count = step_edit_region_pitches(list), dir = change > 0 ? 1 : -1;
    unsigned char used[128] = {0};
    for(int i = 0; i < count; i++) used[list[i]] = 1;
    int n = change > 0 ? change : -change, at = from;
    while(n-- > 0) {
        int p = at + dir;
        while(p >= STEP_EDIT_LOWEST_NOTE && p <= STEP_EDIT_HIGHEST_NOTE && used[p]) p += dir;
        if(p < STEP_EDIT_LOWEST_NOTE || p > STEP_EDIT_HIGHEST_NOTE) break;
        at = p;
    }
    return at;
}
static int step_edit_region_adjust(int change, int mode) {
    int start, length, i, slot, changed = 0, filter = sedits.region_filter;
    int target = -1, moved = 0;
    struct track_event event;
    sedits.edit_timeout = panel_menu_get_timeout();
    if(!change) {step_edit_update_display(); return 0;}
    step_edit_region(&start, &length);
    if(filter >= 0 && mode == STEP_EDIT_ADJUST_NOTE) {
        target = step_edit_region_free_pitch(filter, change);
        if(target == filter) {step_edit_update_display(); return 0;}
    }
    if(step_edit_prepare_undo() < 0) return 0;
    step_edit_stop_notes();
    for(i = 0; i < length; i++) {
        int step = (start + i) % SEQ_NUM_STEPS, notes = 0;
        for(slot = 0; slot < SEQ_TRACK_POLY; slot++) {
            if(song_get_step_event(sedits.scene, sedits.track, step, slot, &event) != 0 ||
                    event.type != SONG_EVENT_NOTE) continue;
            if(filter >= 0 && event.data0 != filter) continue;
            notes++;
            if(mode == STEP_EDIT_ADJUST_NOTE) {
                if(target >= 0) {event.data0 = target; moved = 1;}
                else event.data0 = seq_utils_clamp(event.data0 + change,
                    STEP_EDIT_LOWEST_NOTE, STEP_EDIT_HIGHEST_NOTE);
            }
            else if(mode == STEP_EDIT_ADJUST_VELO)
                event.data1 = seq_utils_clamp(event.data1 + change * 2, 0, 127);
            else if(mode == STEP_EDIT_ADJUST_GATE_TIME)
                event.length = seq_utils_clamp(event.length +
                    seq_utils_warp_change(event.length, change, 10),
                    STEP_EDIT_SHORTEST_NOTE, STEP_EDIT_LONGEST_NOTE);
            else if(mode == STEP_EDIT_ADJUST_START_DELAY && filter >= 0)
                event.delay = seq_utils_delay_adjust(event.delay, change, 1);
            else continue;
            song_set_step_event(sedits.scene, sedits.track, step, slot, &event);
            changed = 1;
        }
        if(mode == STEP_EDIT_ADJUST_START_DELAY && filter < 0 && notes) {
            int old = song_get_start_delay(sedits.scene, sedits.track, step);
            song_set_start_delay(sedits.scene, sedits.track, step, seq_utils_delay_adjust(old, change, 0));
            changed = 1;
        }
    }
    if(moved) sedits.region_filter = target;
    sedits.last_adjust_type = mode;
    step_edit_update_display();
    return changed;
}
// Pitches with notes in the region, ascending; returns the count.
static int step_edit_region_pitches(int *list) {
    int start, length, count = 0;
    unsigned char used[128] = {0};
    struct track_event event;
    step_edit_region(&start, &length);
    for(int i = 0; i < length; i++)
        for(int slot = 0; slot < SEQ_TRACK_POLY; slot++)
            if(song_get_step_event(sedits.scene, sedits.track, (start + i) % SEQ_NUM_STEPS,
                    slot, &event) == 0 && event.type == SONG_EVENT_NOTE &&
                    event.data0 >= 0 && event.data0 < 128) used[event.data0] = 1;
    for(int p = 0; p < 128; p++) if(used[p]) list[count++] = p;
    return count;
}
// Shift + Transpose in Region: All, then each pitch low to high; stops at ends.
static void step_edit_region_filter_step(int change) {
    int list[128], count = step_edit_region_pitches(list), pos = -1;
    sedits.edit_timeout = panel_menu_get_timeout();
    if(change > 0) change = 1; else if(change < 0) change = -1;
    if(sedits.region_filter >= 0) {
        // position of the filter, or just below the next higher pitch if gone
        int h = 0;
        while(h < count && list[h] < sedits.region_filter) h++;
        if(h < count && list[h] == sedits.region_filter) pos = h;
        else pos = change > 0 ? h - 1 : h;
    }
    pos = seq_utils_clamp(pos + change, -1, count - 1);
    sedits.region_filter = pos < 0 ? -1 : list[pos];
    step_edit_update_display();
}
static void step_edit_delay_str(char *s, int len, int delay) {
    if(seq_utils_delay_relative_label(delay)) snprintf(s, len, "%s", seq_utils_delay_relative_label(delay));
    else if(!delay) snprintf(s, len, "OFF");
    else panel_utils_gate_time_to_str(s, delay);
}
// DRUM name without padding (region summary line).
static void step_edit_drum_trim(char *s, int note) {
    int n;
    panel_utils_drum_name(s, note);
    n = strlen(s);
    while(n > 0 && s[n - 1] == ' ') s[--n] = 0;
}
// Filter label: DRUM name on DRUM tracks, note name otherwise.
static void step_edit_pitch_trim(char *s, int note) {
    int n;
    if(song_get_track_type(sedits.track) == SONG_TRACK_TYPE_DRUM) panel_utils_drum_name(s, note);
    else panel_utils_note_to_name(s, note, 1, 0);
    n = strlen(s);
    while(n > 0 && s[n - 1] == ' ') s[--n] = 0;
}
// Ranges over the region; no audition (playback is the preview).
static void step_edit_region_display(void) {
    int start, length, i, slot, notes = 0, steps = 0, filter = sedits.region_filter;
    int lo[4] = {999, 999, 99999, 999}, hi[4] = {-1, -1, -1, -1};
    char a[16], b[16], line[GFX_LABEL_LEN], what[8];
    struct track_event event;
    step_edit_region(&start, &length);
    // Filtered: per-note delay (Step = inherits the shared step delay).
    int dlo = 999, dhi = -1;
    for(i = 0; i < length; i++) {
        int step = (start + i) % SEQ_NUM_STEPS, has = 0;
        for(slot = 0; slot < SEQ_TRACK_POLY; slot++) {
            if(song_get_step_event(sedits.scene, sedits.track, step, slot, &event) != 0 ||
                    event.type != SONG_EVENT_NOTE) continue;
            if(filter >= 0 && event.data0 != filter) continue;
            int v[3] = {event.data0, event.data1, event.length};
            int nd = event.delay == SEQ_START_DELAY_INHERIT ? -1 : event.delay;
            if(nd < dlo) dlo = nd;
            if(nd > dhi) dhi = nd;
            for(int k = 0; k < 3; k++) {if(v[k] < lo[k]) lo[k] = v[k]; if(v[k] > hi[k]) hi[k] = v[k];}
            notes++; has = 1;
        }
        if(has) {
            int d = song_get_start_delay(sedits.scene, sedits.track, step);
            if(d < lo[3]) lo[3] = d;
            if(d > hi[3]) hi[3] = d;
            steps++;
        }
    }
    if(filter >= 0) step_edit_pitch_trim(what, filter);
    else strcpy(what, "notes");
    snprintf(line, sizeof(line), "%d %s on %d of %d steps", notes, what, steps, length);
    gui_set_status_text(1, line);
    if(!notes) {gui_set_status_text(2, filter >= 0 ? "(none) Shift+Note: filter" : "No notes to edit"); return;}
    if(song_get_track_type(sedits.track) == SONG_TRACK_TYPE_DRUM) {
        // DRUM: GM names plus numbers; drop numbers if the line is too long.
        step_edit_drum_trim(a, lo[0]); step_edit_drum_trim(b, hi[0]);
        snprintf(line, sizeof(line), "Note %s-%s %d-%d Vel %d-%d",
            a, b, lo[0], hi[0], lo[1], hi[1]);
        if(strlen(line) > 28)
            snprintf(line, sizeof(line), "Note %s-%s Vel %d-%d", a, b, lo[1], hi[1]);
    }
    else {
        panel_utils_note_to_name(a, lo[0], 1, 0); panel_utils_note_to_name(b, hi[0], 1, 0);
        snprintf(line, sizeof(line), "Note %s-%s Vel %d-%d", a, b, lo[1], hi[1]);
    }
    gui_set_status_text(2, line);
    // Compact layout shows lines 0-3 only: Gate and Dly share line 3.
    char g1[16], g2[16], label[8];
    panel_utils_gate_time_to_str(g1, lo[2]); panel_utils_gate_time_to_str(g2, hi[2]);
    if(filter >= 0) {
        strcpy(label, "NDly");
        if(dlo < 0) strcpy(a, "Step"); else step_edit_delay_str(a, sizeof(a), dlo);
        if(dhi < 0) strcpy(b, "Step"); else step_edit_delay_str(b, sizeof(b), dhi);
    }
    else {
        strcpy(label, "Dly");
        step_edit_delay_str(a, sizeof(a), lo[3]); step_edit_delay_str(b, sizeof(b), hi[3]);
    }
    snprintf(line, sizeof(line), "Gate %.6s-%.6s %s %.6s-%.6s", g1, g2, label, a, b);
    if(strlen(line) > 28)
        snprintf(line, sizeof(line), "G %.6s-%.6s %s %.6s-%.6s", g1, g2, label, a, b);
    if(strlen(line) > 28) line[28] = 0;
    gui_set_status_text(3, line);
}

static void step_edit_update_title(void) {
    char title[GFX_LABEL_LEN];
    int mask = step_edit_scene_mask();
    if(mask >= 0) {
        char scenes[7];
        for(int i=0;i<6;i++) scenes[i] = mask & (1u << i) ? '1'+i : '-';
        scenes[6] = 0;
        snprintf(title, sizeof(title), "Scn: %s Keys toggle", scenes);
        gui_set_status_text(0, title);
        return;
    }
    if(sedits.event_pos == STEP_EDIT_EVENT_POS_REGION) {
        int start, length;
        step_edit_region(&start, &length);
        int end = (start + length - 1) % SEQ_NUM_STEPS;
        if(sedits.region_filter >= 0) {
            char name[8];
            step_edit_pitch_trim(name, sedits.region_filter);
            snprintf(title, sizeof(title), "Region T%d %02d%c%02d [%s]", sedits.track + 1,
                start + 1, start + length > SEQ_NUM_STEPS ? '~' : '-', end + 1, name);
        }
        else snprintf(title, sizeof(title), "Region - Track: %d %02d%c%02d", sedits.track + 1,
            start + 1, start + length > SEQ_NUM_STEPS ? '~' : '-', end + 1);
        gui_set_status_text(0, title);
        return;
    }
    const char *name = step_edit_cci_name(sedits.cci_hint_controller);
    struct track_event event = {.delay = SEQ_START_DELAY_INHERIT};
    if(sedits.event_pos >= 0 && song_get_step_event(sedits.scene, sedits.track,
            sedits.step_pos, sedits.event_pos, &event) != -1 && event.type == SONG_EVENT_GLIDE) {
        if(sedits.last_adjust_type == STEP_EDIT_ADJUST_VELO)
            snprintf(title, sizeof(title), "Gli: Curve %d %s", event.data1,
                event.data1 == 64 ? "Linear" : event.data1 < 64 ? "Slow Start" : "Fast Start");
        else snprintf(title, sizeof(title), "Gli: Duration %s", glide_label(event.data0));
        gui_set_status_text(0, title);
        return;
    }
    if(sedits.event_pos >= 0 && song_get_step_event(sedits.scene, sedits.track,
            sedits.step_pos, sedits.event_pos, &event) != -1 && event.type == SONG_EVENT_INVERSION) {
        snprintf(title, sizeof(title), "Inv: %d Compact %s", event.data0, event.data1 ? "On" : "Off");
        gui_set_status_text(0, title);
        return;
    }
    if(sedits.event_pos >= 0 && song_get_step_event(sedits.scene, sedits.track,
            sedits.step_pos, sedits.event_pos, &event) != -1 && event.type == SONG_EVENT_HARMONY) {
        char scale[32];
        if(event.data0 == SONG_HARMONY_SCALE_KEEP) snprintf(scale, sizeof(scale), "Keep");
        else scale_type_to_name(scale, seq_utils_clamp((int)event.data0 - 1, 0, SCALE_NUM_TONALITIES - 1));
        snprintf(title, sizeof(title), "Hrm: %s / %s", scale,
            event.data1 == SONG_HARMONY_ROOT_KEEP ? "Keep" :
            scale_root_name((int)event.data1 - SONG_HARMONY_ROOT_C));
        gui_set_status_text(0, title);
        return;
    }
    if(sedits.cci_hint_timeout > 0 && name && sedits.cci_hint_show_value)
        step_edit_cci_value_title(title, sedits.cci_hint_controller, sedits.cci_hint_value);
    else if(sedits.cci_hint_timeout > 0 && name)
        snprintf(title, sizeof(title), "CCi %d: %s", sedits.cci_hint_controller, name);
    else
        snprintf(title, sizeof(title), "Edit - Track: %2d Step: %2d",
            sedits.track + 1, sedits.step_pos + 1);
    gui_set_status_text(0, title);
}

// update the display
void step_edit_update_display(void) {
    int i, xpos, temp;
    int expanded=sedits.enable && step_details_preference;
    int first=expanded ? 5 : 3;
    char tempstr[GFX_LABEL_LEN];
    struct track_event event = {.delay = SEQ_START_DELAY_INHERIT};
    gui_step_details_set(sedits.enable && step_details_preference);
    gui_clear_status_text_all();
    if(step_edit_lanes_active()) {step_edit_lanes_display(); return;}
    gui_lane_edit_set(0, 0, 0, 0, 0);
    step_edit_update_title();
    if(sedits.event_pos == STEP_EDIT_EVENT_POS_REGION) {
        step_edit_region_display();
        return;
    }

    // left title
    gui_set_status_text_part(1, 0, 3, "R  ");  // ratchet title
    sprintf(tempstr, "%d  ", song_get_ratchet_mode(sedits.scene,
        sedits.track, sedits.step_pos));
    gui_set_status_text_part(2, 0, 3, tempstr);  // ratchet setting
    switch(sedits.last_adjust_type) {
        case STEP_EDIT_ADJUST_NOTE:
        case STEP_EDIT_ADJUST_VELO:
            gui_set_status_text_part(3, 0, 2, "V");
            break;
        case STEP_EDIT_ADJUST_GATE_TIME:
            gui_set_status_text_part(3, 0, 2, "G");
            break;
        case STEP_EDIT_ADJUST_START_DELAY:
            gui_set_status_text_part(3, 0, 2, "D");
            break;
    }

    //
    // handle event pos highlight
    //
    // global event pos
    if(sedits.event_pos >= 0) {
        for(i = 0; i < SEQ_TRACK_POLY; i ++) {
            if(i == sedits.event_pos) {
                gui_set_status_highlight_part(2, first + (4 * i), 3,
                    GFX_HIGHLIGHT_INVERT);
                gui_set_status_highlight_part(3, first + (4 * i), 3,
                    GFX_HIGHLIGHT_INVERT);
            }
            else {
                gui_set_status_highlight_part(2, first + (4 * i), 3,
                    GFX_HIGHLIGHT_NORMAL);
                gui_set_status_highlight_part(3, first + (4 * i), 3,
                    GFX_HIGHLIGHT_NORMAL);
            }
        }
    }
    else {
        gui_set_status_highlight_part(2, first, 24, GFX_HIGHLIGHT_NORMAL);
        gui_set_status_highlight_part(3, first, 24, GFX_HIGHLIGHT_NORMAL);
    }

    xpos = first;
    //
    // display data for each event
    //
    for(i = 0; i < SEQ_TRACK_POLY; i ++) {
        // get event
        song_get_step_event(sedits.scene, sedits.track,
            sedits.step_pos, i, &event);

        // display the step
        switch(event.type) {
            case SONG_EVENT_NOTE:
                if(song_get_track_type(sedits.track) == SONG_TRACK_TYPE_DRUM) {
                    // DRUM: GM short name; expanded view adds the number above.
                    if(expanded) snprintf(tempstr, sizeof(tempstr), "%d", event.data0);
                    else strcpy(tempstr, "N");
                    gui_set_status_text_part(1, xpos, 4, tempstr);
                    panel_utils_drum_name(tempstr, event.data0);
                }
                else {
                    gui_set_status_text_part(1, xpos, 4, "N");
                    panel_utils_note_to_name(tempstr, event.data0, 1, 1);
                }
                gui_set_status_text_part(2, xpos, 4, tempstr);
                switch(sedits.last_adjust_type) {
                    case STEP_EDIT_ADJUST_NOTE:
                    case STEP_EDIT_ADJUST_VELO:
                        // display note
                        sprintf(tempstr, "%-3d", event.data1);
                        gui_set_status_text_part(3, xpos, 4, tempstr);
                        break;
                    case STEP_EDIT_ADJUST_GATE_TIME:
                        // display length
                        panel_utils_gate_time_to_str(tempstr, event.length);
                        gui_set_status_text_part(3, xpos, 4, tempstr);
                        break;
                    case STEP_EDIT_ADJUST_START_DELAY:
                        // display time
                        temp = sedits.event_pos == STEP_EDIT_EVENT_POS_ALL
                            ? song_get_start_delay(sedits.scene, sedits.track, sedits.step_pos)
                            : event.delay;
                        if(temp == SEQ_START_DELAY_INHERIT) {
                            sprintf(tempstr, "Step");
                        }
                        else if(seq_utils_delay_relative_label(temp)) {
                            sprintf(tempstr, "%s", seq_utils_delay_relative_label(temp));
                        }
                        else if(temp == 0) {
                            sprintf(tempstr, "OFF");
                        }
                        else {
                            panel_utils_gate_time_to_str(tempstr, temp);
                        }
                        gui_set_status_text_part(3, xpos, 4, tempstr);
                        break;
                }
                break;
            case SONG_EVENT_CC:
                gui_set_status_text_part(1, xpos, 4, "CC");
                sprintf(tempstr, "%-3d", event.data0);
                gui_set_status_text_part(2, xpos, 4, tempstr);
                sprintf(tempstr, "%-3d", event.data1);
                gui_set_status_text_part(3, xpos, 4, tempstr);
                break;
            case SONG_EVENT_INTERNAL_CC:
                gui_set_status_text_part(1, xpos, 4, "CCi");
                sprintf(tempstr, "%-3d", event.data0);
                gui_set_status_text_part(2, xpos, 4, tempstr);
                sprintf(tempstr, "%-3d", event.data1);
                gui_set_status_text_part(3, xpos, 4, tempstr);
                break;
            case SONG_EVENT_SCENE:
                gui_set_status_text_part(1, xpos, 4, "Scn");
                scene_mask_text(event.data0, tempstr);
                gui_set_status_text_part(2, xpos, 4, tempstr);
                gui_set_status_text_part(3, xpos, 4, "---");
                break;
            case SONG_EVENT_GLIDE:
                gui_set_status_text_part(1, xpos, 4, "Gli");
                gui_set_status_text_part(2, xpos, 4, event.data0 == 1 ? "128" : (char *)glide_label(event.data0));
                snprintf(tempstr, sizeof(tempstr), "%d", event.data1);
                gui_set_status_text_part(3, xpos, 4, tempstr);
                break;
            case SONG_EVENT_INVERSION:
                gui_set_status_text_part(1, xpos, 4, "Inv");
                sprintf(tempstr, "%d", event.data0);
                gui_set_status_text_part(2, xpos, 4, tempstr);
                gui_set_status_text_part(3, xpos, 4, event.data1 ? "On" : "Off");
                break;
            case SONG_EVENT_HARMONY:
                gui_set_status_text_part(1, xpos, 4, "Hrm");
                if(event.data0 == SONG_HARMONY_SCALE_KEEP) sprintf(tempstr, "Keep");
                else sprintf(tempstr, "%d", event.data0);
                gui_set_status_text_part(2, xpos, 4, tempstr);
                gui_set_status_text_part(3, xpos, 4, event.data1 == SONG_HARMONY_ROOT_KEEP
                    ? "Keep" : (char *)scale_root_name((int)event.data1 - SONG_HARMONY_ROOT_C));
                break;
            case SONG_EVENT_PROBABILITY:
                gui_set_status_text_part(1, xpos, 4, "Prb");
                sprintf(tempstr, "%d%%", event.data0);
                gui_set_status_text_part(2, xpos, 4, tempstr);
                gui_set_status_text_part(3, xpos, 4, "---");
                break;
            case SONG_EVENT_EUCLIDEAN: {
                unsigned a, b;
                conditional_pair(event.data0, &a, &b);
                gui_set_status_text_part(1, xpos, 4, "Euc");
                sprintf(tempstr, "%u:%u", a, b);
                gui_set_status_text_part(2, xpos, 4, tempstr);
                sprintf(tempstr, "%u", event.data1 % b);
                gui_set_status_text_part(3, xpos, 4, tempstr);
                break;
            }
            case SONG_EVENT_PRE:
            case SONG_EVENT_NON_PRE:
                gui_set_status_text_part(1, xpos, 4,
                    event.type == SONG_EVENT_PRE ? "Pre" : "!Pr");
                gui_set_status_text_part(2, xpos, 4, "---");
                gui_set_status_text_part(3, xpos, 4, "---");
                break;
            case SONG_EVENT_PROGCHG:
                gui_set_status_text_part(1, xpos, 4, "PC");
                sprintf(tempstr, "%-3d", event.data0 + 1);
                gui_set_status_text_part(2, xpos, 4, tempstr);
                gui_set_status_text_part(3, xpos, 4, "---");
                break;
            default:  // blank slot
                gui_set_status_text_part(1, xpos, 4, "   ");
                gui_set_status_text_part(2, xpos, 4, "---");
                gui_set_status_text_part(3, xpos, 4, "---");
                break;
        }
        xpos += 4;
    }
    if(sedits.enable && step_details_preference) {
        // Keep the existing type and control-value columns; add note-only rows.
        gui_set_status_text_part(1,0,5,"     ");
        gui_set_status_text_part(2,0,5,"Note ");
        gui_set_status_text_part(3,0,5,"Vel  ");
        gui_set_status_text_part(4,0,5,"Gate ");
        gui_set_status_text_part(5,0,5,"Dly  ");
        for(i=0;i<SEQ_TRACK_POLY;i++) {
            xpos=first+4*i;
            song_get_step_event(sedits.scene,sedits.track,sedits.step_pos,i,&event);
            gui_set_status_text_part(4,xpos,4,"---");
            gui_set_status_text_part(5,xpos,4,"---");
            if(event.type==SONG_EVENT_NOTE) {
                snprintf(tempstr,sizeof(tempstr),"%d",event.data1);
                gui_set_status_text_part(3,xpos,4,tempstr);
                panel_utils_gate_time_to_str(tempstr,event.length);
                gui_set_status_text_part(4,xpos,4,tempstr);
                temp=event.delay;
                if(temp==SEQ_START_DELAY_INHERIT) strcpy(tempstr,"Step");
                else if(seq_utils_delay_relative_label(temp))
                    snprintf(tempstr,sizeof(tempstr),"%s",seq_utils_delay_relative_label(temp));
                else if(!temp) strcpy(tempstr,"OFF");
                else panel_utils_gate_time_to_str(tempstr,temp);
                gui_set_status_text_part(5,xpos,4,tempstr);
            }
            for(int row=1;row<=5;row++)
                gui_set_status_highlight_part(row,xpos,4,
                    sedits.event_pos==i ? GFX_HIGHLIGHT_INVERT : GFX_HIGHLIGHT_NORMAL);
        }
        char delay[16];
        temp=song_get_start_delay(sedits.scene,sedits.track,sedits.step_pos);
        if(seq_utils_delay_relative_label(temp))
            snprintf(delay,sizeof(delay),"%s",seq_utils_delay_relative_label(temp));
        else if(!temp) strcpy(delay,"OFF");
        else panel_utils_gate_time_to_str(delay,temp);
        snprintf(tempstr,sizeof(tempstr),"Ratchet:%d Step delay:%s",
            song_get_ratchet_mode(sedits.scene,sedits.track,sedits.step_pos),delay);
        gui_set_status_text(6,tempstr);
    }
}

// play events on the current step
void step_edit_play_step(void) {
    struct track_event event = {.delay = SEQ_START_DELAY_INHERIT};
    int i;

    // don't play the step if we are running
    if(seq_ctrl_get_run_state()) {
        return;
    }

    // play each note on step
    for(i = 0; i < SEQ_TRACK_POLY; i ++) {
        // get the event and play the note
        if(song_get_step_event(sedits.scene, sedits.track,
                sedits.step_pos, i, &event) == -1) {
            continue;
        }

        // play the step
        switch(event.type) {
            case SONG_EVENT_NOTE:
                // stop the note slot if it's not free
                if(sedits.playing_notes[i] != STEP_EDIT_NOTE_SLOT_FREE) {
                    step_edit_send_note_off(sedits.playing_notes[i]);
                    sedits.playing_notes[i] = STEP_EDIT_NOTE_SLOT_FREE;
                    sedits.playing_note_timeouts[i] = 0;  // time out note
                }
                // store the note data
                sedits.playing_notes[i] = event.data0;
                sedits.playing_note_timeouts[i] = event.length;
                step_edit_send_note_on(event.data0, event.data1);
                break;
            case SONG_EVENT_CC:
                step_edit_send_cc(event.data0, event.data1);
                break;
            case SONG_EVENT_INTERNAL_CC:
                // Internal automation is only applied during running playback.
                break;
            case SONG_EVENT_PROGCHG:
                step_edit_send_program_change(event.data0);
                break;
            default:
                continue;
        }
    }
}

// stop the notes on the current step
void step_edit_stop_notes(void) {
    int i;
    // stop each note on step
    for(i = 0; i < SEQ_TRACK_POLY; i ++) {
        if(sedits.playing_notes[i] != STEP_EDIT_NOTE_SLOT_FREE) {
            step_edit_send_note_off(sedits.playing_notes[i]);
            sedits.playing_notes[i] = STEP_EDIT_NOTE_SLOT_FREE;
            sedits.playing_note_timeouts[i] = 0;  // time out note
        }
    }
}

// send a note on message
void step_edit_send_note_on(int note, int velocity) {
    struct midi_msg msg;
    midi_utils_enc_note_on(&msg, 0, 0, note, velocity);
    outproc_deliver_msg(sedits.scene, sedits.track, &msg,
        OUTPROC_DELIVER_BOTH, OUTPROC_OUTPUT_RAW);  // send note
}

// send a note off message
void step_edit_send_note_off(int note) {
    struct midi_msg msg;
    midi_utils_enc_note_off(&msg, 0, 0, note, 0x40);
    outproc_deliver_msg(sedits.scene, sedits.track, &msg,
        OUTPROC_DELIVER_BOTH, OUTPROC_OUTPUT_RAW);  // send note
}

// send a control change
void step_edit_send_cc(int controller, int value) {
    struct midi_msg msg;
    midi_utils_enc_control_change(&msg, 0, 0, controller, value);
    outproc_deliver_msg(sedits.scene, sedits.track, &msg,
        OUTPROC_DELIVER_BOTH, OUTPROC_OUTPUT_RAW);  // send CC
}

// send a program change
void step_edit_send_program_change(int program) {
    struct midi_msg msg;
    midi_utils_enc_program_change(&msg, 0, 0, program);
    outproc_deliver_msg(sedits.scene, sedits.track, &msg,
        OUTPROC_DELIVER_BOTH, OUTPROC_OUTPUT_RAW);  // send program change
}

// clear the recording notes
void step_edit_clear_recording_notes(void) {
    int i;
    for(i = 0; i < SEQ_TRACK_POLY; i ++) {
        sedits.recording_notes[i] = STEP_EDIT_NOTE_SLOT_FREE;
    }
}

// add a recording note
void step_edit_add_recording_note(int note) {
    int i;
    for(i = 0; i < SEQ_TRACK_POLY; i ++) {
        if(sedits.recording_notes[i] == STEP_EDIT_NOTE_SLOT_FREE) {
            sedits.recording_notes[i] = note;
            return;
        }
    }
}

// remove a recording note
void step_edit_remove_recording_note(int note) {
    int i;
    for(i = 0; i < SEQ_TRACK_POLY; i ++) {
        if(sedits.recording_notes[i] == note) {
            sedits.recording_notes[i] = STEP_EDIT_NOTE_SLOT_FREE;
        }
    }
}

// get the current number of recording notes
int step_edit_get_num_recording_notes(void) {
    int i, num = 0;
    for(i = 0; i < SEQ_TRACK_POLY; i ++) {
        if(sedits.recording_notes[i] != STEP_EDIT_NOTE_SLOT_FREE) {
            num ++;
        }
    }
    return num;
}

// checks whether the step contains an event
// returns -1 if the event is not found, or the slot number if found
int step_edit_does_step_contain_event(int type, int data0) {
    struct track_event event = {.delay = SEQ_START_DELAY_INHERIT};
    int i;
    // see if the event is already there
    for(i = 0; i < SEQ_TRACK_POLY; i ++) {
        // no data on step
        if(song_get_step_event(sedits.scene, sedits.track,
                sedits.step_pos, i, &event) == -1) {
            continue;
        }
        // event is found on a slot
        if(event.type == type && event.data0 == data0) {
            return i;
        }
    }
    return -1;
}
