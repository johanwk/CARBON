/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TRACK_OVERVIEW_KEYS_H
#define TRACK_OVERVIEW_KEYS_H
#include <stdint.h>
/* Observer only: never consumes or defers selection/mute/scene gestures.
 * key is the physical button (0..18), track is 0..5 or -1.
 * allowed: 0=blocked, 1=plain track, 2=Scene-first chord (Scene is key 0).
 * candidate: 1..6=track, 7..12=scene; tick returns 0..5 or 6..11. */
struct track_overview_keys { uint32_t down; unsigned short ms; unsigned char candidate; };
static inline void track_overview_key(struct track_overview_keys *s,int key,
        int track,int pressed,int allowed) {
    if(key<0 || key>=19) {if(pressed) s->candidate=0;return;}
    uint32_t bit=1u<<key;
    if(!pressed) {
        s->down &= ~bit;
        if((track>=0 && s->candidate && (s->candidate-1)%6==track) ||
                (key==0 && s->candidate>6)) s->candidate=0;
        return;
    }
    if(s->down & bit) return; // switch repeats do not restart the hold
    int chord=allowed==2 && s->down==1u;
    s->candidate=track>=0 && ((allowed==1 && !s->down) || chord)
        ? track+1+(chord?6:0) : 0;
    s->ms=0;s->down |= bit;
}
static inline int track_overview_tick(struct track_overview_keys *s) {
    if(s->candidate && ++s->ms>=800) {
        int track=s->candidate-1;s->candidate=0;return track;
    }
    return -1;
}
#endif
