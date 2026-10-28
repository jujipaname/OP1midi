/* Copyright (C) 2026 OP1midi. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
/* MM2 éditeur: machine à états pure (pas de rendu). Voir mm2.h.
 * Blanc = éditeur (action en rotation; appui = édition de valeur, re-appui = valider),
 * rouge = curseur. Rendu: ligne du curseur en rouge, les autres en blanc (côté intégration).
 * Sorties (toujours avec sauvegarde): Help (0x00, Shift ou non) -> 1 (consommé); touches de mode
 * (0x01..0x05, 0x15, 0x2C, 0x2D), T1..T4 (0x06..0x09) et transport (0x21..0x23) -> 0 sans Shift
 * (l'intégration laisse la touche agir), 1 avec Shift (pas de combo accidentel). Les autres
 * touches: 0 tant que l'éditeur reste ouvert (l'intégration les avale: modal). */
#include "mm2.h"

mm2_row *mm2_row_edit(int i);           /* mm2_core.c */

static uint8_t cur, page, edit, open;
static int acc[4];

void mm2_edit_open(void) { open = 1; cur = page = edit = 0; for (int i = 0; i < 4; i++) acc[i] = 0; }
void mm2_edit_close(int save) { if (save) mm2_save(); open = 0; edit = 0; }
int mm2_edit_view(void) { return open; }
int mm2_edit_cursor(void) { return cur; }
int mm2_edit_page(void) { return page; }
int mm2_edit_editing(void) { return edit; }

static int val_max(void)
{
    return mm2_row_get(cur)->act == MM2_TAPE ? 4 : 255;
}

static int val_lo(void)
{
    return mm2_row_get(cur)->act == MM2_TAPE ? 1 : 0;
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
    if (code == 0x24) { page = (uint8_t)(page ? page - 1 : 3); cur = (uint8_t)(page * 4); edit = 0; return 1; }
    if (code == 0x25) { page = (uint8_t)(page == 3 ? 0 : page + 1); cur = (uint8_t)(page * 4); edit = 0; return 1; }
    if (code == 0x3C) {                                                /* vert: CC <-> PC */
        mm2_row *r = mm2_row_edit(cur);
        r->type = (uint8_t)(r->type ^ 1);
        return 1;
    }
    if (code == 0x3D) {                                                /* blanc: édition valeur on/off */
        int a = mm2_row_get(cur)->act;
        if (!edit && a != MM2_TAPE && a != MM2_INSTR) return 1;        /* rien à éditer: avalé quand même */
        edit = (uint8_t)(edit ^ 1);
        return 1;
    }
    return 0;
}

void mm2_edit_turn(int knob, int delta)
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
        edit = 0;
    } else if (knob == 0) {                                            /* bleu: canal, wrap 1..ALL */
        int v = (r->ch ? r->ch : 1) + st;                              /* 0 = non réglé: base 1 */
        if (v < 1) v = MM2_CH_ALL;
        if (v > MM2_CH_ALL) v = 1;
        r->ch = (uint8_t)v;
    } else if (knob == 1 && !edit) {                                   /* vert: numéro, wrap 0..127 */
        int v = r->num + st;
        if (v < 0) v = 127;
        if (v > 127) v = 0;
        r->num = (uint8_t)v;
    } else if (knob == 2) {                                            /* blanc: action / valeur */
        if (edit) {
            int v = r->val + st, lo = val_lo(), mx = val_max();
            if (v < lo) v = mx;
            if (v > mx) v = lo;
            r->val = (uint8_t)v;
        } else {
            int a = r->act + st;
            if (a < 0) a = MM2_ACT_MAX;
            if (a > MM2_ACT_MAX) a = 0;
            if (a != r->act) { r->act = (uint8_t)a; edit = 0; }
        }
    }
}
