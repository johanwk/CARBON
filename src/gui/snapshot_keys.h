/* SPDX-License-Identifier: GPL-3.0-or-later
 * Clock-first chord recognizer. Independent of menu/edit state. */
#ifndef SNAPSHOT_KEYS_H
#define SNAPSHOT_KEYS_H
enum snapshot_key { SNAP_CLOCK, SNAP_RECORD, SNAP_LOAD, SNAP_RUN };
enum snapshot_action { SNAP_PASS, SNAP_CONSUME, SNAP_MENU, SNAP_TAP, SNAP_CAPTURE, SNAP_RECALL, SNAP_UNDO };
struct snapshot_keys { unsigned char down[4], used; };
static inline enum snapshot_action snapshot_keys_event(struct snapshot_keys *s,
        int key, int pressed, int shift) {
    if(key < 0 || key > 3) return SNAP_PASS;
    if(pressed) {
        if(s->down[key]) return SNAP_CONSUME;
        s->down[key] = 1;
        if(key == SNAP_CLOCK) {
            s->used = !!shift;
            return shift ? SNAP_TAP : SNAP_CONSUME;
        }
        if(s->down[SNAP_CLOCK]) {
            s->used = 1;
            return key == SNAP_RECORD ? SNAP_CAPTURE : key == SNAP_LOAD ? SNAP_RECALL : SNAP_UNDO;
        }
        return SNAP_PASS;
    }
    int was_down = s->down[key];
    s->down[key] = 0;
    if(key == SNAP_CLOCK) return was_down && !s->used ? SNAP_MENU : SNAP_CONSUME;
    return SNAP_PASS; // Record/Load releases have no ordinary action
}
#endif
