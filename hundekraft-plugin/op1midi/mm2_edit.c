/* Copyright (C) 2026 OP1midi. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
/* MM2 éditeur v3: machine à états pure (pas de rendu). Voir mm2.h.
 * Édition live (pas de mode édition): bleu clic = type source / Shift+clic = MIDI learn,
 * bleu rotation = n° source, vert rotation = destination / Shift = canal global,
 * blanc rotation = n° destination / Shift = cycle fade, rouge = curseur.
 * Rendu côté intégration: colonnes aux couleurs des encodeurs, ligne du curseur en rouge. */
#include "mm2.h"

mm2_row *mm2_row_edit(int i);              /* mm2_core.c */

static uint8_t cur, page, learn, open;
static int acc[4];

void mm2_edit_open(void) { open = 1; cur = page = learn = 0; for (int i = 0; i < 4; i++) acc[i] = 0; }
void mm2_edit_close(int save) { if (save) mm2_save(); open = 0; learn = 0; }
int mm2_edit_view(void) { return open; }
int mm2_edit_cursor(void) { return cur; }
int mm2_edit_page(void) { return page; }
int mm2_edit_learning(void) { return open && learn; }

void mm2_edit_learn_feed(int stype, int num)               /* prochain NOTE/CC/PC appris */
{
    if (!open || !learn || stype > MM2_S_MAX || num < 0 || num > 127) return;
    mm2_row *r = mm2_row_edit(cur);
    r->stype = (uint8_t)stype;
    r->snum = (uint8_t)num;
    if (!r->dst) r->dst = MM2_D_TAPE;                      /* ligne vide: destination par défaut */
    learn = 0;
}

void mm2_edit_learn_feed_key(int slot)                     /* touche sound apprise (dst SND) */
{
    if (!open || !learn || slot < 1 || slot > 8) return;
    mm2_row_edit(cur)->dnum = (uint8_t)slot;
    learn = 0;
}

static int exit_key(int code)              /* modes, T1..T4, transport: quittent l'éditeur */
{
    return (code >= 0x01 && code <= 0x09) || code == 0x15 ||
           (code >= 0x21 && code <= 0x23) || code == 0x2C || code == 0x2D;
}

int mm2_edit_key(int code, int pressed, int shift)
{
    if (!open || !pressed) return 0;
    if (code == 0x00) { mm2_edit_close(1); return 1; }                 /* Help, Shift ou non: valide */
    if (exit_key(code)) {                                              /* quitte et sauvegarde toujours */
        mm2_edit_close(1);
        return shift ? 1 : 0;                                          /* 0: la touche agit normalement */
    }
    if (code == 0x24) { page = (uint8_t)(page ? page - 1 : 3); cur = (uint8_t)(page * 4); return 1; }
    if (code == 0x25) { page = (uint8_t)(page == 3 ? 0 : page + 1); cur = (uint8_t)(page * 4); return 1; }
    if (code == 0x3B) {                                                /* bleu: type / Shift: learn */
        if (shift) { learn = 1; return 1; }
        mm2_row *r = mm2_row_edit(cur);
        r->stype = (uint8_t)(r->stype == MM2_S_MAX ? MM2_S_NOTE : r->stype + 1);
        return 1;
    }
    return 0;                                                          /* avalé par l'intégration (modal) */
}

void mm2_edit_turn(int knob, int delta, int shift)
{
    if (!open || knob < 0 || knob > 3) return;
    if ((acc[knob] > 0 && delta < 0) || (acc[knob] < 0 && delta > 0)) acc[knob] = 0;
    acc[knob] += delta;
    int st = acc[knob] / MM2_DETENT;
    if (!st) return;
    acc[knob] -= st * MM2_DETENT;
    mm2_row *r = mm2_row_edit(cur);
    if (knob == 3) {                                                   /* rouge: curseur */
        int c = cur + st;
        if (c < 0) c = 0;
        if (c > MM2_ROWS - 1) c = MM2_ROWS - 1;
        cur = (uint8_t)c;
        page = (uint8_t)(cur / 4);
    } else if (knob == 0 && st) {                                      /* bleu: n° source, wrap 0..127 */
        while (st > 0) { r->snum = r->snum >= 127 ? 0 : (uint8_t)(r->snum + 1); st--; }
        while (st < 0) { r->snum = r->snum ? (uint8_t)(r->snum - 1) : 127; st++; }
    } else if (knob == 1) {                                            /* vert: destination / Shift: canal */
        if (shift) {
            while (st > 0) { mm2_ch_step(1); st--; }
            while (st < 0) { mm2_ch_step(-1); st++; }
        } else {
            while (st > 0) { r->dst = r->dst >= MM2_D_MAX ? MM2_D_OFF : (uint8_t)(r->dst + 1); st--; }
            while (st < 0) { r->dst = r->dst ? (uint8_t)(r->dst - 1) : MM2_D_MAX; st++; }
        }
    } else if (knob == 2) {                                            /* blanc: n° / Shift: fade */
        if (shift) {
            while (st > 0) { mm2_fade_step(1); st--; }
            while (st < 0) { mm2_fade_step(-1); st++; }
        } else {
            int lo, hi;
            mm2_dnum_range(r->dst, &lo, &hi);
            while (hi > lo && st > 0) { r->dnum = r->dnum >= hi ? (uint8_t)lo : (uint8_t)(r->dnum + 1); st--; }
            while (hi > lo && st < 0) { r->dnum = r->dnum <= lo ? (uint8_t)hi : (uint8_t)(r->dnum - 1); st++; }
        }
    }
}
