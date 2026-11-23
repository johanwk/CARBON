/*
 * CARBON Sequencer GUI
 *
 * Written by: Andrew Kilpatrick
 * Copyright 2015: Kilpatrick Audio
 *
 * This file is part of CARBON.
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
#include "gui.h"
#include "step_edit.h"
#include "panel.h"
#include "panel_menu.h"
#include "../config.h"
#include "../config_store.h"
#include "../gfx.h"
#include "../midi/midi_clock.h"
#include "../seq/pattern.h"
#include "../seq/scale.h"
#include "../seq/seq_ctrl.h"
#include "../seq/seq_engine.h"
#include "../seq/song.h"
#include "../util/log.h"
#include "../util/panel_utils.h"
#include "../util/seq_utils.h"
#include "../util/state_change.h"
#include "../util/state_change_events.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//
// labels
//
#define GUI_NUM_STATUS_LINES 7
#define GUI_STATUS_LINE_LEN 28
#define GUI_MAX_LABELS 16  // set this to the total number of labels
#define GUI_LABEL_SONG 0  // song display
#define GUI_LABEL_SCENE 1  // scene number
#define GUI_LABEL_RUN 2  // run indicator
#define GUI_LABEL_REC 3  // rec indicator
#define GUI_LABEL_TEMPO 4  // tempo display
#define GUI_LABEL_LIVE 5  // live indicator
#define GUI_LABEL_SONG_MODE 6  // song mode indicator
#define GUI_LABEL_STATUS_L1 7  // status line 1
#define GUI_LABEL_STATUS_L2 8  // status line 2
#define GUI_LABEL_STATUS_L3 9  // status line 3
#define GUI_LABEL_STATUS_L4 10  // status line 4
#define GUI_LABEL_KEYTRANS 11  // keyboard transpose
#define GUI_LABEL_CLKSRC 12  // clock source

//
// colors
//
#define GUI_BG_COLOR 0xff000000
#define GUI_FONT_COLOR_NORMAL 0xffeeeeee
#define GUI_FONT_COLOR_NORMAL_DIM 0xff696969
#define GUI_FONT_COLOR_RED 0xffff0000
#define GUI_FONT_COLOR_RED_DIM 0xff990000
#define GUI_FONT_COLOR_GREEN 0xff00ff00
#define GUI_FONT_COLOR_GREEN_DIM 0xff669966
#define GUI_FONT_COLOR_YELLOW 0xffffff00
#define GUI_FONT_COLOR_YELLOW_DIM 0xff996600
#define GUI_FONT_COLOR_MAGENTA 0xffffff00
#define GUI_FONT_COLOR_MAGENTA_DIM 0xff999900
#define GUI_FONT_COLOR_CYAN 0xff00ffff
#define GUI_FONT_COLOR_CYAN_DIM 0xff009999
#define GUI_FONT_COLOR_GREY 0xff999999
#define GUI_FONT_COLOR_DARK_GREY 0xff666666
#define GUI_TEXT_BG_COLOR 0xff000000
#define GUI_MIDI_IND_BG_COLOR 0xff666666
#define GUI_FONT_COLOR_HIGHLIGHT_BG 0xff333333
// grid squares
#define GUI_GRID_BG_COLOR 0xff000000
#define GUI_TRACK_UNSELECT_COLOR 0xff333333
uint32_t GUI_GRID_TRACK_COLOR_ACTIVE[] = {
    0xffff0000,
    0xffffcc00,
    0xff99ff33,
    0xff00ffff,
    0xffffffff,
    0xffff33ff
};
uint32_t GUI_GRID_TRACK_COLOR_NORMAL[][2] = {
    {0xff696969, 0xff990000},
    {0xff696969, 0xff990000},
    {0xff696969, 0xff990000},
    {0xff696969, 0xff990000},
    {0xff696969, 0xff990000},
    {0xff696969, 0xff990000}
};
uint32_t GUI_GRID_TRACK_COLOR_MUTED[][2] = {
    {0xff222222, 0xff444444},
    {0xff222222, 0xff444444},
    {0xff222222, 0xff444444},
    {0xff222222, 0xff444444},
    {0xff222222, 0xff444444},
    {0xff222222, 0xff444444}
};
uint32_t GUI_GRID_TRACK_COLOR_OFF[][2] = {
    {0xff000000, 0xff222222},
    {0xff000000, 0xff222222},
    {0xff000000, 0xff222222},
    {0xff000000, 0xff222222},
    {0xff000000, 0xff222222},
    {0xff000000, 0xff222222}
};
// overlay colors
#define GUI_OVERLAY_COLOR_BLANK 0x00000000
#define GUI_OVERLAY_COLOR_LOW 0xff000060
#define GUI_OVERLAY_COLOR_MED 0xff0000c0
#define GUI_OVERLAY_COLOR_HIGH 0xffc0c000

//
// sizes / positions / fonts
//
// screen types
#define GUI_DISP_NUM_TYPES 2  // number of unique screen types
#define GUI_DISP_TYPE_A 0  // 320x480 3.95" display (original units)
#define GUI_DISP_TYPE_B 1  // 320x480 3.5" display (newer type)

// 320x480 3.95" display
// font indexes
#define GUI_DISP_A_FONT_NORMAL GFX_FONT_SMALLTEXT_8X10
#define GUI_DISP_A_FONT_HEADING GFX_FONT_SYSTEM_8X12
// grid settings
#define GUI_DISP_A_GRID_X 40
#define GUI_DISP_A_GRID_Y 90
#define GUI_DISP_A_GRID_W 238
#define GUI_DISP_A_GRID_H 238
#define GUI_DISP_A_GRID_SQUARE_X (GUI_DISP_A_GRID_X + GUI_DISP_A_GRID_SQUARE_SPACE) + 2
#define GUI_DISP_A_GRID_SQUARE_Y (GUI_DISP_A_GRID_Y + GUI_DISP_A_GRID_SQUARE_SPACE) + 2
#define GUI_DISP_A_GRID_SQUARE_W 27
#define GUI_DISP_A_GRID_SQUARE_H 27
#define GUI_DISP_A_GRID_START_SQUARE_W 3
#define GUI_DISP_A_GRID_TRACK_H 4
#define GUI_DISP_A_GRID_SQUARE_SPACE 2
#define GUI_DISP_A_PREVIEW_X 45
#define GUI_DISP_A_PREVIEW_Y 331
#define GUI_DISP_A_PREVIEW_SQUARE_W 4
#define GUI_DISP_A_PREVIEW_SQUARE_H 4
#define GUI_DISP_A_PREVIEW_GRID_SPACING 39
#define GUI_DISP_A_PREVIEW_SELECT_Y 366
#define GUI_DISP_A_PREVIEW_SELECT_H 4
#define GUI_DISP_A_PREVIEW_ARP_Y 372
// text positions
#define GUI_DISP_A_LABEL_SONG_X 43
#define GUI_DISP_A_LABEL_SONG_Y 50
#define GUI_DISP_A_LABEL_TEMPO_X 120
#define GUI_DISP_A_LABEL_TEMPO_Y 50
#define GUI_DISP_A_LABEL_SCENE_X 216
#define GUI_DISP_A_LABEL_SCENE_Y 50
#define GUI_DISP_A_LABEL_RUN_X 43
#define GUI_DISP_A_LABEL_RUN_Y 68
#define GUI_DISP_A_LABEL_REC_X 76
#define GUI_DISP_A_LABEL_REC_Y 68
#define GUI_DISP_A_LABEL_CLKSRC_X 120
#define GUI_DISP_A_LABEL_CLKSRC_Y 68
#define GUI_DISP_A_LABEL_KEYTRANS_X 170
#define GUI_DISP_A_LABEL_KEYTRANS_Y 68
#define GUI_DISP_A_LABEL_LIVE_X 240
#define GUI_DISP_A_LABEL_LIVE_Y 68
#define GUI_DISP_A_LABEL_SONG_MODE_X 43
#define GUI_DISP_A_LABEL_SONG_MODE_Y 80
#define GUI_DISP_A_LABEL_STATUS_L1_X 48
#define GUI_DISP_A_LABEL_STATUS_L1_Y 385
#define GUI_DISP_A_LABEL_STATUS_L2_X 48
#define GUI_DISP_A_LABEL_STATUS_L2_Y 397
#define GUI_DISP_A_LABEL_STATUS_L3_X 48
#define GUI_DISP_A_LABEL_STATUS_L3_Y 409
#define GUI_DISP_A_LABEL_STATUS_L4_X 48
#define GUI_DISP_A_LABEL_STATUS_L4_Y 421

// 320x48 3.5" display
// font indexes
#define GUI_DISP_B_FONT_NORMAL GFX_FONT_SMALLTEXT_8X10
#define GUI_DISP_B_FONT_HEADING GFX_FONT_SYSTEM_8X13
// grid settings
#define GUI_DISP_B_GRID_X 25
#define GUI_DISP_B_GRID_Y 80
#define GUI_DISP_B_GRID_W 238
#define GUI_DISP_B_GRID_H 238
#define GUI_DISP_B_GRID_SQUARE_X (GUI_DISP_B_GRID_X + GUI_DISP_B_GRID_SQUARE_SPACE) + 2
#define GUI_DISP_B_GRID_SQUARE_Y (GUI_DISP_B_GRID_Y + GUI_DISP_B_GRID_SQUARE_SPACE) + 2
#define GUI_DISP_B_GRID_SQUARE_W 31
#define GUI_DISP_B_GRID_SQUARE_H 31
#define GUI_DISP_B_GRID_START_SQUARE_W 3
#define GUI_DISP_B_GRID_TRACK_H 4
#define GUI_DISP_B_GRID_SQUARE_SPACE 2
#define GUI_DISP_B_PREVIEW_X 30
#define GUI_DISP_B_PREVIEW_Y 354
#define GUI_DISP_B_PREVIEW_SQUARE_W 5
#define GUI_DISP_B_PREVIEW_SQUARE_H 5
#define GUI_DISP_B_PREVIEW_GRID_SPACING 44
#define GUI_DISP_B_PREVIEW_SELECT_Y 397
#define GUI_DISP_B_PREVIEW_SELECT_H 5
#define GUI_DISP_B_PREVIEW_ARP_Y 404
// text positions
#define GUI_DISP_B_LABEL_SONG_X 28
#define GUI_DISP_B_LABEL_SONG_Y 35
#define GUI_DISP_B_LABEL_TEMPO_X 120
#define GUI_DISP_B_LABEL_TEMPO_Y 35
#define GUI_DISP_B_LABEL_SCENE_X 230
#define GUI_DISP_B_LABEL_SCENE_Y 35
#define GUI_DISP_B_LABEL_RUN_X 28
#define GUI_DISP_B_LABEL_RUN_Y 55
#define GUI_DISP_B_LABEL_REC_X 61
#define GUI_DISP_B_LABEL_REC_Y 55
#define GUI_DISP_B_LABEL_CLKSRC_X 115
#define GUI_DISP_B_LABEL_CLKSRC_Y 55
#define GUI_DISP_B_LABEL_KEYTRANS_X 185
#define GUI_DISP_B_LABEL_KEYTRANS_Y 55
#define GUI_DISP_B_LABEL_LIVE_X 260
#define GUI_DISP_B_LABEL_LIVE_Y 55
#define GUI_DISP_B_LABEL_SONG_MODE_X 28
#define GUI_DISP_B_LABEL_SONG_MODE_Y 68
#define GUI_DISP_B_LABEL_STATUS_L1_X 33
#define GUI_DISP_B_LABEL_STATUS_L1_Y 419
#define GUI_DISP_B_LABEL_STATUS_L2_X 33
#define GUI_DISP_B_LABEL_STATUS_L2_Y 431
#define GUI_DISP_B_LABEL_STATUS_L3_X 33
#define GUI_DISP_B_LABEL_STATUS_L3_Y 443
#define GUI_DISP_B_LABEL_STATUS_L4_X 33
#define GUI_DISP_B_LABEL_STATUS_L4_Y 455

//
// state
//
struct gui_state {
    // grid square colors
    uint32_t grid_state[SEQ_NUM_STEPS];  // colors of main grid
    struct seq_step_preview pitch_preview[SEQ_NUM_STEPS];
    uint16_t pitch_marks[SEQ_NUM_STEPS][SEQ_TRACK_POLY]; // cached pixel positions
    uint32_t grid_overlay[SEQ_NUM_STEPS];  // colors of the grid overlay
    int grid_overlay_enable;  // 0 = disable, 1 = enable
    uint32_t preview_state[SEQ_NUM_TRACKS][SEQ_NUM_STEPS];  // colors of preview grids
    uint32_t track_select_state[SEQ_NUM_TRACKS];  // colors of track select state
    uint32_t arp_enable_state[SEQ_NUM_TRACKS];  // colors of the arp enable state
    // screen type settings and position cache
    uint8_t screen_type;  // see lookup
    uint16_t GUI_GRID_X;
    uint16_t GUI_GRID_Y;
    uint16_t GUI_GRID_W;
    uint16_t GUI_GRID_H;
    uint16_t GUI_GRID_SQUARE_X;
    uint16_t GUI_GRID_SQUARE_Y;
    uint16_t GUI_GRID_SQUARE_W;
    uint16_t GUI_GRID_SQUARE_H;
    uint16_t GUI_GRID_SQUARE_SPACE;
    uint16_t GUI_PREVIEW_X;
    uint16_t GUI_PREVIEW_Y;
    uint16_t GUI_PREVIEW_SQUARE_W;
    uint16_t GUI_PREVIEW_SQUARE_H;
    uint16_t GUI_PREVIEW_GRID_SPACING;
    uint16_t GUI_PREVIEW_SELECT_Y;
    uint16_t GUI_PREVIEW_SELECT_H;
    uint16_t GUI_PREVIEW_ARP_Y;

    //
    // MIDI activity timeouts
    //
    uint8_t midi_act_timeouts[8];  // timeouts for showing MIDI activity
    //
    // current state cache from sequencer
    //
    // grid displays and track into (required per track)
    uint8_t motion_step[SEQ_NUM_TRACKS][SEQ_NUM_STEPS];  // step enables for each track
    uint8_t motion_start[SEQ_NUM_TRACKS];  // motion start pos for each track
    uint8_t motion_length[SEQ_NUM_TRACKS];  // motion length for each track
    uint8_t pattern_type[SEQ_NUM_TRACKS];  // pattern type for each track
    uint8_t track_select[SEQ_NUM_TRACKS];  // track select state for each track
    uint8_t arp_enable[SEQ_NUM_TRACKS];  // arp enable state for each track
    uint8_t track_mute[SEQ_NUM_TRACKS];  // track mute state for each track
    uint8_t active_step[SEQ_NUM_TRACKS];  // currently active step for each track
    // cached status for display
    uint8_t current_scene;  // the current scene
    uint8_t first_track;  // the first selected track from the sequencer
    // status display mode
    uint8_t step_details, step_details_drawn;
    uint8_t status_override;  // 1 = status is in use by function, 0 = status shows track info
    // labels and temp text
    struct gfx_label glabels[GUI_MAX_LABELS];
    char status_lines[GUI_NUM_STATUS_LINES][GFX_LABEL_LEN];  // temp strs that can be built up
    // Small cached text overlay, no framebuffer. Input changes only flags/cursor.
    volatile uint8_t clock_help, clock_help_selected;
    uint8_t clock_help_drawn, overview_track, overview_read_only_drawn;
    char clock_help_rows[18][30];
    // DRUM lane overview: page 0-3 (16 steps), follow playhead until a turn.
    uint8_t lane_page, lane_follow;
    uint16_t lane_cells[10][16];
    int8_t lane_playhead;
    // Tracker overview cache: row text and per-cell attributes.
    char trk_rows[21][30];
    uint8_t trk_attr[21][7];
    // Step Edit tracker view (VOICE; owned by step_edit.c)
    uint8_t trk_edit, trk_edit_drawn, trk_edit_track, trk_edit_step;
    int8_t trk_edit_slot;  // -1 = all slots
    // Step Edit lane view (editable; owned by step_edit.c)
    uint8_t lane_edit, lane_edit_drawn, lane_edit_track, lane_edit_step;
    int8_t lane_edit_lanes[10], lane_edit_cursor, lane_cursor_drawn_row, lane_cursor_drawn_col;
    struct gfx_label clock_help_label;
    // refresh stuff
    uint8_t force_refresh;  // 0 = no refresh, 1 = force refresh
    uint8_t force_reinit;  // 0 = no reinit, 1 = reinit
    uint8_t started;  // 0 = not started, 1 = started
};
// put data into CCMRAM instead of regular RAM
#ifndef GUI_STATE_SECTION
#define GUI_STATE_SECTION __attribute__ ((section (".ccm")))
#endif
struct gui_state gstate GUI_STATE_SECTION;

char tempstr[GFX_LABEL_LEN];

// local functions
void gui_handle_state_change(int event_type, int *data, int data_len);
void gui_draw_grid_bg(void);
int gui_draw_main_grid(void);
int gui_draw_preview_grids(void);
int gui_draw_labels(void);
void gui_set_label(int index, char *text);
void gui_set_label_highlight(int index, int startch, int lench, int mode);
void gui_set_label_prefs(int index, int x, int y, int w, int h,
    int font, uint32_t fg_color, uint32_t bg_color);
void gui_set_label_color(int index, uint32_t fg_color,
    uint32_t bg_color);
void gui_calculate_motion_steps(int track);
int gui_get_step_display_enable(int track, int step);
void gui_update_song(int song);
void gui_update_scene(int scene);
void gui_update_track_select(int track, int select);
void gui_update_first_track(int first);
void gui_update_tempo(float tempo);
void gui_update_run_enable(int enable);
void gui_update_rec_mode(int mode);
void gui_update_clock_source(int source);
void gui_update_live_mode(int mode);
void gui_update_keyboard_transpose(int transpose);
void gui_update_track_mute(int scene, int track, int mute);
void gui_update_bias_track(int scene, int track, int bias_track);
void gui_update_track_transpose(int scene, int track, int transpose);
void gui_update_arp_enable(int scene, int track, int enable);
void gui_update_tonality(int scene, int track, int tonality);
void gui_update_motion_start(int scene, int track, int pos);
void gui_update_motion_length(int scene, int track, int length);
void gui_update_motion_dir(int scene, int track, int rev);
void gui_update_step_length(int scene, int track, int len);
void gui_update_gate_time_override(int scene, int track, int time);
void gui_update_pattern_type(int scene, int track, int pattern);
void gui_update_track_type(int scene, int track, int type);
void gui_update_active_step(int track, int step);
void gui_update_song_mode(void);

// init the GUI and reset all vars
int gui_init(void) {
    int i, j;
    // .ccm is outside the startup-zeroed .bss and copied .data ranges.
    // Initialize every flag/cache explicitly before applying label defaults.
    memset(&gstate,0,sizeof(gstate));
	log_info("starting up GUI...");

#ifdef DEBUG_SIM
	// load fonts in simulation mode
	// (embedded mode will already have the fonts available)
	if(gfx_load_font(GUI_FONT_NORMAL, "LiberationMono-Regular.ttf", 12) != 0) {
	    return -1;
	}
	// load fonts in simulation mode
	// (embedded mode will already have the fonts available)
	if(gfx_load_font(GUI_FONT_HEADING, "LiberationMono-Regular.ttf", 14) != 0) {
	    return -1;
	}
#endif

    //
    // reset stuff
    //
    // reset all the labels
    for(i = 0; i < GUI_MAX_LABELS; i ++) {
        gstate.glabels[i].x = -1;
        gstate.glabels[i].y = -1;
        gstate.glabels[i].w = 10;
        gstate.glabels[i].h = 10;
        gstate.glabels[i].font = 0;
        gstate.glabels[i].fg_color = 0xffffffff;
        gstate.glabels[i].bg_color = 0xff111111;
        sprintf(gstate.glabels[i].text, " ");
        for(j = 0; j < GFX_LABEL_LEN; j ++) {
            gstate.glabels[i].highlight[j] = GFX_HIGHLIGHT_NORMAL;
        }
        gstate.glabels[i].dirty = 1;
    }

    // reset the main grid state
    for(i = 0; i < SEQ_NUM_STEPS; i ++) {
        gstate.grid_state[i] = 0;
        gstate.grid_overlay[i] = 0;
    }
    gstate.grid_overlay_enable = 0;
    gstate.lane_edit = gstate.lane_edit_drawn = 0;
    gstate.trk_edit = gstate.trk_edit_drawn = 0;

    // reset the preview grid states
    for(i = 0; i < SEQ_NUM_TRACKS; i ++) {
        gstate.track_select_state[i] = 0x00;
        gstate.arp_enable_state[i] = 0x00;
        for(j = 0; j < SEQ_NUM_STEPS; j ++) {
            gstate.preview_state[i][j] = 0x00;
        }
    }

    // reset the grid cache
    for(i = 0; i < SEQ_NUM_TRACKS; i ++) {
        gstate.motion_start[i] = 0;
        gstate.motion_length[i] = 0;
        gstate.pattern_type[i] = 0;
        gstate.active_step[i] = 0;
        gstate.track_select[i] = 0;
        gstate.arp_enable[i] = 0;
        gstate.track_mute[i] = 0;
        for(j = 0; j < SEQ_NUM_STEPS; j ++) {
            gstate.motion_step[i][j] = 0;
        }
    }

    gstate.first_track = 0;
    gstate.current_scene = 0;

    // make sure we don't start drawing yet
    gstate.started = 0;
    return 0;
}

// close the GUI
void gui_close(void) {
	gfx_close();
}

// start the GUI and load all layout params - config store must be loaded
void gui_startup(void) {
    int i;

    // figure out the screen type
    gstate.screen_type = config_store_get_val(CONFIG_STORE_GUI_DISP_TYPE);
    if(gstate.screen_type >= GUI_DISP_NUM_TYPES) {
        gstate.screen_type = 0;  // reset if invalid
        config_store_set_val(CONFIG_STORE_GUI_DISP_TYPE, gstate.screen_type);
    }

    // init stuff for screen type A
    if(gstate.screen_type == GUI_DISP_TYPE_A) {
        // cache some drawing stuff
        gstate.GUI_GRID_X = GUI_DISP_A_GRID_X;
        gstate.GUI_GRID_Y = GUI_DISP_A_GRID_Y;
        gstate.GUI_GRID_W = GUI_DISP_A_GRID_W;
        gstate.GUI_GRID_H = GUI_DISP_A_GRID_H;
        gstate.GUI_GRID_SQUARE_X = GUI_DISP_A_GRID_SQUARE_X;
        gstate.GUI_GRID_SQUARE_Y = GUI_DISP_A_GRID_SQUARE_Y;
        gstate.GUI_GRID_SQUARE_W = GUI_DISP_A_GRID_SQUARE_W;
        gstate.GUI_GRID_SQUARE_H = GUI_DISP_A_GRID_SQUARE_H;
        gstate.GUI_GRID_SQUARE_SPACE = GUI_DISP_A_GRID_SQUARE_SPACE;
        gstate.GUI_PREVIEW_X = GUI_DISP_A_PREVIEW_X;
        gstate.GUI_PREVIEW_Y = GUI_DISP_A_PREVIEW_Y;
        gstate.GUI_PREVIEW_SQUARE_W = GUI_DISP_A_PREVIEW_SQUARE_W;
        gstate.GUI_PREVIEW_SQUARE_H = GUI_DISP_A_PREVIEW_SQUARE_H;
        gstate.GUI_PREVIEW_GRID_SPACING = GUI_DISP_A_PREVIEW_GRID_SPACING;
        gstate.GUI_PREVIEW_SELECT_Y = GUI_DISP_A_PREVIEW_SELECT_Y;
        gstate.GUI_PREVIEW_SELECT_H = GUI_DISP_A_PREVIEW_SELECT_H;
        gstate.GUI_PREVIEW_ARP_Y = GUI_DISP_A_PREVIEW_ARP_Y;

        //
        // set the prefs for each label
        //
        gui_set_label_prefs(GUI_LABEL_SONG,
            GUI_DISP_A_LABEL_SONG_X, GUI_DISP_A_LABEL_SONG_Y,
            60, 12, GUI_DISP_A_FONT_HEADING, GUI_FONT_COLOR_NORMAL, GUI_TEXT_BG_COLOR);

        gui_set_label_prefs(GUI_LABEL_TEMPO,
            GUI_DISP_A_LABEL_TEMPO_X, GUI_DISP_A_LABEL_TEMPO_Y,
            80, 12, GUI_DISP_A_FONT_HEADING, GUI_FONT_COLOR_NORMAL, GUI_TEXT_BG_COLOR);

        gui_set_label_prefs(GUI_LABEL_SCENE,
            GUI_DISP_A_LABEL_SCENE_X, GUI_DISP_A_LABEL_SCENE_Y,
            60, 12, GUI_DISP_A_FONT_HEADING, GUI_FONT_COLOR_NORMAL, GUI_TEXT_BG_COLOR);

        gui_set_label_prefs(GUI_LABEL_RUN,
            GUI_DISP_A_LABEL_RUN_X, GUI_DISP_A_LABEL_RUN_Y,
            20, 12, GUI_DISP_A_FONT_NORMAL, GUI_FONT_COLOR_GREEN_DIM, GUI_TEXT_BG_COLOR);
        gui_set_label(GUI_LABEL_RUN, "RUN");

        gui_set_label_prefs(GUI_LABEL_REC,
            GUI_DISP_A_LABEL_REC_X, GUI_DISP_A_LABEL_REC_Y,
            20, 12, GUI_DISP_A_FONT_NORMAL, GUI_FONT_COLOR_RED_DIM, GUI_TEXT_BG_COLOR);
        gui_set_label(GUI_LABEL_REC, "REC");

        gui_set_label_prefs(GUI_LABEL_CLKSRC,
            GUI_DISP_A_LABEL_CLKSRC_X, GUI_DISP_A_LABEL_CLKSRC_Y,
            20, 12, GUI_DISP_A_FONT_NORMAL, GUI_FONT_COLOR_GREY, GUI_TEXT_BG_COLOR);
        gui_set_label(GUI_LABEL_CLKSRC, "INT");

        gui_set_label_prefs(GUI_LABEL_KEYTRANS,
            GUI_DISP_A_LABEL_KEYTRANS_X, GUI_DISP_A_LABEL_KEYTRANS_Y,
            60, 12, GUI_DISP_A_FONT_NORMAL, GUI_FONT_COLOR_MAGENTA_DIM, GUI_TEXT_BG_COLOR);
        gui_update_keyboard_transpose(0);

        gui_set_label_prefs(GUI_LABEL_LIVE,
            GUI_DISP_A_LABEL_LIVE_X, GUI_DISP_A_LABEL_LIVE_Y,
            30, 12, GUI_DISP_A_FONT_NORMAL, GUI_FONT_COLOR_CYAN_DIM, GUI_TEXT_BG_COLOR);
        gui_set_label(GUI_LABEL_LIVE, "LIVE");

        gui_set_label_prefs(GUI_LABEL_SONG_MODE,
            GUI_DISP_A_LABEL_SONG_MODE_X, GUI_DISP_A_LABEL_SONG_MODE_Y,
            224, 12, GUI_DISP_A_FONT_NORMAL, GUI_FONT_COLOR_YELLOW_DIM, GUI_TEXT_BG_COLOR);
        gui_set_label(GUI_LABEL_SONG_MODE, "SONG MODE");

        gui_set_label_prefs(GUI_LABEL_STATUS_L1,
            GUI_DISP_A_LABEL_STATUS_L1_X, GUI_DISP_A_LABEL_STATUS_L1_Y,
            224, 10, GUI_DISP_A_FONT_NORMAL, GUI_FONT_COLOR_NORMAL, GUI_TEXT_BG_COLOR);

        gui_set_label_prefs(GUI_LABEL_STATUS_L2,
            GUI_DISP_A_LABEL_STATUS_L2_X, GUI_DISP_A_LABEL_STATUS_L2_Y,
            224, 10, GUI_DISP_A_FONT_NORMAL, GUI_FONT_COLOR_NORMAL, GUI_TEXT_BG_COLOR);

        gui_set_label_prefs(GUI_LABEL_STATUS_L3,
            GUI_DISP_A_LABEL_STATUS_L3_X, GUI_DISP_A_LABEL_STATUS_L3_Y,
            224, 10, GUI_DISP_A_FONT_NORMAL, GUI_FONT_COLOR_NORMAL, GUI_TEXT_BG_COLOR);

        gui_set_label_prefs(GUI_LABEL_STATUS_L4,
            GUI_DISP_A_LABEL_STATUS_L4_X, GUI_DISP_A_LABEL_STATUS_L4_Y,
            224, 10, GUI_DISP_A_FONT_NORMAL, GUI_FONT_COLOR_NORMAL, GUI_TEXT_BG_COLOR);
    }
    // init stuff for screen type B
    else if(gstate.screen_type == GUI_DISP_TYPE_B) {
        // cache some drawing stuff
        gstate.GUI_GRID_X = GUI_DISP_B_GRID_X;
        gstate.GUI_GRID_Y = GUI_DISP_B_GRID_Y;
        gstate.GUI_GRID_W = GUI_DISP_B_GRID_W;
        gstate.GUI_GRID_H = GUI_DISP_B_GRID_H;
        gstate.GUI_GRID_SQUARE_X = GUI_DISP_B_GRID_SQUARE_X;
        gstate.GUI_GRID_SQUARE_Y = GUI_DISP_B_GRID_SQUARE_Y;
        gstate.GUI_GRID_SQUARE_W = GUI_DISP_B_GRID_SQUARE_W;
        gstate.GUI_GRID_SQUARE_H = GUI_DISP_B_GRID_SQUARE_H;
        gstate.GUI_GRID_SQUARE_SPACE = GUI_DISP_B_GRID_SQUARE_SPACE;
        gstate.GUI_PREVIEW_X = GUI_DISP_B_PREVIEW_X;
        gstate.GUI_PREVIEW_Y = GUI_DISP_B_PREVIEW_Y;
        gstate.GUI_PREVIEW_SQUARE_W = GUI_DISP_B_PREVIEW_SQUARE_W;
        gstate.GUI_PREVIEW_SQUARE_H = GUI_DISP_B_PREVIEW_SQUARE_H;
        gstate.GUI_PREVIEW_GRID_SPACING = GUI_DISP_B_PREVIEW_GRID_SPACING;
        gstate.GUI_PREVIEW_SELECT_Y = GUI_DISP_B_PREVIEW_SELECT_Y;
        gstate.GUI_PREVIEW_SELECT_H = GUI_DISP_B_PREVIEW_SELECT_H;
        gstate.GUI_PREVIEW_ARP_Y = GUI_DISP_B_PREVIEW_ARP_Y;

        //
        // set the prefs for each label
        //
        gui_set_label_prefs(GUI_LABEL_SONG,
            GUI_DISP_B_LABEL_SONG_X, GUI_DISP_B_LABEL_SONG_Y,
            60, 12, GUI_DISP_B_FONT_HEADING, GUI_FONT_COLOR_NORMAL, GUI_TEXT_BG_COLOR);

        gui_set_label_prefs(GUI_LABEL_TEMPO,
            GUI_DISP_B_LABEL_TEMPO_X, GUI_DISP_B_LABEL_TEMPO_Y,
            80, 12, GUI_DISP_B_FONT_HEADING, GUI_FONT_COLOR_NORMAL, GUI_TEXT_BG_COLOR);

        gui_set_label_prefs(GUI_LABEL_SCENE,
            GUI_DISP_B_LABEL_SCENE_X, GUI_DISP_B_LABEL_SCENE_Y,
            60, 12, GUI_DISP_B_FONT_HEADING, GUI_FONT_COLOR_NORMAL, GUI_TEXT_BG_COLOR);

        gui_set_label_prefs(GUI_LABEL_RUN,
            GUI_DISP_B_LABEL_RUN_X, GUI_DISP_B_LABEL_RUN_Y,
            20, 12, GUI_DISP_B_FONT_NORMAL, GUI_FONT_COLOR_GREEN_DIM, GUI_TEXT_BG_COLOR);
        gui_set_label(GUI_LABEL_RUN, "RUN");

        gui_set_label_prefs(GUI_LABEL_REC,
            GUI_DISP_B_LABEL_REC_X, GUI_DISP_B_LABEL_REC_Y,
            20, 12, GUI_DISP_B_FONT_NORMAL, GUI_FONT_COLOR_RED_DIM, GUI_TEXT_BG_COLOR);
        gui_set_label(GUI_LABEL_REC, "REC");

        gui_set_label_prefs(GUI_LABEL_CLKSRC,
            GUI_DISP_B_LABEL_CLKSRC_X, GUI_DISP_B_LABEL_CLKSRC_Y,
            20, 12, GUI_DISP_B_FONT_NORMAL, GUI_FONT_COLOR_GREY, GUI_TEXT_BG_COLOR);
        gui_set_label(GUI_LABEL_CLKSRC, "INT");

        gui_set_label_prefs(GUI_LABEL_KEYTRANS,
            GUI_DISP_B_LABEL_KEYTRANS_X, GUI_DISP_B_LABEL_KEYTRANS_Y,
            60, 12, GUI_DISP_B_FONT_NORMAL, GUI_FONT_COLOR_MAGENTA_DIM, GUI_TEXT_BG_COLOR);
        gui_update_keyboard_transpose(0);

        gui_set_label_prefs(GUI_LABEL_LIVE,
            GUI_DISP_B_LABEL_LIVE_X, GUI_DISP_B_LABEL_LIVE_Y,
            30, 12, GUI_DISP_B_FONT_NORMAL, GUI_FONT_COLOR_CYAN_DIM, GUI_TEXT_BG_COLOR);
        gui_set_label(GUI_LABEL_LIVE, "LIVE");

        gui_set_label_prefs(GUI_LABEL_SONG_MODE,
            GUI_DISP_B_LABEL_SONG_MODE_X, GUI_DISP_B_LABEL_SONG_MODE_Y,
            224, 12, GUI_DISP_B_FONT_NORMAL, GUI_FONT_COLOR_YELLOW_DIM, GUI_TEXT_BG_COLOR);
        gui_set_label(GUI_LABEL_SONG_MODE, "SONG MODE");

        gui_set_label_prefs(GUI_LABEL_STATUS_L1,
            GUI_DISP_B_LABEL_STATUS_L1_X, GUI_DISP_B_LABEL_STATUS_L1_Y,
            224, 10, GUI_DISP_B_FONT_NORMAL, GUI_FONT_COLOR_NORMAL, GUI_TEXT_BG_COLOR);

        gui_set_label_prefs(GUI_LABEL_STATUS_L2,
            GUI_DISP_B_LABEL_STATUS_L2_X, GUI_DISP_B_LABEL_STATUS_L2_Y,
            224, 10, GUI_DISP_B_FONT_NORMAL, GUI_FONT_COLOR_NORMAL, GUI_TEXT_BG_COLOR);

        gui_set_label_prefs(GUI_LABEL_STATUS_L3,
            GUI_DISP_B_LABEL_STATUS_L3_X, GUI_DISP_B_LABEL_STATUS_L3_Y,
            224, 10, GUI_DISP_B_FONT_NORMAL, GUI_FONT_COLOR_NORMAL, GUI_TEXT_BG_COLOR);

        gui_set_label_prefs(GUI_LABEL_STATUS_L4,
            GUI_DISP_B_LABEL_STATUS_L4_X, GUI_DISP_B_LABEL_STATUS_L4_Y,
            224, 10, GUI_DISP_B_FONT_NORMAL, GUI_FONT_COLOR_NORMAL, GUI_TEXT_BG_COLOR);
    }

	// draw display for the first time
	gfx_clear_screen(GUI_BG_COLOR);
	gui_draw_grid_bg();
	// mark components dirty
    for(i = 0; i < GUI_MAX_LABELS; i ++) {
        gstate.glabels[i].dirty = 1;
    }
    for(i = 0; i < SEQ_NUM_STEPS; i ++) {
        gstate.grid_state[i] = 0;
    }
    gui_set_status_override(0);
    gstate.force_refresh = 0;
    gstate.force_reinit = 0;

    // register for events
    state_change_register(gui_handle_state_change, SCEC_SONG);
    state_change_register(gui_handle_state_change, SCEC_CTRL);
    state_change_register(gui_handle_state_change, SCEC_ENG);

    // Re-establish the lower layout after startup/reinitialization.
    gstate.step_details_drawn = 255;
    // unblock the drawing
    gstate.started = 1;
}

int gui_clock_help_active(void) { return gstate.clock_help; }
// Tracker: open on the playhead page while that scene runs, else the
// motion start page of the reference track; follow until a page turn.
static void gui_tracker_open(int mode,int index) {
    gstate.overview_track=index;
    int song=mode==GUI_OVERVIEW_SONG;
    int scene=song ? index : seq_ctrl_get_scene();
    int track=song ? seq_utils_clamp(seq_ctrl_get_first_track(),0,SEQ_NUM_TRACKS-1) : index;
    int step=seq_ctrl_get_run_state() && scene==seq_engine_get_current_scene() ?
        gstate.active_step[track] : song_get_motion_start(scene,track);
    gstate.lane_page=seq_utils_clamp(step,0,SEQ_NUM_STEPS-1)/16;
    gstate.lane_follow=1;
    gstate.clock_help=mode;
}
int gui_overview_mode(void) {return gstate.clock_help;}
void gui_track_overview_open(int track) {
    if(track<0 || track>=SEQ_NUM_TRACKS) return;
    gstate.overview_track=track;
    if(song_get_track_type(track)==SONG_TRACK_TYPE_DRUM) {
        // Open on the playhead page while running, else the motion start page.
        int step=seq_ctrl_get_run_state() ? gstate.active_step[track] :
            song_get_motion_start(seq_ctrl_get_scene(),track);
        gstate.lane_page=seq_utils_clamp(step,0,SEQ_NUM_STEPS-1)/16;
        gstate.lane_follow=1;
        gstate.clock_help=GUI_OVERVIEW_LANES;
        return;
    }
    gui_tracker_open(GUI_OVERVIEW_TRACKER,track);
}
// Track views follow the selected track (page/follow kept); lanes for
// DRUM, tracker for VOICE; the Settings page stays Settings.
static void gui_overview_follow_track(int track) {
    int mode=gstate.clock_help;
    if(track<0 || track>=SEQ_NUM_TRACKS || track==gstate.overview_track) return;
    if(mode!=GUI_OVERVIEW_TRACK && mode!=GUI_OVERVIEW_LANES && mode!=GUI_OVERVIEW_TRACKER) return;
    gstate.overview_track=track;
    if(mode!=GUI_OVERVIEW_TRACK)
        gstate.clock_help=song_get_track_type(track)==SONG_TRACK_TYPE_DRUM ?
            GUI_OVERVIEW_LANES : GUI_OVERVIEW_TRACKER;
    gstate.clock_help_drawn=0;  // full redraw
}
void gui_overview_page(int change) {
    int mode=gstate.clock_help,index=gstate.overview_track,grid,last;
    if(!change || index>=SEQ_NUM_TRACKS) return;
    // pages 0-3 = lanes/tracker, 4 = Settings (the stored overview)
    if(mode==GUI_OVERVIEW_SCENE || mode==GUI_OVERVIEW_SONG) {
        grid=GUI_OVERVIEW_SONG;last=GUI_OVERVIEW_SCENE;
    } else if(mode==GUI_OVERVIEW_TRACK || mode==GUI_OVERVIEW_LANES ||
            mode==GUI_OVERVIEW_TRACKER) {
        grid=song_get_track_type(index)==SONG_TRACK_TYPE_DRUM ?
            GUI_OVERVIEW_LANES : GUI_OVERVIEW_TRACKER;
        last=GUI_OVERVIEW_TRACK;
    } else return;
    int page=mode==last ? 4 : gstate.lane_page;
    page=seq_utils_clamp(page+change,0,4);
    gstate.lane_follow=0;
    if(page==4) gstate.clock_help=last;
    else {gstate.lane_page=page;gstate.clock_help=grid;}
}
void gui_scene_overview_open(int scene) {
    if(scene<0 || scene>=SEQ_NUM_SCENES) return;
    // pinned index, independent of engine scene changes
    gui_tracker_open(GUI_OVERVIEW_SONG,scene);
}
static int gui_overview_read_only(void) {
    return gstate.clock_help==GUI_OVERVIEW_TRACK || gstate.clock_help==GUI_OVERVIEW_SCENE ||
        gstate.clock_help==GUI_OVERVIEW_LANES || gstate.clock_help==GUI_OVERVIEW_TRACKER ||
        gstate.clock_help==GUI_OVERVIEW_SONG;
}
void gui_overview_open(int mode) {
    if(!panel_menu_overview_count(mode)) return;
    gstate.clock_help_selected=0;gstate.clock_help=mode;
}
void gui_overview_edit(int change) {
    if(gui_overview_read_only()) return;
    panel_menu_overview_edit(gstate.clock_help,gstate.clock_help_selected,change);
}
void gui_clock_help_set(int enable) {
    if(enable && !gstate.clock_help) gstate.clock_help_selected = 0;
    gstate.clock_help = enable ? PANEL_MENU_CLOCK : 0;
}
void gui_clock_help_scroll(int change) {
    if(gui_overview_read_only()) return;
    gstate.clock_help_selected = seq_utils_clamp(gstate.clock_help_selected + change,
        0, panel_menu_overview_count(gstate.clock_help) - 1);
}

static void gui_overview_range(int scene,int track,int number,int selected,char text[30]) {
        int start=seq_utils_clamp(song_get_motion_start(scene,track),0,63);
        int length=seq_utils_clamp(song_get_motion_length(scene,track),1,64);
        int end=(start+length-1)%64;
        int bias=song_get_bias_track(scene,track);
        char rate[16],source[4]="-";
        panel_utils_step_len_to_str(rate,song_get_step_length(scene,track));
        if(bias>=0 && bias<SEQ_NUM_TRACKS) snprintf(source,sizeof(source),"T%d",bias+1);
        snprintf(text,30,"%c%c %02u%c%02u %-5.5s %-4s %s",
            selected?'*':' ','0'+seq_utils_clamp(number,1,6),
            (unsigned)start+1,start+length>64?'~':'-',(unsigned)end+1,
            rate,source,song_get_mute(scene,track)?"On":"Off");
}

// Stored scene settings only: never substitute live CCi/Hrm or queued edits.
static void gui_track_overview_row(int track,int row,char text[30]) {
    if(row==0) {
        snprintf(text,30,"TRACK %d / %s / stored",track+1,
            song_get_track_type(track)==SONG_TRACK_TYPE_DRUM ? "DRUM" : "VOICE");
        return;
    }
    if(row==1) {strcpy(text,"SC RANGE RATE  BIAS MUTE");return;}
    int scene=(row-2)/2;
    if(!(row&1)) {
        gui_overview_range(scene,track,scene+1,scene==seq_ctrl_get_scene(),text);
    } else {
        char scale[32],root[12];
        scale_type_to_name(scale,seq_utils_clamp(song_get_tonality(scene,track),0,SCALE_NUM_TONALITIES-1));
        int scene_root=song_get_scene_root(scene);
        if(scene_root==SONG_ROOT_INHERIT)
            snprintf(root,sizeof(root),"Song(%s)",scale_root_name(song_get_root()));
        else snprintf(root,sizeof(root),"%s",scene_root==SONG_ROOT_OFF?"Root Off":scale_root_name(scene_root));
        snprintf(text,30,"   %.9s %.15s",root,scale);
    }
}

static void gui_scene_overview_row(int scene,int row,char text[30]) {
    if(row==0) {
        char root[12];
        int value=song_get_scene_root(scene);
        if(value==SONG_ROOT_INHERIT)
            snprintf(root,sizeof(root),"Song(%s)",scale_root_name(song_get_root()));
        else snprintf(root,sizeof(root),"%s",value==SONG_ROOT_OFF?"Root Off":scale_root_name(value));
        snprintf(text,30,"SCENE %c / %.9s / stored",'1'+seq_utils_clamp(scene,0,5),root);
        return;
    }
    if(row==1) {strcpy(text,"TR RANGE RATE  BIAS MUTE");return;}
    int track=(row-2)/2;
    if(!(row&1)) {
        gui_overview_range(scene,track,track+1,seq_ctrl_get_track_select(track),text);
    } else {
        char scale[32];
        scale_type_to_name(scale,seq_utils_clamp(song_get_tonality(scene,track),0,SCALE_NUM_TONALITIES-1));
        snprintf(text,30,"   %-5s %.15s",
            song_get_track_type(track)==SONG_TRACK_TYPE_DRUM?"DRUM":"VOICE",scale);
    }
}

// ---- DRUM lane overview (read-only) ----
#define GUI_LANES_MAX 10
#define GUI_LANE_NAME_W 34
#define GUI_LANE_PITCH 12   // step column pitch (11 px cell + 1 px gap)
#define GUI_LANE_H 18
#define GUI_LANE_TOP 28
#define GUI_LANE_GHOST 40   // velocity below this: small square
// Pitches shown as lanes, ascending (index 0 = bottom row). At most 10, by
// hit count; the GM kick (36, else 35) is always kept. Returns hidden count.
#define GUI_LANE_TAKE_MAX 64
static struct {
    int count;
    uint8_t pitch[GUI_LANE_TAKE_MAX],step[GUI_LANE_TAKE_MAX],vel[GUI_LANE_TAKE_MAX];
    uint8_t played[128];
} gui_take;
static void gui_lanes_take(int track) {
    gui_take.count=seq_engine_record_preview(track,gui_take.pitch,gui_take.step,
        gui_take.vel,GUI_LANE_TAKE_MAX);
    memset(gui_take.played,0,sizeof(gui_take.played));
    for(int i=0;i<gui_take.count;i++) gui_take.played[gui_take.pitch[i]]=1;
}
static int gui_lanes_pick(int track,int *lanes,int *count) {
    uint16_t hits[128]={0};
    struct track_event event;
    int used=0;
    // Pitches in the pending take get a lane at once (ranked above others).
    for(int i=0;i<gui_take.count;i++) {
        if(!hits[gui_take.pitch[i]]) used++;
        hits[gui_take.pitch[i]]=1000;
    }
    for(int step=0;step<SEQ_NUM_STEPS;step++)
        for(int slot=0;slot<SEQ_TRACK_POLY;slot++)
            if(song_get_step_event(seq_ctrl_get_scene(),track,step,slot,&event)==0 &&
                    event.type==SONG_EVENT_NOTE && event.data0<128) {
                if(!hits[event.data0]) used++;
                if(hits[event.data0]<1000) hits[event.data0]++;
            }
    int n=0;
    unsigned char take[128]={0};
    int kick=hits[36] ? 36 : hits[35] ? 35 : -1;
    if(kick>=0) {take[kick]=1;n++;}
    while(n<GUI_LANES_MAX && n<used) {
        int best=-1;
        for(int p=0;p<128;p++) if(hits[p] && !take[p] && (best<0 || hits[p]>hits[best])) best=p;
        take[best]=1;n++;
    }
    *count=0;
    for(int p=0;p<128;p++) if(take[p]) lanes[(*count)++]=p;
    return used-*count;
}
// Cell code: bits 0-2 level 1-4 (0 = no hit), 3 ghost, 4 condition on the
// step, 5 note has its own delay, 6 step inside the playing region,
// 7 pending take (white). Played lanes: stored hits shown at level 1 (the
// DRUM overdub replaces them at the end of the pass).
static uint8_t gui_lane_cell(int track,int step,int pitch,int in_region) {
    struct track_event event;
    int vel=-1,delay=0,cond=0;
    for(int i=0;i<gui_take.count;i++)
        if(gui_take.pitch[i]==pitch && gui_take.step[i]==step && gui_take.vel[i]>vel) vel=gui_take.vel[i];
    if(vel>=0) {
        int level=vel<70 ? 2 : vel<100 ? 3 : 4;
        return (in_region ? 0x40 : 0)|0x80|level|(vel<GUI_LANE_GHOST ? 0x08 : 0);
    }
    for(int slot=0;slot<SEQ_TRACK_POLY;slot++) {
        if(song_get_step_event(seq_ctrl_get_scene(),track,step,slot,&event)!=0) continue;
        if(event.type==SONG_EVENT_PROBABILITY || event.type==SONG_EVENT_EUCLIDEAN ||
                event.type==SONG_EVENT_PRE || event.type==SONG_EVENT_NON_PRE ||
                event.type==SONG_EVENT_SCENE) cond=1;
        if(event.type==SONG_EVENT_NOTE && event.data0==pitch && event.data1>vel) {
            vel=event.data1;
            delay=event.delay!=SEQ_START_DELAY_INHERIT;
        }
    }
    uint8_t code=in_region ? 0x40 : 0;
    if(vel<0) return code;
    int level=vel<70 ? 2 : vel<100 ? 3 : 4;
    if(pitch>=0 && pitch<128 && gui_take.played[pitch]) level=1;
    return code|level|(vel<GUI_LANE_GHOST ? 0x08 : 0)|(cond ? 0x10 : 0)|(delay ? 0x20 : 0);
}
static uint32_t gui_lane_color(int track,int level,int in_region,int take) {
    uint32_t c=take ? 0xffffffff : GUI_GRID_TRACK_COLOR_ACTIVE[seq_utils_clamp(track,0,5)];
    int pct=level*25-(in_region ? 0 : 50);
    if(pct<15) pct=15;
    uint32_t r=((c>>16)&255)*pct/100,g=((c>>8)&255)*pct/100,b=(c&255)*pct/100;
    return 0xff000000|(r<<16)|(g<<8)|b;
}
static void gui_lane_text(int row,int x,int y,const char *text,uint32_t color) {
    if(!strcmp(text,gstate.clock_help_rows[row])) return;
    snprintf(gstate.clock_help_rows[row],sizeof(gstate.clock_help_rows[row]),"%s",text);
    struct gfx_label *label=&gstate.clock_help_label;
    memset(label,0,sizeof(*label));
    label->x=x;label->y=y;label->w=(row==0 ? 232 : GUI_LANE_NAME_W-2);label->h=10;
    label->font=GFX_FONT_SMALLTEXT_8X10;label->fg_color=color;label->bg_color=GUI_TEXT_BG_COLOR;
    strcpy(label->text,text);
    gfx_fill_rect(label->x,label->y,label->w,label->h,label->bg_color);
    gfx_draw_string(label);
}
void gui_lane_edit_set(int on, int track, const int *lanes, int cursor_lane, int cursor_step) {
    if(!on) {gstate.lane_edit=0;return;}
    gstate.lane_edit_track=seq_utils_clamp(track,0,SEQ_NUM_TRACKS-1);
    for(int i=0;i<GUI_LANES_MAX;i++) gstate.lane_edit_lanes[i]=lanes ? lanes[i] : -1;
    gstate.lane_edit_cursor=seq_utils_clamp(cursor_lane,0,GUI_LANES_MAX-1);
    gstate.lane_edit_step=cursor_step&(SEQ_NUM_STEPS-1);
    gstate.lane_edit=1;
}
static int gui_draw_lanes(int full) {
    int edit=gstate.lane_edit && !gstate.clock_help;
    int track=edit ? gstate.lane_edit_track : gstate.overview_track,lanes[GUI_LANES_MAX],count,dirty=0;
    int x0=gstate.GUI_GRID_X,y0=gstate.GUI_GRID_Y;
    int start=seq_utils_clamp(song_get_motion_start(seq_ctrl_get_scene(),track),0,63);
    int length=seq_utils_clamp(song_get_motion_length(seq_ctrl_get_scene(),track),1,64);
    int running=seq_ctrl_get_run_state();
    int active=gstate.active_step[seq_utils_clamp(track,0,5)];
    if(gstate.lane_follow && running && !edit) gstate.lane_page=(active&63)/16;
    gui_lanes_take(track);
    int page=gstate.lane_page&3,hidden=0;
    if(edit) {
        // Fixed rows while editing (no reordering); empty rows have no pitch.
        page=gstate.lane_edit_step/16;
        count=GUI_LANES_MAX;
        for(int i=0;i<GUI_LANES_MAX;i++) lanes[i]=gstate.lane_edit_lanes[i];
    }
    else hidden=gui_lanes_pick(track,lanes,&count);
    // Cursor moved: force both old and new cells to redraw.
    int crow=edit ? GUI_LANES_MAX-1-gstate.lane_edit_cursor : -1;
    int ccol=edit ? (gstate.lane_edit_step&15) : -1;
    if(crow!=gstate.lane_cursor_drawn_row || ccol!=gstate.lane_cursor_drawn_col) {
        if(gstate.lane_cursor_drawn_row>=0 && gstate.lane_cursor_drawn_row<GUI_LANES_MAX &&
                gstate.lane_cursor_drawn_col>=0 && gstate.lane_cursor_drawn_col<16)
            gstate.lane_cells[gstate.lane_cursor_drawn_row][gstate.lane_cursor_drawn_col]=0xffff;
        if(crow>=0) gstate.lane_cells[crow][ccol]=0xffff;
        gstate.lane_cursor_drawn_row=crow;gstate.lane_cursor_drawn_col=ccol;
    }
    if(full) {
        memset(gstate.lane_cells,0xff,sizeof(gstate.lane_cells));
        gstate.lane_playhead=-2;
        // step numbers 1, 5, 9, 13 of the page (static per page: in row 1 text)
    }
    char text[30],other[12]="";
    if(hidden>0) snprintf(other,sizeof(other)," +%d other",hidden);
    if(edit) snprintf(text,sizeof(text),"EDIT T%d %02d-%02d p%d/4",track+1,
        page*16+1,page*16+16,page+1);
    else snprintf(text,sizeof(text),"T%d %02d-%02d p%d/4%s%s",track+1,page*16+1,page*16+16,
        page+1,seq_ctrl_get_record_mode()==SEQ_CTRL_RECORD_RT ? " REC" :
        gstate.lane_follow && running ? " play" : "",other);
    if(strcmp(text,gstate.clock_help_rows[0])) dirty=1;
    gui_lane_text(0,x0+2,y0+2,text,GUI_FONT_COLOR_NORMAL);
    // Step numbers: one label per beat, cached as a page number in row 1.
    snprintf(text,sizeof(text),"%d",page);
    if(strcmp(text,gstate.clock_help_rows[1])) {
        strcpy(gstate.clock_help_rows[1],text);
        gfx_fill_rect(x0,y0+14,gstate.GUI_GRID_W,12,GUI_TEXT_BG_COLOR);
        for(int beat=0;beat<4;beat++) {
            struct gfx_label *label=&gstate.clock_help_label;
            memset(label,0,sizeof(*label));
            label->x=x0+GUI_LANE_NAME_W+beat*4*GUI_LANE_PITCH;label->y=y0+15;
            label->w=24;label->h=10;label->font=GFX_FONT_SMALLTEXT_8X10;
            label->fg_color=GUI_FONT_COLOR_GREY;label->bg_color=GUI_TEXT_BG_COLOR;
            snprintf(label->text,sizeof(label->text),"%d",page*16+beat*4+1);
            gfx_draw_string(label);
        }
        dirty=1;
    }
    // Black only where a hit still would not play: the Euclidean mask or a
    // fixed pattern. "As recorded" (at density 100%) enables any step that
    // holds an event, so empty steps there are not shown as masked.
    uint8_t mask_on[16];
    int pattern=seq_engine_get_effective_pattern_type(track);
    int euclid=seq_ctrl_get_scene()==seq_engine_get_current_scene() ?
        seq_engine_get_effective_euclidean_density(track) :
        song_get_euclidean_density(seq_ctrl_get_scene(),track);
    for(int col=0;col<16;col++)
        mask_on[col]=(pattern==PATTERN_AS_RECORDED && euclid>=SEQ_EUCLIDEAN_DENSITY_MAX) ? 1 :
            pattern_get_step_enable(seq_ctrl_get_scene(),track,pattern,page*16+col);
    for(int row=0;row<GUI_LANES_MAX;row++) {
        int lane=GUI_LANES_MAX-1-row; // bottom row = lowest pitch
        int ly=y0+GUI_LANE_TOP+row*GUI_LANE_H;
        int pitch=lane<count ? lanes[lane] : -1;
        char name[8]="";
        if(pitch>=0) {
            panel_utils_drum_name(name,pitch);
            int n=strlen(name);
            while(n>0 && name[n-1]==' ') name[--n]=0;
        }
        else if(edit) strcpy(name,"--");  // empty row: Transpose picks a drum
        if(strcmp(name,gstate.clock_help_rows[2+row])) dirty=1;
        gui_lane_text(2+row,x0+2,ly+4,name,GUI_FONT_COLOR_NORMAL);
        for(int col=0;col<16;col++) {
            int step=page*16+col;
            int in_region=((step-start+SEQ_NUM_STEPS)%SEQ_NUM_STEPS)<length;
            // Masked by Pattern/Euclidean (same check as playback): black
            // background, hits kept visible at the lowest level. Bit 8.
            int masked=in_region && !mask_on[col];
            uint16_t code=pitch>=0 ? gui_lane_cell(track,step,pitch,in_region) : (in_region ? 0x40 : 0);
            if(masked) {
                code|=0x100;
                if(code&7) code=(code&~7)|1;
            }
            if(code==gstate.lane_cells[row][col]) continue;
            gstate.lane_cells[row][col]=code;dirty=1;
            int cx=x0+GUI_LANE_NAME_W+col*GUI_LANE_PITCH;
            gfx_fill_rect(cx,ly,GUI_LANE_PITCH-1,GUI_LANE_H-1,
                masked ? 0xff000000 : in_region ? 0xff1c1c1c : 0xff080808);
            if(row==crow && col==ccol) {  // cursor: yellow frame (Step Edit marker colour)
                gfx_fill_rect(cx,ly,GUI_LANE_PITCH-1,1,GUI_OVERLAY_COLOR_HIGH);
                gfx_fill_rect(cx,ly+GUI_LANE_H-2,GUI_LANE_PITCH-1,1,GUI_OVERLAY_COLOR_HIGH);
                gfx_fill_rect(cx,ly,1,GUI_LANE_H-1,GUI_OVERLAY_COLOR_HIGH);
                gfx_fill_rect(cx+GUI_LANE_PITCH-2,ly,1,GUI_LANE_H-1,GUI_OVERLAY_COLOR_HIGH);
            }
            int level=code&7;
            if(!level) continue;
            uint32_t color=gui_lane_color(track,level,in_region && !masked,code&0x80);
            if(code&0x08) gfx_fill_rect(cx+3,ly+6,5,5,color);           // ghost
            else if(code&0x10) {                                           // conditional: outline
                gfx_fill_rect(cx+1,ly+4,9,1,color);gfx_fill_rect(cx+1,ly+12,9,1,color);
                gfx_fill_rect(cx+1,ly+4,1,9,color);gfx_fill_rect(cx+9,ly+4,1,9,color);
            }
            else gfx_fill_rect(cx+1,ly+4,9,9,color);
            if(code&0x20) gfx_fill_rect(cx+9,ly+14,2,2,GUI_FONT_COLOR_NORMAL); // own delay
        }
    }
    // Playhead bar under the grid (only when its step is on this page).
    int head=(active&63)/16==page ? (active&15) : -1;
    if(head!=gstate.lane_playhead) {
        int by=y0+GUI_LANE_TOP+GUI_LANES_MAX*GUI_LANE_H+1;
        gfx_fill_rect(x0+GUI_LANE_NAME_W,by,16*GUI_LANE_PITCH,3,GUI_TEXT_BG_COLOR);
        if(head>=0) gfx_fill_rect(x0+GUI_LANE_NAME_W+head*GUI_LANE_PITCH,by,GUI_LANE_PITCH-1,3,
            GUI_GRID_TRACK_COLOR_ACTIVE[seq_utils_clamp(track,0,5)]);
        gstate.lane_playhead=head;dirty=1;
    }
    return dirty;
}

// ---- Tracker overview (read-only) ----
// SONG: 6 tracks of one scene (first note per step). TRACKER: one track,
// 6 slots. Rows: title, header, 16 steps, blank, 2 footer lines. Cells
// are 3 characters + flag; velocity sets brightness, region dims; dark grey
// background = step enabled, black = disabled (region/Pattern/Euclid).
#define GUI_TRK_ROW_H 11
#define GUI_TRK_NUM_W 24
#define GUI_TRK_CELL_W 32
#define GUI_TRK_EMPTY 0xff3a3a3a
#define GUI_TRK_HELD 0xff707070
#define GUI_TRK_HEAD_BG 0xff484848   // playhead cell
#define GUI_TRK_ON_BG 0xff1c1c1c     // enabled step (drum lane colour)
static int gui_trk_is_cond(int type) {
    return type==SONG_EVENT_PROBABILITY || type==SONG_EVENT_EUCLIDEAN ||
        type==SONG_EVENT_PRE || type==SONG_EVENT_NON_PRE || type==SONG_EVENT_SCENE;
}
static void gui_trk_note(char out[4],int track,int note) {
    char name[12];
    const char *drum=song_get_track_type(track)==SONG_TRACK_TYPE_DRUM ?
        panel_utils_drum_short(note) : 0;
    if(drum) snprintf(name,sizeof(name),"%s",drum);
    else panel_utils_note_to_name(name,note,1,1);
    snprintf(out,4,"%-3.3s",name);
}
// A note from an earlier step (same slot, or any slot if slot<0) still sounds.
static int gui_trk_held(int scene,int track,int step,int slot) {
    int ticks=seq_utils_step_len_to_ticks(song_get_step_length(scene,track));
    if(ticks<=0) return 0;
    struct track_event e;
    for(int back=1;back<=16;back++) {
        int prev=(step-back+SEQ_NUM_STEPS)%SEQ_NUM_STEPS;
        for(int i=0;i<SEQ_TRACK_POLY;i++) {
            if(slot>=0 && i!=slot) continue;
            if(song_get_step_event(scene,track,prev,i,&e)!=0 || e.type!=SONG_EVENT_NOTE) continue;
            if(e.length>back*ticks) return 1;
            if(slot>=0) return 0;  // a new note in this slot ends the search
        }
    }
    return 0;
}
static int gui_trk_level(int vel) {return vel<40 ? 1 : vel<70 ? 2 : vel<100 ? 3 : 4;}
// One cell: text (4 chars) and attribute (bits 0-2 level, 3 empty, 4 held).
static uint8_t gui_trk_cell(int scene,int track,int step,int slot,char out[5]) {
    struct track_event e,first={0};
    int notes=0,cond=0,glide=0,harm=0,cc=0,delay=0,found=-1,other=-1;
    for(int i=0;i<SEQ_TRACK_POLY;i++) {
        if(song_get_step_event(scene,track,step,i,&e)!=0) continue;
        if(slot>=0 && i!=slot) {
            if(gui_trk_is_cond(e.type)) cond=1;
            continue;
        }
        if(e.type==SONG_EVENT_NOTE) {
            if(!notes++) {first=e;found=i;}
            if(e.delay!=SEQ_START_DELAY_INHERIT) delay=1;
        }
        else if(gui_trk_is_cond(e.type)) cond=1;
        else if(e.type==SONG_EVENT_GLIDE) glide=1;
        else if(e.type==SONG_EVENT_HARMONY || e.type==SONG_EVENT_INVERSION) harm=1;
        else if(e.type==SONG_EVENT_CC || e.type==SONG_EVENT_PROGCHG ||
                e.type==SONG_EVENT_INTERNAL_CC) cc=1;
        if(other<0 && e.type!=SONG_EVENT_NOTE) {other=i;if(!notes) first=e;}
    }
    char name[4]="...",flag=' ';
    uint8_t attr=0x08;
    if(notes) {gui_trk_note(name,track,first.data0);attr=gui_trk_level(first.data1);}
    else if(slot>=0 && other>=0) {
        // Track view: non-note slots get a short type name.
        switch(first.type) {
            case SONG_EVENT_CC: snprintf(name,4,"c%02d",first.data0%100);break;
            case SONG_EVENT_INTERNAL_CC: snprintf(name,4,"i%02d",first.data0%100);break;
            case SONG_EVENT_PROGCHG: snprintf(name,4,"p%02d",first.data0%100);break;
            case SONG_EVENT_PROBABILITY: snprintf(name,4,"%%%02d",first.data0%100);break;
            case SONG_EVENT_EUCLIDEAN: strcpy(name,"Euc");break;
            case SONG_EVENT_PRE: strcpy(name,"Pre");break;
            case SONG_EVENT_NON_PRE: strcpy(name,"!Pr");break;
            case SONG_EVENT_SCENE: strcpy(name,"Scn");break;
            case SONG_EVENT_HARMONY: strcpy(name,"Hrm");break;
            case SONG_EVENT_INVERSION: strcpy(name,"Inv");break;
            case SONG_EVENT_GLIDE: strcpy(name,"Gli");break;
            default: strcpy(name,"???");break;
        }
        attr=3;cc=glide=harm=0;
    }
    else if(gui_trk_held(scene,track,step,slot)) {strcpy(name,"|||");attr=0x10;}
    if(notes>1) flag='+';
    else if(notes && song_get_ratchet_mode(scene,track,step)>1) flag='"';
    else if(cond && (notes || slot<0)) flag='?';
    else if(glide) flag='~';
    else if(harm) flag='h';
    else if(cc) flag='c';
    else if(delay) flag='\'';
    if(found<0 && !(attr&7) && flag!=' ' && slot<0) attr=1;  // events, no note
    snprintf(out,5,"%s%c",name,flag);
    return attr;
}
static int gui_trk_harmony_at(int scene,int song,int index,int step) {
    struct track_event e;
    for(int t=song?0:index;t<(song?SEQ_NUM_TRACKS:index+1);t++)
        for(int i=0;i<SEQ_TRACK_POLY;i++)
            if(song_get_step_event(scene,t,step,i,&e)==0 && e.type==SONG_EVENT_HARMONY) return 1;
    return 0;
}
static void gui_trk_label(int x,int y,int w,const char *text,uint32_t fg,uint32_t bg) {
    struct gfx_label *label=&gstate.clock_help_label;
    memset(label,0,sizeof(*label));
    label->x=x;label->y=y;label->w=w;label->h=10;
    label->font=GFX_FONT_SMALLTEXT_8X10;label->fg_color=fg;label->bg_color=bg;
    snprintf(label->text,sizeof(label->text),"%s",text);
    gfx_fill_rect(x,y,w,10,bg);
    gfx_draw_string(label);
}
// Draw a text row only when its text or attributes changed.
static int gui_trk_row(int row,const char *text,const uint8_t attr[7],uint32_t fg) {
    static const uint8_t none[7]={0};
    if(!attr) attr=none;
    if(!strcmp(text,gstate.trk_rows[row]) && !memcmp(attr,gstate.trk_attr[row],7)) return 0;
    snprintf(gstate.trk_rows[row],sizeof(gstate.trk_rows[row]),"%s",text);
    memcpy(gstate.trk_attr[row],attr,7);
    gui_trk_label(gstate.GUI_GRID_X+2,gstate.GUI_GRID_Y+2+row*GUI_TRK_ROW_H,232,text,fg,GUI_TEXT_BG_COLOR);
    return 1;
}
void gui_trk_edit_set(int on,int track,int step,int slot) {
    if(!on) {gstate.trk_edit=0;return;}
    gstate.trk_edit_track=seq_utils_clamp(track,0,SEQ_NUM_TRACKS-1);
    gstate.trk_edit_step=step&(SEQ_NUM_STEPS-1);
    gstate.trk_edit_slot=seq_utils_clamp(slot,-1,SEQ_TRACK_POLY-1);
    gstate.trk_edit=1;
}
static int gui_draw_tracker(int full) {
    int edit=gstate.trk_edit && !gstate.clock_help;
    int song=!edit && gstate.clock_help==GUI_OVERVIEW_SONG,dirty=0;
    int index=edit ? gstate.trk_edit_track : gstate.overview_track;
    int scene=song ? index : seq_ctrl_get_scene();
    int ref=song ? seq_utils_clamp(seq_ctrl_get_first_track(),0,SEQ_NUM_TRACKS-1) : index;
    int live=seq_ctrl_get_run_state() && scene==seq_engine_get_current_scene();
    if(edit) gstate.lane_page=gstate.trk_edit_step/16;  // page follows the cursor
    else if(gstate.lane_follow && live) gstate.lane_page=(gstate.active_step[ref]&63)/16;
    int page=gstate.lane_page&3;
    int x0=gstate.GUI_GRID_X+2,y0=gstate.GUI_GRID_Y+2;
    if(full) {memset(gstate.trk_rows,0,sizeof(gstate.trk_rows));memset(gstate.trk_attr,0xff,sizeof(gstate.trk_attr));}
    char text[30];
    snprintf(text,sizeof(text),"%s%s%c %02u-%02u p%c/4%s",edit ? "EDIT " : "",song ? "SCENE " : "T",
        '1'+seq_utils_clamp(song ? scene : index,0,5),(unsigned)page*16+1,(unsigned)page*16+16,
        '1'+page,!edit && gstate.lane_follow && live ? " play" : "");
    dirty|=gui_trk_row(0,text,0,GUI_FONT_COLOR_NORMAL);
    // Header: selected tracks bright (SONG), slot numbers (TRACKER).
    {
        uint8_t attr[7]={0};
        strcpy(text," # ");
        for(int c=0;c<6;c++) {
            char col[6];snprintf(col,sizeof(col)," %c%d ",song?'T':'s',c+1);strcat(text,col);
            attr[c]=song && seq_ctrl_get_track_select(c);
        }
        if(strcmp(text,gstate.trk_rows[1]) || memcmp(attr,gstate.trk_attr[1],7)) {
            strcpy(gstate.trk_rows[1],text);memcpy(gstate.trk_attr[1],attr,7);
            int y=y0+GUI_TRK_ROW_H;
            gui_trk_label(x0,y,GUI_TRK_NUM_W," # ",GUI_FONT_COLOR_GREY,GUI_TEXT_BG_COLOR);
            for(int c=0;c<6;c++) {
                char col[6];snprintf(col,sizeof(col)," %c%d ",song?'T':'s',c+1);
                gui_trk_label(x0+GUI_TRK_NUM_W+c*GUI_TRK_CELL_W,y,GUI_TRK_CELL_W,col,
                    attr[c] ? GUI_FONT_COLOR_NORMAL : GUI_FONT_COLOR_GREY,
                    attr[c] ? GUI_FONT_COLOR_HIGHLIGHT_BG : GUI_TEXT_BG_COLOR);
            }
            dirty=1;
        }
    }
    // Enabled = in region and not masked by Pattern/Euclid (lane rule).
    uint8_t enabled[6][16];
    for(int c=0;c<6;c++) {
        int track=song ? c : index;
        int start=seq_utils_clamp(song_get_motion_start(scene,track),0,63);
        int length=seq_utils_clamp(song_get_motion_length(scene,track),1,64);
        int pattern=seq_engine_get_effective_pattern_type(track);
        int euclid=scene==seq_engine_get_current_scene() ?
            seq_engine_get_effective_euclidean_density(track) :
            song_get_euclidean_density(scene,track);
        for(int r=0;r<16;r++) {
            int step=page*16+r;
            int in_region=((step-start+SEQ_NUM_STEPS)%SEQ_NUM_STEPS)<length;
            enabled[c][r]=in_region && ((pattern==PATTERN_AS_RECORDED && euclid>=SEQ_EUCLIDEAN_DENSITY_MAX) ||
                pattern_get_step_enable(scene,track,pattern,step));
        }
        if(!song) break;
    }
    for(int r=0;r<16;r++) {
        int step=page*16+r,row=2+r;
        char cells[6][5];uint8_t attr[7];
        int gutter=gui_trk_harmony_at(scene,song,index,step);
        // attr[6]: bit 0 Hrm gutter, bits 1-3 cursor column+1 (7 = all slots)
        int cursor=edit && step==gstate.trk_edit_step ?
            (gstate.trk_edit_slot<0 ? 7 : gstate.trk_edit_slot+1) : 0;
        attr[6]=gutter|(cursor<<1);
        for(int c=0;c<6;c++) {
            int track=song ? c : index;
            int start=seq_utils_clamp(song_get_motion_start(scene,track),0,63);
            int length=seq_utils_clamp(song_get_motion_length(scene,track),1,64);
            int in_region=((step-start+SEQ_NUM_STEPS)%SEQ_NUM_STEPS)<length;
            attr[c]=gui_trk_cell(scene,track,step,song?-1:c,cells[c]);
            if(in_region) attr[c]|=0x20;
            if(enabled[song?c:0][r]) attr[c]|=0x80;
            if(live && gstate.active_step[track]==step) attr[c]|=0x40;
        }
        snprintf(text,sizeof(text),"%02d%c%s%s%s%s%s%s",(step+1)%100,gutter?'*':' ',
            cells[0],cells[1],cells[2],cells[3],cells[4],cells[5]);
        if(!strcmp(text,gstate.trk_rows[row]) && !memcmp(attr,gstate.trk_attr[row],7)) continue;
        strcpy(gstate.trk_rows[row],text);memcpy(gstate.trk_attr[row],attr,7);
        int y=y0+row*GUI_TRK_ROW_H;
        char num[4];snprintf(num,sizeof(num),"%02d%c",(step+1)%100,gutter?'*':' ');
        gui_trk_label(x0,y,GUI_TRK_NUM_W,num,(step&3) ? GUI_FONT_COLOR_GREY : GUI_FONT_COLOR_NORMAL,
            GUI_TEXT_BG_COLOR);
        for(int c=0;c<6;c++) {
            int track=song ? c : index,level=attr[c]&7,in_region=!!(attr[c]&0x20);
            uint32_t fg=attr[c]&0x08 ? GUI_TRK_EMPTY : attr[c]&0x10 ? GUI_TRK_HELD :
                gui_lane_color(track,level,in_region,0);
            if(!in_region && (attr[c]&0x18)) fg=0xff262626;
            uint32_t bg=attr[c]&0x40 ? GUI_TRK_HEAD_BG : attr[c]&0x80 ? GUI_TRK_ON_BG : GUI_TEXT_BG_COLOR;
            if(cursor==7 || cursor==c+1) {bg=GUI_OVERLAY_COLOR_HIGH;fg=GUI_TEXT_BG_COLOR;}  // edit cursor
            gui_trk_label(x0+GUI_TRK_NUM_W+c*GUI_TRK_CELL_W,y,GUI_TRK_CELL_W-1,cells[c],fg,bg);
        }
        dirty=1;
    }
    // Footer: output and region of the reference track (harmony is in the
    // main status lines below the overlay).
    {
        char chan[16];
        int start=seq_utils_clamp(song_get_motion_start(scene,ref),0,63);
        int length=seq_utils_clamp(song_get_motion_length(scene,ref),1,64);
        panel_utils_channel_str(chan,song_get_midi_port_map(ref,0),song_get_midi_channel_map(ref,0));
        snprintf(text,sizeof(text),"T%c %.8s Rgn %02u%c%02u",'1'+ref,chan,(unsigned)start+1,
            start+length>64?'~':'-',(unsigned)(start+length-1)%64+1);
        dirty|=gui_trk_row(19,text,0,GUI_FONT_COLOR_GREY);
    }
    return dirty;
}

static int gui_draw_clock_help(void) {
    int mode=gstate.clock_help, track_view=gui_overview_read_only();
    if(mode==GUI_OVERVIEW_LANES) track_view=2;
    if(mode==GUI_OVERVIEW_TRACKER || mode==GUI_OVERVIEW_SONG) track_view=3;
    int count=track_view ? 1 : panel_menu_overview_count(mode);
    int selected = seq_utils_clamp(gstate.clock_help_selected,0,count-1), dirty = 0;
    if(!gstate.clock_help_drawn || gstate.overview_read_only_drawn!=track_view) {
        gstate.overview_read_only_drawn=track_view;
        gfx_fill_rect(gstate.GUI_GRID_X, gstate.GUI_GRID_Y,
            gstate.GUI_GRID_W, gstate.GUI_GRID_H, GUI_TEXT_BG_COLOR);
        memset(gstate.clock_help_rows, 0, sizeof(gstate.clock_help_rows));
        gstate.clock_help_drawn = 1;
        dirty = 1;
        if(track_view==2) return gui_draw_lanes(1)|dirty;
        if(track_view==3) return gui_draw_tracker(1)|dirty;
    }
    if(track_view==2) return gui_draw_lanes(0);
    if(track_view==3) return gui_draw_tracker(0);
    int top = selected < 12 ? 0 : selected - 11;
    for(int row = 0; row < (track_view ? 14 : 18); row++) {
        char text[30] = {0};
        if(mode==GUI_OVERVIEW_SCENE) gui_scene_overview_row(gstate.overview_track,row,text);
        else if(track_view) gui_track_overview_row(gstate.overview_track,row,text);
        else if(row == 0) {} // top breathing room above the menu heading
        else if(row == 1) snprintf(text, sizeof(text), "%s T%c S%c %02u/%02u", panel_menu_overview_title(mode),
            '1'+seq_utils_clamp(seq_ctrl_get_first_track(),0,5),
            '1'+seq_utils_clamp(seq_ctrl_get_scene(),0,5), (unsigned)(selected+1)%100, (unsigned)count%100);
        else if(row <= 13) {
            int item = top + row - 2;
            if(item < count) {
                struct clock_menu_row entry;
                panel_menu_overview_row(mode,item, &entry);
                // Actions show status only, never an invitation to execute.
                if(mode==PANEL_MENU_CLOCK && item == PANEL_MENU_CLOCK_CAPTURE)
                    snprintf(entry.value, sizeof(entry.value), "%s", seq_engine_snapshot_status() ? "Ready" : "Empty");
                if(mode==PANEL_MENU_CLOCK && item == PANEL_MENU_CLOCK_STEP_LEN &&
                        seq_ctrl_get_scene() == seq_engine_get_current_scene() &&
                        seq_engine_get_internal_cc(seq_ctrl_get_first_track(), 16, -1) >= 0) {
                    char effective[16];
                    panel_utils_step_len_to_str(effective,
                        seq_engine_get_internal_cc(seq_ctrl_get_first_track(), 16, -1));
                    snprintf(entry.value, sizeof(entry.value), "%s CCi", effective);
                }
                if(mode==PANEL_MENU_CLOCK && item == PANEL_MENU_CLOCK_STEP_LEN && seq_engine_timing_pending(seq_ctrl_get_first_track(), 0)) {
                    char target[16];
                    panel_utils_step_len_to_str(target, seq_engine_timing_value(seq_ctrl_get_scene(), seq_ctrl_get_first_track(), 0));
                    snprintf(entry.value, sizeof(entry.value), "%s >bar%s", target,
                        seq_engine_get_internal_cc(seq_ctrl_get_first_track(), 16, -1) >= 0 ? "*" : "");
                }
                const char *name = mode==PANEL_MENU_CLOCK && item == PANEL_MENU_CLOCK_SCENE_SYNC ? "Scene Sync" :
                    mode==PANEL_MENU_CLOCK && item == PANEL_MENU_CLOCK_TX_CV ? "CV Clock" : entry.name;
                snprintf(text, sizeof(text), "%c%-13.13s %13.13s", item==selected?'>':' ', name, entry.value);
            }
        } else {
            // Only keep detail when CCi hides the base value being edited.
            int edit_detail=mode==PANEL_MENU_CLOCK && selected==PANEL_MENU_CLOCK_STEP_LEN &&
                seq_ctrl_get_scene()==seq_engine_get_current_scene() &&
                seq_engine_get_internal_cc(seq_ctrl_get_first_track(),16,-1)>=0;
            // Row 14 is always a blank separator; help stays in place.
            int help_start=15;
            if(edit_detail && row==17) {
                struct clock_menu_row entry;panel_menu_overview_row(mode,selected,&entry);
                snprintf(text,sizeof(text),"Edit:%.24s",entry.value);
            } else if(row>=help_start && row<help_start+2)
                snprintf(text,sizeof(text),"%.29s",panel_menu_overview_help(mode,selected,row-help_start));
        }
        if(!strcmp(text, gstate.clock_help_rows[row])) continue;
        strcpy(gstate.clock_help_rows[row], text);
        struct gfx_label *label = &gstate.clock_help_label;
        memset(label, 0, sizeof(*label));
        label->x = gstate.GUI_GRID_X + 2; label->y = gstate.GUI_GRID_Y + row*(track_view ? 16 : 13);
        label->w = 232; label->h = track_view ? 14 : 10; label->font = GFX_FONT_SMALLTEXT_8X10;
        label->fg_color = track_view ? GUI_FONT_COLOR_GREY : GUI_FONT_COLOR_NORMAL;
        label->bg_color = GUI_TEXT_BG_COLOR;
        strcpy(label->text, text);
        if(track_view && row>=2 && !(row&1) && text[0]=='*')
            memset(label->highlight,GFX_HIGHLIGHT_INVERT,2);
        if(!track_view && row>=2 && row<=13 && top+row-2==selected &&
                !(mode==PANEL_MENU_CLOCK && (selected==PANEL_MENU_CLOCK_CAPTURE || selected==PANEL_MENU_CLOCK_RECALL)) &&
                !(mode==PANEL_MENU_SYS && selected==PANEL_MENU_SYS_VERSION))
            memset(label->highlight+15,GFX_HIGHLIGHT_INVERT,13);
        gfx_fill_rect(label->x, label->y, label->w, label->h, label->bg_color);
        gfx_draw_string(label); dirty = 1;
    }
    return dirty;
}

// Tracker edit view: full draw on entry, grid restored on exit.
static int gui_draw_trk_edit(void) {
    int full=!gstate.trk_edit_drawn;
    if(full) {
        gfx_fill_rect(gstate.GUI_GRID_X, gstate.GUI_GRID_Y,
            gstate.GUI_GRID_W, gstate.GUI_GRID_H, GUI_TEXT_BG_COLOR);
        gstate.trk_edit_drawn=1;
    }
    return gui_draw_tracker(full)|full;
}
static int gui_trk_edit_restore(void) {
    if(gstate.trk_edit || !gstate.trk_edit_drawn) return 0;
    gstate.trk_edit_drawn = 0;
    if(gstate.clock_help) return 0;
    gui_draw_grid_bg();
    memset(gstate.grid_state, 0, sizeof(gstate.grid_state));
    memset(gstate.pitch_marks, 0, sizeof(gstate.pitch_marks));
    return 1;
}
// Lane edit view: full draw on entry, grid restored on exit.
static int gui_draw_lane_edit(void) {
    int full=!gstate.lane_edit_drawn;
    if(full) {
        gfx_fill_rect(gstate.GUI_GRID_X, gstate.GUI_GRID_Y,
            gstate.GUI_GRID_W, gstate.GUI_GRID_H, GUI_TEXT_BG_COLOR);
        memset(gstate.clock_help_rows, 0, sizeof(gstate.clock_help_rows));
        gstate.lane_cursor_drawn_row=gstate.lane_cursor_drawn_col=-1;
        gstate.lane_edit_drawn=1;
    }
    return gui_draw_lanes(full)|full;
}
static int gui_lane_edit_restore(void) {
    if(gstate.lane_edit || !gstate.lane_edit_drawn) return 0;
    gstate.lane_edit_drawn = 0;
    if(gstate.clock_help) return 0;  // overview owns the area; it restores later
    gui_draw_grid_bg();
    memset(gstate.grid_state, 0, sizeof(gstate.grid_state));
    memset(gstate.pitch_marks, 0, sizeof(gstate.pitch_marks));
    return 1;
}
static int gui_clock_help_restore(void) {
    if(gstate.clock_help || !gstate.clock_help_drawn) return 0;
    gstate.clock_help_drawn = 0;
    gstate.lane_edit_drawn = 0;  // lane edit (if active) redraws in full
    gstate.trk_edit_drawn = 0;
    gui_draw_grid_bg();
    memset(gstate.grid_state, 0, sizeof(gstate.grid_state));
    memset(gstate.pitch_marks, 0, sizeof(gstate.pitch_marks));
    return 1;
}

// Change only the lower display rectangle; preserve the step grid overlay.
// Full redraw: forget the drawn grid cache. grid_overlay holds the editors'
// requested colours and must survive a redraw (e.g. Undo in Region).
static void gui_full_redraw_reset(void) {
    for(int step = 0; step < SEQ_NUM_STEPS; step ++) gstate.grid_state[step] = 0;
}

static int gui_step_details_layout(int force) {
    int expanded=gstate.step_details;
    if(!force && expanded==gstate.step_details_drawn) return 0;
    int x=gstate.screen_type==GUI_DISP_TYPE_A ? GUI_DISP_A_LABEL_STATUS_L1_X : GUI_DISP_B_LABEL_STATUS_L1_X;
    int base=gstate.screen_type==GUI_DISP_TYPE_A ? GUI_DISP_A_LABEL_STATUS_L1_Y : GUI_DISP_B_LABEL_STATUS_L1_Y;
    int top=gstate.GUI_GRID_Y+gstate.GUI_GRID_H+2;
    gfx_fill_rect(gstate.GUI_GRID_X,top,
        gstate.GUI_PREVIEW_X-gstate.GUI_GRID_X+5*gstate.GUI_PREVIEW_GRID_SPACING+8*gstate.GUI_PREVIEW_SQUARE_W,
        base+46-top,GUI_TEXT_BG_COLOR);
    for(int row=0;row<7;row++) {
        int index=row<4 ? GUI_LABEL_STATUS_L1+row : 13+row-4;
        struct gfx_label *label=&gstate.glabels[index];
        label->x=(!expanded && row>=4) ? UINT16_MAX : x;
        label->y=expanded ? base-36+12*row : base+12*row;
        label->w=expanded ? 232 : 224;label->h=10;label->font=GUI_DISP_A_FONT_NORMAL;
        label->fg_color=GUI_FONT_COLOR_NORMAL;label->bg_color=GUI_TEXT_BG_COLOR;
        label->dirty=1;
    }
    memset(gstate.preview_state,0,sizeof(gstate.preview_state));
    memset(gstate.track_select_state,0,sizeof(gstate.track_select_state));
    memset(gstate.arp_enable_state,0,sizeof(gstate.arp_enable_state));
    gstate.step_details_drawn=expanded;
    return 1;
}

// run the refresh task - run on the main polling loop
void gui_refresh_task(void) {
	int dirty = 0;
	int i, track, step;

    // make sure we're started
    if(!gstate.started) {
        return;
    }

    dirty += gui_clock_help_restore();
    dirty += gui_lane_edit_restore();
    dirty += gui_trk_edit_restore();
    // CCi overrides need no stored-song event; unchanged labels do not redraw.
    gui_update_song_mode();

	// force LCD reinit
	if(gstate.force_reinit) {
	    gstate.force_reinit = 0;
	    gstate.force_refresh = 1;
	    gfx_init_lcd();  // power on and init LCD
	}
	// force refresh
	int lower_refresh=gstate.force_refresh;
	if(gstate.force_refresh) {
        gstate.force_refresh = 0;
        gstate.clock_help_drawn = 0;
        gstate.lane_edit_drawn = 0;
        gstate.trk_edit_drawn = 0;
	    gui_clear_screen();
	    // dirty all the labels
        for(i = 0; i < GUI_MAX_LABELS; i ++) {
            gstate.glabels[i].dirty = 1;
        }
        // dirty all the grid state
        gui_full_redraw_reset();
        for(track = 0; track < SEQ_NUM_TRACKS; track ++) {
            gstate.track_select_state[track] = 0;
            gstate.arp_enable_state[track] = 0;
            for(step = 0; step < SEQ_NUM_STEPS; step ++) {
                gstate.preview_state[track][step] = 0;
            }
        }
	}
    dirty += gui_step_details_layout(lower_refresh);
    dirty += gui_draw_labels();
	// attempt to redraw the squares
	dirty += gui_draw_preview_grids();
    dirty += gstate.clock_help ? gui_draw_clock_help() :
        gstate.lane_edit ? gui_draw_lane_edit() :
        gstate.trk_edit ? gui_draw_trk_edit() : gui_draw_main_grid();
	if(dirty) {
		gfx_commit();
	}
}

// force all GUI items to refresh
void gui_force_refresh(void) {
    gstate.force_refresh = 1;
}

// clear the screen
void gui_clear_screen(void) {
    gfx_clear_screen(0);
}

// get the currently loaded screen type
int gui_get_screen_type(void) {
    return gstate.screen_type;
}

// set the LCD power state
// schedule a power-up and reinit (power on)
// turns off power immediately (power off)
void gui_set_lcd_power(int state) {
    if(state) {
        gstate.force_reinit = 1;  // request a power on and reinit
    }
    else {
        gfx_deinit_lcd();  // power off immediately
    }
}

// enable or disable the grid overlay
void gui_grid_set_overlay_enable(int enable) {
    if(enable) {
        gstate.grid_overlay_enable = 1;
    }
    else {
        gstate.grid_overlay_enable = 0;
    }
}

// clear the grid overlay
void gui_grid_clear_overlay(void) {
    int i;
    for(i = 0; i < SEQ_NUM_STEPS; i ++) {
        gstate.grid_overlay[i] = 0;
    }
}

// set a grid overlay color index
void gui_grid_set_overlay_color(int step, int index) {
    if(step < 0 || step >= SEQ_NUM_STEPS) {
        return;
    }
    switch(index) {
        case GUI_OVERLAY_BLANK:
            gstate.grid_overlay[step] = GUI_OVERLAY_COLOR_BLANK;
            break;
        case GUI_OVERLAY_LOW:
            gstate.grid_overlay[step] = GUI_OVERLAY_COLOR_LOW;
            break;
        case GUI_OVERLAY_MED:
            gstate.grid_overlay[step] = GUI_OVERLAY_COLOR_MED;
            break;
        case GUI_OVERLAY_HIGH:
            gstate.grid_overlay[step] = GUI_OVERLAY_COLOR_HIGH;
            break;
    }
}

//
// status display helpers
//
// set the status / menu override state
void gui_set_status_override(int enable) {
    // overriding the status display for menu / status info
    if(enable) {
        gstate.status_override = 1;
    }
    // reverting back to locally generated info display
    else {
        gstate.status_override = 0;
        gui_clear_menu();
        gui_update_first_track(gstate.first_track);  // cause info to be refreshed
    }
}

// The extra status rows follow the non-status labels in the label table.
static int gui_status_label(int line) {
    return line < 4 ? GUI_LABEL_STATUS_L1 + line : 13 + line - 4;
}

void gui_step_details_set(int enable) { gstate.step_details = !!enable; }

// Expanded editor borrows one extra character; ordinary menus stay 28 wide.
static int gui_status_width(void) { return gstate.step_details ? 29 : GUI_STATUS_LINE_LEN; }

// clear all status text
void gui_clear_status_text_all(void) {
    for(int line=0; line<GUI_NUM_STATUS_LINES; line++) gui_clear_status_text(line);
}

// clear a line of status text
void gui_clear_status_text(int line) {
    int i;
    if(line < 0 || line >= GUI_NUM_STATUS_LINES) {
        return;
    }
    for(i = 0; i < gui_status_width(); i ++) {
        gstate.status_lines[line][i] = ' ';
    }
    gstate.status_lines[line][i] = 0x00;
    gui_set_label(gui_status_label(line), gstate.status_lines[line]);
    gui_set_label_highlight(gui_status_label(line),
        0, gui_status_width(), GFX_HIGHLIGHT_NORMAL);
}

// set a line of status text
void gui_set_status_text(int line, char *text) {
    int i, len;
    if(line < 0 || line >= GUI_NUM_STATUS_LINES) {
        return;
    }
    len = strlen(text);
    if(len > gui_status_width()) {
        len = gui_status_width();
    }
    memcpy(gstate.status_lines[line], text, len);
    // pad out line
    for(i = strlen(text); i < gui_status_width(); i ++) {
        gstate.status_lines[line][i] = ' ';
    }
    gstate.status_lines[line][i] = 0x00;
    gui_set_label(gui_status_label(line), gstate.status_lines[line]);
}

// set a portion of a status line
// this will clear the range and truncate the line if it's too long
void gui_set_status_text_part(int line, int startch, int lench, char *text) {
    int i, len;
    if(line < 0 || line >= GUI_NUM_STATUS_LINES) {
        log_error("gsstp - line invalid: %d", line);
        return;
    }
    if(startch < 0 || lench < 1 || (startch + lench) > (GFX_LABEL_LEN - 1)) {
        log_error("gsstp - range invalid - s: %d - l: %d", startch, lench);
        return;
    }
    len = strlen(text);
    if(len > lench) {
        len = lench;
    }
    memcpy(gstate.status_lines[line] + startch, text, len);
    for(i = startch + strlen(text); i < (startch + lench); i ++) {
        gstate.status_lines[line][i] = ' ';
    }
    gstate.status_lines[line][gui_status_width()] = 0x00;
    gui_set_label(gui_status_label(line), gstate.status_lines[line]);
}

// set the highlight of a status line
void gui_set_status_highlight_part(int line, int startch,
        int lench, int mode) {
    if(line < 0 || line >= GUI_NUM_STATUS_LINES) {
        log_error("gsshp - line invalid: %d", line);
        return;
    }
    if(startch < 0 || lench < 1 || (startch + lench) >= GFX_LABEL_LEN) {
        log_error("gsshp - range invalid - s: %d - l: %d", startch, lench);
        return;
    }
    gui_set_label_highlight(gui_status_label(line), startch, lench, mode);
}

//
// menu control
//
// clear the menu
void gui_clear_menu(void) {
    gui_clear_status_text(0);
    gui_clear_status_text(1);
    gui_clear_status_text(2);
    gui_clear_status_text(3);
}

// set the menu title
void gui_set_menu_title(char *text) {
    gui_set_status_text_part(0, 0, 14, text);
}

// set the menu next/prev
void gui_set_menu_prev_next(int prev, int next) {
    if(!prev && next) {
        gui_set_status_text_part(0, 14, 13, "            >");
    }
    else if(prev && !next) {
        gui_set_status_text_part(0, 14, 13, "          <  ");
    }
    else if(prev && next) {
        gui_set_status_text_part(0, 14, 13, "          < >");
    }
    else {
        gui_set_status_text_part(0, 14, 13, "             ");
    }
}

// set the menu subtitle
void gui_set_menu_subtitle(char *text) {
    gui_set_status_text(1, text);
}

// set the menu param
void gui_set_menu_param(char *text) {
    gui_set_status_text_part(3, 0, 14, text);
}

// set the menu value
void gui_set_menu_value(char *text) {
    gui_set_status_text_part(3, 14, 13, text);
}

// set the edit mode on the menu
void gui_set_menu_edit(int edit) {
    if(edit) {
        gui_set_label_highlight(GUI_LABEL_STATUS_L4, 14, 14, GFX_HIGHLIGHT_INVERT);
    }
    else {
        gui_set_label_highlight(GUI_LABEL_STATUS_L4, 14, 14, GFX_HIGHLIGHT_NORMAL);
    }
}

//
// local functions
//
// handle state change
void gui_handle_state_change(int event_type, int *data, int data_len) {
    switch(event_type) {
        case SCE_SONG_CLEARED:
            gui_update_song(data[0]);
            gui_update_tempo(song_get_tempo());
            gui_update_scene(seq_ctrl_get_scene());
            gui_update_song_mode();
            gui_update_clock_source(song_get_midi_clock_source());
            break;
        case SCE_ENG_UNDO_RECALLED:
        case SCE_ENG_SNAPSHOT_RECALLED:
            gui_notice(event_type == SCE_ENG_UNDO_RECALLED ? "Undo restored" : "Snapshot recalled");
            gui_update_tempo(song_get_tempo());
            gui_update_scene(seq_ctrl_get_scene());
            gui_update_song_mode();
            gui_update_clock_source(song_get_midi_clock_source());
            gui_force_refresh();
            break;
        case SCE_SONG_LOADED:
            gui_update_song(data[0]);
            gui_update_tempo(song_get_tempo());
            gui_update_scene(seq_ctrl_get_scene());
            gui_update_song_mode();
            gui_update_clock_source(song_get_midi_clock_source());
            break;
        case SCE_SONG_SAVED:
            gui_update_song(data[0]);
            break;
        case SCE_CTRL_TRACK_SELECT:
            gui_update_track_select(data[0], data[1]);
            break;
        case SCE_CTRL_FIRST_TRACK:
            gui_update_first_track(data[0]);
            break;
        case SCE_CTRL_EXT_TEMPO:
            gui_update_tempo(midi_clock_get_tempo());
            gui_update_clock_source(song_get_midi_clock_source());  // sync color
            break;
        case SCE_CTRL_EXT_SYNC:
            gui_update_clock_source(song_get_midi_clock_source());  // sync color
            break;
        case SCE_SONG_TEMPO:
            gui_update_tempo(song_get_tempo());
            break;
        case SCE_CTRL_RUN_STATE:
            gui_update_run_enable(data[0]);
            break;
        case SCE_CTRL_RECORD_MODE:
            gui_update_rec_mode(data[0]);
            break;
        case SCE_CTRL_LIVE_MODE:
            gui_update_live_mode(data[0]);
            break;
        case SCE_SONG_MIDI_CLOCK_SOURCE:
            gui_update_clock_source(data[0]);
            break;
        case SCE_CTRL_SONG_MODE:
        case SCE_ENG_SONG_MODE_STATUS:
            gui_update_song_mode();
            break;
        case SCE_SONG_MUTE:
            gui_update_track_mute(data[0], data[1], data[2]);
            break;
        case SCE_SONG_TRANSPOSE:
            gui_update_track_transpose(data[0], data[1], data[2]);
            break;
        case SCE_SONG_ARP_ENABLE:
            gui_update_arp_enable(data[0], data[1], data[2]);
            break;
        case SCE_ENG_HARMONY:
        case SCE_SONG_ROOT:
            gui_update_tonality(gstate.current_scene, gstate.first_track,
                song_get_tonality(gstate.current_scene, gstate.first_track));
            break;
        case SCE_SONG_TONALITY:
            gui_update_tonality(data[0], data[1], data[2]);
            break;
        case SCE_SONG_MOTION_START:
            gui_update_motion_start(data[0], data[1], data[2]);
            break;
        case SCE_SONG_MOTION_LENGTH:
            gui_update_motion_length(data[0], data[1], data[2]);
            break;
        case SCE_SONG_MOTION_DIR:
            gui_update_motion_dir(data[0], data[1], data[2]);
            break;
        case SCE_SONG_STEP_LEN:
            gui_update_step_length(data[0], data[1], data[2]);
            break;
        case SCE_SONG_GATE_TIME:
            gui_update_gate_time_override(data[0], data[1], data[2]);
            break;
        case SCE_SONG_PATTERN_TYPE:
            gui_update_pattern_type(data[0], data[1], data[2]);
            break;
        case SCE_SONG_EUCLIDEAN_DENSITY:
        case SCE_SONG_EUCLIDEAN_ROTATION:
            // Preview grids recompute effective masks each refresh and redraw
            // changed squares only. Do not clear the entire LCD on each edit.
            gui_update_song_mode();
            break;
        case SCE_SONG_TRACK_TYPE:
            gui_update_track_type(data[0], data[1], data[2]);
            break;
        case SCE_ENG_CURRENT_SCENE:
            gui_update_scene(data[0]);
            break;
        case SCE_ENG_ACTIVE_STEP:
            gui_update_active_step(data[0], data[1]);
            break;
        case SCE_ENG_KBTRANS:
            gui_update_keyboard_transpose(data[0]);
            break;
        default:
            break;
    }
}

// draw the grid background
void gui_draw_grid_bg(void) {
    gfx_fill_rect(gstate.GUI_GRID_X, gstate.GUI_GRID_Y,
        gstate.GUI_GRID_W, gstate.GUI_GRID_H, GUI_GRID_BG_COLOR);
}

// draw the grid squares if they are dirty
int gui_draw_main_grid(void) {
    int track = seq_ctrl_get_first_track(), dirty = 0;
    int low = 127, high = 0;
    if(track < 0 || track >= SEQ_NUM_TRACKS) return 0;
    // GUI refresh polls effective state, including transient Bias/CCi/Hrm/Inv.
    // Scratch data lives in gstate, not on the small realtime stack.
    for(int step = 0; step < SEQ_NUM_STEPS; step++) {
        struct seq_step_preview *p = &gstate.pitch_preview[step];
        seq_engine_preview_step(track, step, p);
        for(int i = 0; i < p->count; i++) {
            if(p->pitch[i] < low) low = p->pitch[i];
            if(p->pitch[i] > high) high = p->pitch[i];
        }
    }
    // One octave-anchored range for the WHOLE grid, at least two octaves.
    low = (low / 12) * 12;
    int span = ((high - low + 11) / 12) * 12;
    if(span < 24) span = 24;
    for(int step = 0; step < SEQ_NUM_STEPS; step++) {
        uint16_t marks[SEQ_TRACK_POLY] = {0};
        uint32_t color = gstate.grid_overlay_enable && gstate.grid_overlay[step]
            ? gstate.grid_overlay[step] : gstate.preview_state[track][step];
        struct seq_step_preview *p = &gstate.pitch_preview[step];
        // Step Edit retains the pitch view; other overlays keep their own meaning.
        if(!gstate.grid_overlay_enable || step_edit_get_enable()) for(int i = 0; i < p->count; i++) {
            int px = 3 + (p->delay[i] * (gstate.GUI_GRID_SQUARE_W - 15) + 127) / 255;
            int py = 3 + (gstate.GUI_GRID_SQUARE_H - 8) -
                ((p->pitch[i] - low) * (gstate.GUI_GRID_SQUARE_H - 8) + span / 2) / span;
            marks[i] = (py << 8) | px;
        }
        if(gstate.grid_state[step] == color &&
                !memcmp(gstate.pitch_marks[step], marks, sizeof(marks))) continue;
        int x = gstate.GUI_GRID_SQUARE_X + (gstate.GUI_GRID_SQUARE_W + gstate.GUI_GRID_SQUARE_SPACE) * (step & 7);
        int y = gstate.GUI_GRID_SQUARE_Y + (gstate.GUI_GRID_SQUARE_H + gstate.GUI_GRID_SQUARE_SPACE) * (step >> 3);
        gstate.grid_state[step] = color;
        memcpy(gstate.pitch_marks[step], marks, sizeof(marks));
        gfx_fill_rect(x, y, gstate.GUI_GRID_SQUARE_W, gstate.GUI_GRID_SQUARE_H, color);
        unsigned r = (color >> 16) & 255, g = (color >> 8) & 255, b = color & 255;
        uint32_t ink = (r * 299 + g * 587 + b * 114) > 140000 ? 0xff202020 : 0xffbbbbbb;
        for(int i = 0; i < SEQ_TRACK_POLY; i++) if(marks[i])
            gfx_fill_rect(x + (marks[i] & 255), y + (marks[i] >> 8), 9, 2, ink);
        dirty = 1;
    }
    return dirty;
}

// draw mini preview grids
int gui_draw_preview_grids(void) {
    int step, track, x, y;
    int dirty = 0;
    uint32_t color;
    // render each track grid
    for(track = 0; track < SEQ_NUM_TRACKS; track ++) {
        // figure out the colour for each step
        for(step = 0; step < SEQ_NUM_STEPS; step ++) {
            // figure out what the current color should be on the square
            // muted
            if(gstate.track_mute[track]) {
                // current step
                if(step == gstate.active_step[track]) {
                    color = GUI_GRID_TRACK_COLOR_ACTIVE[track];
                }
                // pattern enabled on step
                else if(gui_get_step_display_enable(track, step)) {
                    color = GUI_GRID_TRACK_COLOR_MUTED[track][gstate.motion_step[track][step]];
                }
                // off
                else {
                    color = GUI_GRID_TRACK_COLOR_OFF[track][gstate.motion_step[track][step]];
                }
            }
            // not muted
            else {
                // current step
                if(step == gstate.active_step[track]) {
                    color = GUI_GRID_TRACK_COLOR_ACTIVE[track];
                }
                // pattern enabled on step
                else if(gui_get_step_display_enable(track, step)) {
                    color = GUI_GRID_TRACK_COLOR_NORMAL[track][gstate.motion_step[track][step]];
                }
                // off
                else {
                    color = GUI_GRID_TRACK_COLOR_OFF[track][gstate.motion_step[track][step]];
                }
            }
            // only redraw if the square has changed
            if(gstate.preview_state[track][step] != color) {
                x = step & 0x07;
                y = (step >> 3) & 0x07;
                if(!gstate.step_details) gfx_fill_rect(gstate.GUI_PREVIEW_X + (gstate.GUI_PREVIEW_SQUARE_W * x) +
                  (gstate.GUI_PREVIEW_GRID_SPACING * track),
                  gstate.GUI_PREVIEW_Y + (gstate.GUI_PREVIEW_SQUARE_H * y),
                  gstate.GUI_PREVIEW_SQUARE_W, gstate.GUI_PREVIEW_SQUARE_H,
                  color);
                gstate.preview_state[track][step] = color;
                if(!gstate.step_details) dirty = 1;
            }
        }
        // track select indicator
        if(gstate.track_select[track]) {
            color = GUI_GRID_TRACK_COLOR_ACTIVE[track];
        }
        else {
            color = GUI_TRACK_UNSELECT_COLOR;
        }
        // only redraw if the select bar has changed
        if(color != gstate.track_select_state[track]) {
            if(!gstate.step_details) gfx_fill_rect(gstate.GUI_PREVIEW_X + (gstate.GUI_PREVIEW_GRID_SPACING * track),
              gstate.GUI_PREVIEW_SELECT_Y,
              (gstate.GUI_PREVIEW_SQUARE_W * 8), gstate.GUI_PREVIEW_SELECT_H,
              color);
            gstate.track_select_state[track] = color;
        }
        // arp enable color
        if(gstate.arp_enable[track]) {
            color = GUI_FONT_COLOR_YELLOW;
        }
        else {
            color = GUI_FONT_COLOR_YELLOW_DIM;
        }
        // only redraw if the arp bar has changed
        if(color != gstate.arp_enable_state[track]) {
            if(!gstate.step_details) gfx_fill_rect(gstate.GUI_PREVIEW_X + (gstate.GUI_PREVIEW_GRID_SPACING * track),
              gstate.GUI_PREVIEW_ARP_Y,
              (gstate.GUI_PREVIEW_SQUARE_W * 8), gstate.GUI_PREVIEW_SELECT_H,
              color);
            gstate.arp_enable_state[track] = color;
        }
    }
    return dirty;
}

// draw the song display if it is dirty
int gui_draw_labels(void) {
    int i, dirty = 0;
    for(i = 0; i < GUI_MAX_LABELS; i ++) {
        if(gstate.glabels[i].x == UINT16_MAX || gstate.glabels[i].dirty == 0) {
            continue;
        }
        gfx_fill_rect(gstate.glabels[i].x, gstate.glabels[i].y,
            gstate.glabels[i].w, gstate.glabels[i].h, gstate.glabels[i].bg_color);
        gfx_draw_string(&gstate.glabels[i]);
        gstate.glabels[i].dirty = 0;
        dirty = 1;
    }
    return dirty;
}

// set a label
void gui_set_label(int index, char *text) {
    if(index < 0 || index >= GUI_MAX_LABELS) {
        log_error("gsl - index invalid: %d", index);
        return;
    }
    // string is unchanged - don't redraw
    if(!strcmp(text, gstate.glabels[index].text)) {
        return;
    }
    // don't crash with zero length strings
    if(*text == 0x00) {
        strcpy(gstate.glabels[index].text, " ");
    }
    else {
        strcpy(gstate.glabels[index].text, text);
    }
    gstate.glabels[index].dirty = 1;
}

// set label highlight
void gui_set_label_highlight(int index, int startch, int lench, int mode) {
    int i;
    if(index < 0 || index >= GUI_MAX_LABELS) {
        log_error("gslh - index invalid: %d", index);
        return;
    }
    if(mode < 0 || mode >= GFX_HIGHLIGHT_MAX_MODES) {
        log_error("gslh - mode invalid: %d", mode);
        return;
    }
    if(startch < 0 || lench < 1 || (startch + lench) > GFX_LABEL_LEN) {
        log_error("gslh - range invalid - s: %d - l: %d", startch, lench);
        return;
    }
    for(i = startch; i < (startch + lench); i ++) {
        gstate.glabels[index].highlight[i] = mode;
    }
    gstate.glabels[index].dirty = 1;
}

// set the prefs for a label
void gui_set_label_prefs(int index, int x, int y, int w, int h,
        int font, uint32_t fg_color, uint32_t bg_color) {
    if(index < 0 || index >= GUI_MAX_LABELS) {
        log_error("gslp - index invalid: %d", index);
        return;
    }
    gstate.glabels[index].x = x;
    gstate.glabels[index].y = y;
    gstate.glabels[index].w = w;
    gstate.glabels[index].h = h;
    gstate.glabels[index].font = font;
    gstate.glabels[index].fg_color = fg_color;
    gstate.glabels[index].bg_color = bg_color;
    gstate.glabels[index].dirty = 1;
}

// set the color of a label
void gui_set_label_color(int index, uint32_t fg_color, uint32_t bg_color) {
    if(index < 0 || index >= GUI_MAX_LABELS) {
        log_error("gslc - index invalid: %d", index);
        return;
    }
    // Polling status must not invalidate an unchanged label on every frame.
    if(gstate.glabels[index].fg_color == fg_color &&
            gstate.glabels[index].bg_color == bg_color) {
        return;
    }
    gstate.glabels[index].fg_color = fg_color;
    gstate.glabels[index].bg_color = bg_color;
    gstate.glabels[index].dirty = 1;
}

// Determine whether a step should be shown as containing material. Within the
// motion window this follows the effective pattern (including Euclidean
// masking). Outside the window, retain the normal indication of stored events
// so Euclidean mode does not make existing notes and CCs appear empty.
int gui_get_step_display_enable(int track, int step) {
    if(!gstate.motion_step[track][step] &&
            song_get_euclidean_density(gstate.current_scene, track) <
            SEQ_EUCLIDEAN_DENSITY_MAX) {
        return song_get_num_step_events(gstate.current_scene, track, step) > 0;
    }
    return pattern_get_step_enable(gstate.current_scene, track,
        gstate.pattern_type[track], step);
}

// calculate the motion squares for a track
void gui_calculate_motion_steps(int track) {
    int i;
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("gcms - track invalid: %d", track);
        return;
    }
    // figure out which steps are enabled for motion
    for(i = 0; i < SEQ_NUM_STEPS; i ++) {
        gstate.motion_step[track][i] = seq_utils_is_step_active(i,
                gstate.motion_start[track],
                gstate.motion_length[track], SEQ_NUM_STEPS);
    }
}

// update the song number display
void gui_update_song(int song) {
    sprintf(tempstr, "SONG %d", (song + 1));
    gui_set_label(GUI_LABEL_SONG, tempstr);
}

// update the scene because it probably changed
void gui_update_scene(int scene) {
    int track;
    if(scene < 0 || scene >= SEQ_NUM_SCENES) {
        log_error("gus - scene invalid: %d", scene);
        return;
    }
    sprintf(tempstr, "SCENE %d", (scene + 1));
    gui_set_label(GUI_LABEL_SCENE, tempstr);

    gstate.current_scene = scene;

    // refresh all display info
    // we need this stuff for all tracks
    for(track = 0; track < SEQ_NUM_TRACKS; track ++) {
        gui_update_step_length(scene, track,
            song_get_step_length(scene, track));
        gui_update_motion_start(scene, track,
            song_get_motion_start(scene, track));
        gui_update_motion_length(scene, track,
            song_get_motion_length(scene, track));
        gui_update_pattern_type(scene, track,
            song_get_pattern_type(scene, track));
        gui_update_arp_enable(scene, track,
            song_get_arp_enable(scene, track));
        gui_update_track_mute(scene, track,
            song_get_mute(scene, track));
    }
    gui_update_first_track(seq_ctrl_get_first_track());
}

// update the track select state
void gui_update_track_select(int track, int select) {
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("guts - track invalid: %d", track);
        return;
    }
    if(select) {
        gstate.track_select[track] = 1;
    }
    else {
        gstate.track_select[track] = 0;
    }

    // check to see if the first track changed
    if(seq_ctrl_get_first_track() != gstate.first_track) {
        gui_update_first_track(seq_ctrl_get_first_track());
    }
}

// update the first selected track
void gui_update_first_track(int first) {
    if(first < 0 || first >= SEQ_NUM_TRACKS) {
        log_error("guft - first invalid: %d", first);
        return;
    }
    gstate.first_track = first;
    gui_overview_follow_track(first);

    // track number
    if(!gstate.status_override) {
        sprintf(tempstr, "Track: %d", (gstate.first_track + 1));
        gui_set_status_text_part(0, 0, 8, tempstr);
    }
    gui_update_track_transpose(gstate.current_scene, first,
        song_get_transpose(gstate.current_scene, first));
    gui_update_tonality(gstate.current_scene, first,
        song_get_tonality(gstate.current_scene, first));
    gui_update_bias_track(gstate.current_scene, first,
        song_get_bias_track(gstate.current_scene, first));
    gui_update_motion_start(gstate.current_scene, first,
        song_get_motion_start(gstate.current_scene, first));
    gui_update_motion_length(gstate.current_scene, first,
        song_get_motion_length(gstate.current_scene, first));
    gui_update_motion_dir(gstate.current_scene, first,
        song_get_motion_dir(gstate.current_scene, first));
    gui_update_step_length(gstate.current_scene, first,
        song_get_step_length(gstate.current_scene, first));
    gui_update_gate_time_override(gstate.current_scene, first,
        song_get_gate_time(gstate.current_scene, first));
    gui_update_track_type(gstate.current_scene, first,
        song_get_track_type(first));
    gui_update_track_mute(gstate.current_scene, first,
        song_get_mute(gstate.current_scene, first));
}

// update the tempo display
void gui_update_tempo(float tempo) {
    sprintf(tempstr, "%d.%d BPM", (int)tempo, ((int)(tempo * 10) % 10));
    gui_set_label(GUI_LABEL_TEMPO, tempstr);
}

// update the run indicator state
void gui_update_run_enable(int enable) {
    if(enable) {
        gui_set_label_color(GUI_LABEL_RUN,
            GUI_FONT_COLOR_GREEN, GUI_TEXT_BG_COLOR);
    }
    else {
        gui_set_label_color(GUI_LABEL_RUN,
            GUI_FONT_COLOR_GREEN_DIM, GUI_TEXT_BG_COLOR);
    }
}

// update the rec indicator state
void gui_update_rec_mode(int mode) {
    switch(mode) {
        case SEQ_CTRL_RECORD_ARM:
            gui_set_label_color(GUI_LABEL_REC,
                GUI_FONT_COLOR_RED, GUI_TEXT_BG_COLOR);
            break;
        case SEQ_CTRL_RECORD_STEP:
            gui_set_label_color(GUI_LABEL_REC,
                GUI_FONT_COLOR_RED, GUI_TEXT_BG_COLOR);
            break;
        case SEQ_CTRL_RECORD_RT:
            gui_set_label_color(GUI_LABEL_REC,
                GUI_FONT_COLOR_RED, GUI_TEXT_BG_COLOR);
            break;
        case SEQ_CTRL_RECORD_IDLE:
        default:
            gui_set_label_color(GUI_LABEL_REC,
                GUI_FONT_COLOR_RED_DIM, GUI_TEXT_BG_COLOR);
            break;
    }
}

// update the clock source mode
void gui_update_clock_source(int source) {
    switch(source) {
        case SONG_MIDI_CLOCK_SOURCE_INT:
            gui_set_label(GUI_LABEL_CLKSRC, "INT");
            gui_set_label_color(GUI_LABEL_CLKSRC,
                GUI_FONT_COLOR_NORMAL, GUI_TEXT_BG_COLOR);
            break;
        default:
            gui_set_label(GUI_LABEL_CLKSRC, "EXT");
            if(midi_clock_is_ext_synced()) {
                gui_set_label_color(GUI_LABEL_CLKSRC,
                    GUI_FONT_COLOR_GREEN, GUI_TEXT_BG_COLOR);
            }
            else {
                gui_set_label_color(GUI_LABEL_CLKSRC,
                    GUI_FONT_COLOR_DARK_GREY, GUI_TEXT_BG_COLOR);
            }
            break;
    }
}

// update the live indicator state
void gui_update_live_mode(int mode) {
    int live;
    int kbtrans;

    switch(mode) {
        case SEQ_CTRL_LIVE_ON:
            live = 1;
            kbtrans = 0;
            break;
        case SEQ_CTRL_LIVE_KBTRANS:
            live = 0;
            kbtrans = 1;
            break;
        case SEQ_CTRL_LIVE_OFF:
        default:
            live = 0;
            kbtrans = 0;
            break;
    }

    if(live) {
        gui_set_label_color(GUI_LABEL_LIVE,
            GUI_FONT_COLOR_CYAN, GUI_TEXT_BG_COLOR);
    }
    else {
        gui_set_label_color(GUI_LABEL_LIVE,
            GUI_FONT_COLOR_CYAN_DIM, GUI_TEXT_BG_COLOR);
    }
    if(kbtrans) {
        gui_set_label_color(GUI_LABEL_KEYTRANS,
            GUI_FONT_COLOR_MAGENTA, GUI_TEXT_BG_COLOR);
    }
    else {
        gui_set_label_color(GUI_LABEL_KEYTRANS,
            GUI_FONT_COLOR_MAGENTA_DIM, GUI_TEXT_BG_COLOR);
    }
}

// update the current keyboard transpose
void gui_update_keyboard_transpose(int transpose) {
    if(transpose == 0) {
        gui_set_label(GUI_LABEL_KEYTRANS, "KB: 0");
    }
    else {
        sprintf(tempstr, "KB:%+-2d", transpose);
        gui_set_label(GUI_LABEL_KEYTRANS, tempstr);
    }
}

// update the track mute state
void gui_update_track_mute(int scene, int track, int mute) {
    if(scene < 0 || scene >= SEQ_NUM_SCENES) {
        log_error("gutm - scene invalid: %d", scene);
        return;
    }
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("gutm - track invalid: %d", track);
        return;
    }
    // ignore scenes other than ours
    if(scene != gstate.current_scene) {
        return;
    }
    // store mute data for preview displays
    if(mute) {
        gstate.track_mute[track] = 1;
    }
    else {
        gstate.track_mute[track] = 0;
    }
    // update info display
    if(!gstate.status_override && track == gstate.first_track) {
        if(gstate.track_mute[track]) {
            gui_set_status_text_part(0, 13, 4, "Mute");
        }
        else {
            gui_set_status_text_part(0, 13, 4, "  On");
        }
    }
}

// uopdate the bias track
void gui_update_bias_track(int scene, int track, int bias_track) {
    if(scene < 0 || scene >= SEQ_NUM_SCENES) {
        log_error("gubt - scene invalid: %d", scene);
        return;
    }
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("gubt - track invalid: %d", track);
        return;
    }
    // ignore scenes other than ours
    if(scene != gstate.current_scene) {
        return;
    }
    // update info display
    if(!gstate.status_override && track == gstate.first_track) {
        if(bias_track == SONG_TRACK_BIAS_NULL || bias_track == track) {
            gui_set_status_text_part(0, 8, 3, " <X");
        }
        else {
            sprintf(tempstr, " <%d", (bias_track + 1));
            gui_set_status_text_part(0, 8, 3, tempstr);
        }
    }
}

// update the track transpose
void gui_update_track_transpose(int scene, int track, int transpose) {
    if(scene < 0 || scene >= SEQ_NUM_SCENES) {
        log_error("gutt - scene invalid: %d", scene);
        return;
    }
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("gutt - track invalid: %d", track);
        return;
    }
    // ignore scenes other than ours
    if(scene != gstate.current_scene) {
        return;
    }
    // we only care about the first track
    if(track != gstate.first_track) {
        return;
    }
    // update info display
    if(!gstate.status_override && track == gstate.first_track) {
        int pending = seq_engine_pitch_pending(track);
        gui_set_status_text_part(0, 19, 6, pending ? "Tran:>" : "Tran: ");
        if(pending) transpose = seq_engine_pitch_value(scene, track);
        panel_utils_transpose_to_str(tempstr, transpose);
        gui_set_status_text_part(0, 25, 3, tempstr);
    }
}

// update the track arp enable state
void gui_update_arp_enable(int scene, int track, int enable) {
    if(scene < 0 || scene >= SEQ_NUM_SCENES) {
        log_error("guae - scene invalid: %d", scene);
        return;
    }
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("guae - track invalid: %d", track);
        return;
    }
    // ignore scenes other than ours
    if(scene != gstate.current_scene) {
        return;
    }
    // store for preview displays
    if(enable) {
        gstate.arp_enable[track] = 1;
    }
    else {
        gstate.arp_enable[track] = 0;
    }
}

// update the tonality display
void gui_update_tonality(int scene, int track, int tonality) {
    if(scene < 0 || scene >= SEQ_NUM_SCENES) {
        log_error("gut - scene invalid: %d", scene);
        return;
    }
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("gut - track invalid: %d", track);
        return;
    }
    // ignore scenes other than ours
    if(scene != gstate.current_scene) {
        return;
    }
    // we only care about the first track
    if(track != gstate.first_track) {
        return;
    }
    // update info display
    if(!gstate.status_override) {
        scale_name_with_root(tempstr,
            seq_engine_get_effective_tonality(seq_ctrl_get_scene(), gstate.first_track),
            seq_engine_get_effective_root(seq_ctrl_get_scene()));
        // Leave columns 19 onward for VOICE/DRUM on both LCD layouts.
        if(seq_engine_get_effective_root(seq_ctrl_get_scene()) < 0) {
            char name[32];
            scale_type_to_name(name, seq_engine_get_effective_tonality(seq_ctrl_get_scene(), gstate.first_track));
            sprintf(tempstr, "%s (Off)", name);
        }
        gui_set_status_text_part(3, 0, 18, tempstr);
    }
}

// update the start position of a track
void gui_update_motion_start(int scene, int track, int pos) {
    if(scene < 0 || scene >= SEQ_NUM_SCENES) {
        log_error("gums - scene invalid: %d", scene);
        return;
    }
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("gums - track invalid: %d", track);
        return;
    }
    if(pos < 0 || pos >= SEQ_NUM_STEPS) {
        log_error("gums - pos invalid: %d", pos);
        return;
    }
    // ignore scenes other than ours
    if(scene != gstate.current_scene) {
        return;
    }
    // store data for preview displays
    gstate.motion_start[track] = pos;
    // update the info display
    if(!gstate.status_override && track == gstate.first_track) {
        sprintf(tempstr, "Start: %d", (gstate.motion_start[track] + 1));
        gui_set_status_text_part(1, 0, 9, tempstr);
    }
    // cause the motion steps to be recalculated
    gui_calculate_motion_steps(track);
}

// update the end position of a track
void gui_update_motion_length(int scene, int track, int length) {
    if(scene < 0 || scene >= SEQ_NUM_SCENES) {
        log_error("guml - scene invalid: %d", scene);
        return;
    }
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("guml - track invalid: %d", track);
        return;
    }
    if(length < 1 || length > SEQ_NUM_STEPS) {
        log_error("guml - length invalid: %d", length);
        return;
    }
    // ignore scenes other than ours
    if(scene != gstate.current_scene) {
        return;
    }
    // store data for preview displays
    gstate.motion_length[track] = length;
    // update info display
    if(!gstate.status_override && track == gstate.first_track) {
        sprintf(tempstr, "Len: %d", gstate.motion_length[track]);
        gui_set_status_text_part(1, 10, 7, tempstr);
    }
    // cause the motion steps to be recalculated
    gui_calculate_motion_steps(track);
}

// update the motion direction for a track
void gui_update_motion_dir(int scene, int track, int rev) {
    if(scene < 0 || scene >= SEQ_NUM_SCENES) {
        log_error("gumd - scene invalid: %d", scene);
        return;
    }
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("gumd - track invalid: %d", track);
        return;
    }
    // ignore scenes other than ours
    if(scene != gstate.current_scene) {
        return;
    }
    // we only care about the first track
    if(track != gstate.first_track) {
        return;
    }
    // update info display
    if(!gstate.status_override && track == gstate.first_track) {
        if(rev) {
            gui_set_status_text_part(1, 19, 3, "REV");
        }
        else {
            gui_set_status_text_part(1, 19, 3, "FWD");
        }
    }
}

// update the step length for a track
void gui_update_step_length(int scene, int track, int len) {
    if(scene < 0 || scene >= SEQ_NUM_SCENES) {
        log_error("gusl - scene invalid: %d", scene);
        return;
    }
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("gusl - track invalid: %d", track);
        return;
    }
    // ignore scenes other than ours
    if(scene != gstate.current_scene) {
        return;
    }
    // we only care about the first track
    if(track != gstate.first_track) {
        return;
    }
    // update info display
    if(!gstate.status_override && track == gstate.first_track) {
        int pending = seq_engine_timing_pending(track, 0);
        gui_set_status_text_part(2, 0, 6, pending ? "Step:>" : "Step: ");
        if(pending) len = seq_engine_timing_value(scene, track, 0);
        panel_utils_step_len_to_str(tempstr, len);
        gui_set_status_text_part(2, 6, 7, tempstr);
    }
}

// update the gate time override - 0-255 = 0-200%
void gui_update_gate_time_override(int scene, int track, int time) {
    if(scene < 0 || scene >= SEQ_NUM_SCENES) {
        log_error("gugto - scene invalid: %d", scene);
        return;
    }
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("gugto - track invalid: %d", track);
        return;
    }
    // ignore scenes other than ours
    if(scene != gstate.current_scene) {
        return;
    }
    // we only care about the first track
    if(track != gstate.first_track) {
        return;
    }
    // update info display
    if(!gstate.status_override && track == gstate.first_track) {
        sprintf(tempstr, "Gate: %d%%", ((time + 1) * 200 / 256));
        gui_set_status_text_part(2, 13, 10, tempstr);
    }
}

// update the pattern type for a track
void gui_update_pattern_type(int scene, int track, int pattern) {
    if(scene < 0 || scene >= SEQ_NUM_SCENES) {
        log_error("gupt - scene invalid: %d", scene);
        return;
    }
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("gupt - track invalid: %d", track);
        return;
    }
    // ignore scenes other than ours
    if(scene != gstate.current_scene) {
        return;
    }
    // store data for preview displays
    gstate.pattern_type[track] = pattern;
}

// update the track type of a track
void gui_update_track_type(int scene, int track, int type) {
    if(scene < 0 || scene >= SEQ_NUM_SCENES) {
        log_error("gutt - scene invalid: %d", scene);
        return;
    }
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("gutt - track invalid: %d", track);
        return;
    }
    // ignore scenes other than ours
    if(scene != gstate.current_scene) {
        return;
    }
    // we only care about the first track
    if(track != gstate.first_track) {
        return;
    }
    // update info display
    if(!gstate.status_override && track == gstate.first_track) {
        if(type == SONG_TRACK_TYPE_DRUM) {
            gui_set_status_text_part(3, 19, 10, "DRUM");
        }
        else {
            gui_set_status_text_part(3, 19, 10, "VOICE");
        }
    }
}

// update the currently active step or -1 to disable
void gui_update_active_step(int track, int step) {
    if(track < 0 || track >= SEQ_NUM_TRACKS) {
        log_error("guas - track invalid: %d", track);
        return;
    }
    // store data for preview displays
    gstate.active_step[track] = step;
}

// update the song mode status display
static char performance_notice[29];
static int performance_notice_ms;
void gui_notice(const char *text) {
    snprintf(performance_notice, sizeof(performance_notice), "%-28.28s", text);
    performance_notice_ms = 2000;
}
void gui_notice_tick(void) { if(performance_notice_ms) performance_notice_ms--; }
void gui_update_song_mode(void) {
    // Queued panel edits do not fire stored-song events until the bar. Poll
    // targets here; label setters leave unchanged text clean. Respect menus.
    if(!gstate.status_override) {
        gui_update_track_transpose(gstate.current_scene, gstate.first_track,
            seq_engine_pitch_value(gstate.current_scene, gstate.first_track));
        gui_update_step_length(gstate.current_scene, gstate.first_track,
            seq_engine_timing_value(gstate.current_scene, gstate.first_track, 0));
    }
    if(performance_notice_ms) {
        gui_set_label(GUI_LABEL_SONG_MODE, performance_notice);
        return;
    }
    gui_update_tonality(gstate.current_scene, gstate.first_track,
        seq_engine_get_effective_tonality(gstate.current_scene, gstate.first_track));
    struct song_mode_state *sngmode;
    sngmode = seq_engine_get_song_mode_state();

    if(seq_ctrl_get_song_mode()) {
        gui_set_label_color(GUI_LABEL_SONG_MODE,
            GUI_FONT_COLOR_YELLOW, GUI_TEXT_BG_COLOR);
    }
    else {
        gui_set_label_color(GUI_LABEL_SONG_MODE,
            GUI_FONT_COLOR_YELLOW_DIM, GUI_TEXT_BG_COLOR);
    }
    {
        char song_status[32], euclidean[32];
        int track = gstate.first_track;
        int length = seq_engine_get_effective_motion_length(track);
        int density = seq_engine_get_effective_euclidean_density(track);
        int rotation = seq_engine_get_effective_euclidean_rotation(track);
        int hits;
        if(sngmode->current_entry == -1 || !seq_ctrl_get_song_mode()) {
            snprintf(song_status, sizeof(song_status), "SONG - x: x/x");
        }
        else {
            snprintf(song_status, sizeof(song_status), "SONG - %d: %d/%d",
                sngmode->current_entry + 1,
                seq_utils_clamp(sngmode->beat_count,
                    SEQ_SONG_LIST_MIN_LENGTH, SEQ_SONG_LIST_MAX_LENGTH),
                sngmode->total_beats);
        }
        if(seq_engine_straight_enabled() || seq_engine_pitch_mode()) {
            // Replace the existing prefix: no extra width or lost Euclidean status.
            memcpy(song_status, seq_engine_straight_enabled() ? "STR" : "PIT", 3);
            song_status[3] = (seq_engine_timing_pending(track, 0) ||
                seq_engine_timing_pending(track, 1) || seq_engine_pitch_pending(track)) ? '*' : ' ';
        }
        // Match playback rounding: mask positions, not sounding notes.
        if(density < 100 && length >= 1 && length <= SEQ_NUM_STEPS) {
            hits = (density * length + 50) / 100;
            snprintf(euclidean, sizeof(euclidean), "%d:%d:%d",
                length, hits, rotation % length);
            {
                size_t right = strlen(euclidean);
                size_t left = strlen(song_status);
                if(right > 8) right = 8;
                if(left > GUI_STATUS_LINE_LEN - right - 1)
                    left = GUI_STATUS_LINE_LEN - right - 1;
                memset(tempstr, ' ', GUI_STATUS_LINE_LEN);
                memcpy(tempstr, song_status, left);
                memcpy(tempstr + GUI_STATUS_LINE_LEN - right, euclidean, right);
                tempstr[GUI_STATUS_LINE_LEN] = '\0';
            }
        }
        else {
            snprintf(tempstr, sizeof(tempstr), "%-28s", song_status);
        }
        gui_set_label(GUI_LABEL_SONG_MODE, tempstr);
    }
}
