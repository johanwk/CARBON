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
#ifndef GUI_H
#define GUI_H

#include "../config.h"
#include <inttypes.h>

// GUI overlay colors
#define GUI_OVERLAY_BLANK 0
#define GUI_OVERLAY_LOW 1
#define GUI_OVERLAY_MED 2
#define GUI_OVERLAY_HIGH 3

// init the GUI and reset all vars
int gui_init(void);

// close the GUI
void gui_close(void);

// start the GUI and load all layout params - config store must be loaded
void gui_startup(void);

// run the refresh task - run on the main polling loop
void gui_refresh_task(void);

// force all GUI items to refresh
void gui_force_refresh(void);

// clear the screen
void gui_clear_screen(void);

// get the currently loaded screen type
int gui_get_screen_type(void);

// set the LCD power state 
// schedule a power-up and reinit (power on)
// turns off power immediately (power off)
void gui_set_lcd_power(int state);

// Read-only Clock overview, rendered over the main grid in the GUI loop.
int gui_clock_help_active(void);
#define GUI_OVERVIEW_TRACK 255
#define GUI_OVERVIEW_SCENE 254
#define GUI_OVERVIEW_LANES 253  // read-only DRUM lanes (long-press a DRUM track)
#define GUI_OVERVIEW_TRACKER 252  // read-only tracker, one track x 6 slots
#define GUI_OVERVIEW_SONG 251  // read-only tracker, 6 tracks of one scene
// Track overviews: lanes (DRUM) or tracker pages 1-4, then Settings.
// Scene overviews: song tracker pages 1-4, then the scene settings.
void gui_overview_page(int change);
// Step Edit lane view (DRUM): draw lanes over the step grid with a cursor.
// lanes[10]: pitch per row from the bottom, -1 = empty row. on=0 restores.
void gui_lane_edit_set(int on, int track, const int *lanes, int cursor_lane, int cursor_step);
int gui_overview_mode(void);
void gui_track_overview_open(int track);
void gui_scene_overview_open(int scene);
void gui_overview_open(int mode);
void gui_overview_edit(int change);
void gui_clock_help_set(int enable);
void gui_clock_help_scroll(int change);

// enable or disable the grid overlay
void gui_grid_set_overlay_enable(int enable);

// clear the grid overlay
void gui_grid_clear_overlay(void);

// set a grid overlay color index
void gui_grid_set_overlay_color(int step, int index);

//
// status display helpers
//
// set the status / menu override state
void gui_set_status_override(int enable);

// Expanded Step Edit owns the preview/status area, never the main grid.
void gui_step_details_set(int enable);

// clear all status text
void gui_clear_status_text_all(void);

// clear a line of status text
void gui_clear_status_text(int line);

// set a line of status text
void gui_set_status_text(int line, char *text);

// set a portion of a status line
// will clear the range and truncate the line if it's too long
void gui_set_status_text_part(int line, int startch, int lench, char *text);

// set the highlight of a status line
void gui_set_status_highlight_part(int line, int startch, 
    int lench, int mode);

//
// menu control
//
// clear the menu
void gui_clear_menu(void);

// set the menu title
void gui_set_menu_title(char *text);

// set the menu next/prev
void gui_set_menu_prev_next(int prev, int next);

// set the menu subtitle
void gui_set_menu_subtitle(char *text);

// set the menu param
void gui_set_menu_param(char *text);

// set the menu value
void gui_set_menu_value(char *text);

// set the edit mode on the menu - highlight the field
void gui_set_menu_edit(int edit);

// Short performance feedback on the song-status line; menus remain intact.
void gui_notice(const char *text);
void gui_notice_tick(void);
#endif

