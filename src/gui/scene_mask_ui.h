/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SCENE_MASK_UI_H
#define SCENE_MASK_UI_H
/* Four private, non-NUL ASCII control glyphs. Ordinary fonts are unchanged. */
static inline unsigned scene_mask_glyph_row(unsigned ch, int row) {
    unsigned bits = ch - 1;
    return row >= 2 && row <= 8 ? ((bits & 1 ? 0x0e : 0) | (bits & 2 ? 0x70 : 0)) : 0;
}
static inline void scene_mask_text(unsigned mask, char text[4]) {
    for(int i=0;i<3;i++) text[i] = 1 + ((mask >> (2*i)) & 3);
    text[3] = 0;
}
struct scene_mask_keys { unsigned down, owned; };
/* 0: normal dispatch, 1: consume, 2: toggle. Ownership survives editor exit. */
static inline int scene_mask_key(struct scene_mask_keys *s, int key, int pressed, int active) {
    if(key < 0 || key >= 6) return 0;
    unsigned bit = 1u << key;
    if(pressed) {
        if(s->down & bit) return 1;
        s->down |= bit;
        if(active) { s->owned |= bit; return 2; }
        return 0;
    }
    s->down &= ~bit;
    if(s->owned & bit) { s->owned &= ~bit; return 1; }
    return 0; // release a normal track press even if editor was entered meanwhile
}
#endif
