/* Copyright (C) 2026 OP1midi. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
/* MM2 éditeur v6: machine à états pure (pas de rendu). Voir mm2.h.
 * Édition live (pas de mode édition) — continuité encodeurs/colonnes (bleu vert blanc rouge):
 *   bleu: rotation = curseur (16 slots, la page suit)
 *   vert: clic = type source / Shift+clic = learn on-off; rotation = n° source / Shift = canal
 *   blanc: rotation = destination + n° en liste plate (OFF, SND 1..8, TAPE 1..4, MUTE 1..4,
 *          MRK 1..32, M+, M- — les 4 tapes et les 32 markers défilent) / Shift+clic = supprimer
 *   rouge: rotation = fade de la ligne (OFF/1s/2s/8s/32s)
 * Rendu côté intégration: colonnes bleu/vert/blanc/rouge alignées aux encodeurs, curseur en
 * surbrillance (fond couleur de colonne, typo noire); Shift tenu, la colonne source du curseur
 * affiche le canal; learn armé, elle affiche LRN. */
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
    if (!r->ch) r->ch = MM2_CH_ALL;
    learn = 0;
}

void mm2_edit_learn_feed_key(int slot)                     /* touche sound apprise (dst SND) */
{
    if (!open || !learn || slot < 1 || slot > 8) return;
    mm2_row_edit(cur)->dnum = (uint8_t)slot;
    learn = 0;
}

void mm2_edit_delete(void)                                 /* Shift + clic rouge: ligne -> OFF */
{
    if (!open) return;
    mm2_row *r = mm2_row_edit(cur);
    r->ch = 0; r->stype = 0; r->snum = 0; r->dst = MM2_D_OFF; r->dnum = 0; r->fade = 0;
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
    if (code == 0x3C) {                                                /* vert: type / Shift: learn on-off */
        if (shift) { learn = (uint8_t)(learn ^ 1); return 1; }
        mm2_row *r = mm2_row_edit(cur);
        r->stype = (uint8_t)(r->stype == MM2_S_MAX ? MM2_S_NOTE : r->stype + 1);
        return 1;
    }
    if (code == 0x3D) {                                                /* blanc: Shift = supprimer */
        if (shift) mm2_edit_delete();
        return 1;
    }
    return 0;                                                          /* avalé par l'intégration (modal) */
}

/* liste plate des destinations: OFF, SND 1..8, TAPE 1..4, MUTE 1..4, MRK 1..32, M+, M- (0..50) */
#define FLAT_TOTAL 50
static int flat_pos(const mm2_row *r)
{
    if (r->dst == MM2_D_SND) { int n = r->dnum < 1 ? 1 : r->dnum > 8 ? 8 : r->dnum; return n; }
    if (r->dst == MM2_D_TAPE) { int n = r->dnum < 1 ? 1 : r->dnum > 4 ? 4 : r->dnum; return 8 + n; }
    if (r->dst == MM2_D_MUTE) { int n = r->dnum < 1 ? 1 : r->dnum > 4 ? 4 : r->dnum; return 12 + n; }
    if (r->dst == MM2_D_MRK) { int n = r->dnum < 1 ? 1 : r->dnum > MM2_MRK_MAX ? MM2_MRK_MAX : r->dnum; return 16 + n; }
    if (r->dst == MM2_D_MRP) return 49;
    if (r->dst == MM2_D_MRM) return 50;
    return 0;                                              /* OFF (et toute destination inconnue) */
}
static void flat_set(mm2_row *r, int pos)
{
    if (pos <= 0) { r->dst = MM2_D_OFF; r->dnum = 0; }
    else if (pos <= 8) { r->dst = MM2_D_SND; r->dnum = (uint8_t)pos; }
    else if (pos <= 12) { r->dst = MM2_D_TAPE; r->dnum = (uint8_t)(pos - 8); }
    else if (pos <= 16) { r->dst = MM2_D_MUTE; r->dnum = (uint8_t)(pos - 12); }
    else if (pos <= 48) { r->dst = MM2_D_MRK; r->dnum = (uint8_t)(pos - 16); }
    else if (pos == 49) { r->dst = MM2_D_MRP; r->dnum = 0; }
    else { r->dst = MM2_D_MRM; r->dnum = 0; }
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
    if (knob == 0) {                                                   /* bleu: curseur */
        int c = cur + st;
        if (c < 0) c = 0;
        if (c > MM2_ROWS - 1) c = MM2_ROWS - 1;
        cur = (uint8_t)c;
        page = (uint8_t)(cur / 4);
    } else if (knob == 1) {                                            /* vert: n° source / Shift: canal de ligne */
        if (shift) {
            while (st > 0) { r->ch = r->ch >= MM2_CH_ALL ? 1 : (uint8_t)(r->ch + 1); st--; }
            while (st < 0) { r->ch = r->ch > 1 ? (uint8_t)(r->ch - 1) : MM2_CH_ALL; st++; }
        } else {
            while (st > 0) { r->snum = r->snum >= 127 ? 0 : (uint8_t)(r->snum + 1); st--; }
            while (st < 0) { r->snum = r->snum ? (uint8_t)(r->snum - 1) : 127; st++; }
        }
    } else if (knob == 2) {                                            /* blanc: destination en liste plate */
        int pos = flat_pos(r);
        while (st > 0) { pos = pos >= 50 ? 0 : pos + 1; st--; }
        while (st < 0) { pos = pos <= 0 ? 50 : pos - 1; st++; }
        flat_set(r, pos);
        if (!r->ch) r->ch = MM2_CH_ALL;                            /* ligne nouvellement assignée */
    } else if (knob == 3) {                                            /* rouge: fade de la ligne */
        while (st > 0) { r->fade = r->fade >= 4 ? 0 : (uint8_t)(r->fade + 1); st--; }
        while (st < 0) { r->fade = r->fade ? (uint8_t)(r->fade - 1) : 4; st++; }
    }
}
