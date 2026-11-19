/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef OVERVIEW_KEYS_H
#define OVERVIEW_KEYS_H
/* Non-Clock menu buttons: defer unshifted taps until release. */
struct overview_keys { unsigned char down[3], used[3]; unsigned short ms[3]; };
static inline void overview_keys_cancel(struct overview_keys *s) {
    for(int i=0;i<3;i++) if(s->down[i]) s->used[i]=1;
}
/* 0=pass through, 1=consumed, 2=short tap action on release. */
static inline int overview_keys_event(struct overview_keys *s,int key,int pressed,int shift) {
    if(key<0 || key>=3) {if(pressed) overview_keys_cancel(s);return 0;}
    if(pressed) {
        if(s->down[key]) return 1;
        overview_keys_cancel(s);
        if(shift) return 0;
        s->down[key]=1;s->used[key]=0;s->ms[key]=0;return 1;
    }
    if(!s->down[key]) return 0;
    s->down[key]=0;return s->used[key]?1:2;
}
/* Returns key index once on crossing the hold threshold, otherwise -1. */
static inline int overview_keys_tick(struct overview_keys *s) {
    for(int i=0;i<3;i++) if(s->down[i] && !s->used[i] && ++s->ms[i]>=800) {
        s->used[i]=1;return i;
    }
    return -1;
}
#endif
