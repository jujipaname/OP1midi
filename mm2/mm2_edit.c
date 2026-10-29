/* Copyright (C) 2026 OP1midi. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
/* MM2 éditeur v6: machine à états pure (pas de rendu). Voir mm2.h.
 * Édition live (pas de mode édition) — continuité encodeurs/colonnes (bleu vert blanc rouge):
 *   bleu: rotation = curseur (16 slots, la page suit); clic = mode d'affichage des notes
 *         (numéro / anglais / solfège); Shift+clic = vider la ligne
 *   vert: clic = type source / Shift+clic = learn MIDI on-off; rotation = n° source / Shift = canal
 *   blanc: rotation = destination + n° en liste plate (COMBO, OFF, SND 1..8, TAPE 1..4,
 *          MUTE 1..4, MRK 1..32, M+, M- — les 4 tapes et les 32 markers défilent); clic seul
 *          = sortie du learn touche ou validation du combo; Shift+clic = learn on-off selon
 *          la destination (SND -> touche brute stockée, COMBO -> combo: 3 touches max, ± Shift)
 *   rouge: rotation = fade de la ligne (OFF/1s/2s/8s/32s) / Shift = pistes; Shift+clic = supprimer
 * Learn armé (MIDI, touche ou combo): aucune touche ne quitte l'éditeur (Help valide toujours).
 * Rendu côté intégration: colonnes bleu/vert/blanc/rouge alignées aux encodeurs, curseur en
 * surbrillance (fond couleur de colonne, typo noire); la colonne source affiche toujours la
 * source (étiquette en petites capitales: note/cc/pc); learn MIDI armé, elle affiche LRN;
 * learn touche/combo armé, la colonne destination affiche LRN ou les touches en cours. */
#include "mm2.h"

mm2_row *mm2_row_edit(int i);              /* mm2_core.c */

static uint8_t cur, page, learn, open;                 /* learn: MM2_L_* (0..3) */
static int acc[4];
static uint8_t stage[MM2_COMBO_MAX];                   /* combo en cours d'enregistrement */
static uint8_t stage_n;

static void learn_set(int l)                    /* tout changement de learn vide le staging */
{
    if (learn != l) stage_n = 0;
    learn = (uint8_t)l;
}

static void combo_commit(int row)               /* staging -> stockage */
{
    mm2_row *r = mm2_row_edit(row);
    mm2_combo_set(row, stage, stage_n);
    if (r && r->dst == MM2_D_COMBO) r->dnum = (uint8_t)stage_n;   /* nombre = affichage (COMBO) */
    stage_n = 0;
}

void mm2_edit_open(void) { open = 1; cur = page = 0; learn_set(MM2_L_NONE); for (int i = 0; i < 4; i++) acc[i] = 0; }
void mm2_edit_close(int save) { if (save) mm2_save(); open = 0; learn_set(MM2_L_NONE); }
int mm2_edit_view(void) { return open; }
int mm2_edit_cursor(void) { return cur; }
int mm2_edit_page(void) { return page; }
int mm2_edit_learn_state(void) { return open ? learn : MM2_L_NONE; }
int mm2_edit_learn_stage(uint8_t out[MM2_COMBO_MAX])
{
    if (out) for (int k = 0; k < stage_n; k++) out[k] = stage[k];
    return open && learn == MM2_L_COMBO ? stage_n : 0;
}

void mm2_edit_learn_feed(int stype, int num)               /* prochain NOTE/CC/PC appris */
{
    if (!open || learn != MM2_L_MIDI || stype > MM2_S_MAX || num < 0 || num > 127) return;
    mm2_row *r = mm2_row_edit(cur);
    r->stype = (uint8_t)stype;
    r->snum = (uint8_t)num;
    if (!r->dst) r->dst = MM2_D_TAPE;                      /* ligne vide: destination par défaut */
    if (!r->ch) r->ch = MM2_CH_ALL;
    learn = 0;
}

static int combo_key_ok(int code)                          /* Help/Shift/clics encodeurs: pas jouables */
{
    return code > 0 && code <= 0x7F && code != 0x26 && (code < 0x3B || code > 0x3E);
}

void mm2_edit_learn_feed_key_code(int code, int shift)     /* touche sound apprise (dst SND): 1 touche */
{
    if (!open || learn != MM2_L_KEY || !combo_key_ok(code)) return;
    stage[0] = (uint8_t)(code | (shift ? 0x80 : 0));
    stage_n = 1;
    combo_commit(cur);                                     /* la touche brute est stockée telle quelle */
    learn_set(MM2_L_NONE);
}

void mm2_edit_learn_feed_combo(int code, int shift)        /* touche du combo (dst COMBO) */
{
    if (!open || learn != MM2_L_COMBO || !combo_key_ok(code)) return;
    if (stage_n >= MM2_COMBO_MAX) return;
    stage[stage_n++] = (uint8_t)(code | (shift ? 0x80 : 0));
    if (stage_n == MM2_COMBO_MAX) {                        /* plein: validation auto */
        combo_commit(cur);
        learn_set(MM2_L_NONE);
    }
}

void mm2_edit_delete(void)                                 /* Shift + clic rouge/bleu: ligne -> OFF */
{
    if (!open) return;
    mm2_row *r = mm2_row_edit(cur);
    r->ch = 0; r->stype = 0; r->snum = 0; r->dst = MM2_D_OFF; r->dnum = 0; r->fade = 0;
    mm2_combo_set(cur, 0, 0);                              /* le combo appartient à la ligne */
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
        if (learn) return 1;               /* v6.8: learn armé, aucune touche ne quitte (Help reste
                                            * valide: géré avant). Sortie du learn: clic blanc seul */
        mm2_edit_close(1);
        return shift ? 1 : 0;                                          /* 0: la touche agit normalement */
    }
    if (code == 0x24) { page = (uint8_t)(page ? page - 1 : 3); cur = (uint8_t)(page * 4); return 1; }
    if (code == 0x25) { page = (uint8_t)(page == 3 ? 0 : page + 1); cur = (uint8_t)(page * 4); return 1; }
    if (code == 0x3B) {                       /* bleu: clic = affichage notes / Shift+clic = vider */
        if (shift) mm2_edit_delete();
        else mm2_note_disp_cycle();
        return 1;
    }
    if (code == 0x3C) {                       /* vert: type / Shift+clic = learn MIDI on-off */
        if (shift) { learn_set(learn == MM2_L_MIDI ? MM2_L_NONE : MM2_L_MIDI); return 1; }
        if (learn == MM2_L_MIDI) { learn_set(MM2_L_NONE); return 1; }   /* clic seul: sortie du learn */
        mm2_row *r = mm2_row_edit(cur);
        r->stype = (uint8_t)(r->stype == MM2_S_MAX ? MM2_S_NOTE : r->stype + 1);
        return 1;
    }
    if (code == 0x3D) {                       /* blanc: Shift+clic = learn (SND: touche, COMBO:
                                               * combo) / clic seul = sortie ou validation */
        if (shift) {
            int dst = mm2_row_get(cur)->dst;
            if (dst == MM2_D_SND) learn_set(learn == MM2_L_KEY ? MM2_L_NONE : MM2_L_KEY);
            else if (dst == MM2_D_COMBO) learn_set(learn == MM2_L_COMBO ? MM2_L_NONE : MM2_L_COMBO);
            return 1;                          /* autre destination: rien (pas une affaire de touche) */
        }
        if (learn == MM2_L_KEY || learn == MM2_L_COMBO) {
            if (learn == MM2_L_COMBO) combo_commit(cur);    /* clic blanc = valider le combo */
            learn_set(MM2_L_NONE);
            return 1;
        }
        return 1;                          /* rien d'autre au blanc sans shift (modal) */
    }
    if (code == 0x3E) {                                                /* rouge: Shift = supprimer */
        if (shift) mm2_edit_delete();
        return 1;
    }
    return 0;                                                          /* avalé par l'intégration (modal) */
}

/* liste plate des destinations: OFF, COMBO, SND 1..8, TAPE 1..4, MUTE 1..4, MRK 1..32, M+, M-
 * (0..51; COMBO en premier: première destination atteinte en tournant depuis OFF) */
#define FLAT_LAST 51
static int flat_pos(const mm2_row *r)
{
    if (r->dst == MM2_D_COMBO) return 1;
    if (r->dst == MM2_D_SND) { int n = r->dnum < 1 ? 1 : r->dnum > 8 ? 8 : r->dnum; return 1 + n; }
    if (r->dst == MM2_D_TAPE) { int n = r->dnum < 1 ? 1 : r->dnum > 4 ? 4 : r->dnum; return 9 + n; }
    if (r->dst == MM2_D_MUTE) { int n = r->dnum < 1 ? 1 : r->dnum > 4 ? 4 : r->dnum; return 13 + n; }
    if (r->dst == MM2_D_MRK) { int n = r->dnum < 1 ? 1 : r->dnum > MM2_MRK_MAX ? MM2_MRK_MAX : r->dnum; return 17 + n; }
    if (r->dst == MM2_D_MRP) return 50;
    if (r->dst == MM2_D_MRM) return 51;
    return 0;                                              /* OFF (et toute destination inconnue) */
}
static void flat_set(mm2_row *r, int pos)
{
    if (pos <= 0) { r->dst = MM2_D_OFF; r->dnum = 0; }
    else if (pos == 1) { r->dst = MM2_D_COMBO; r->dnum = 0; }
    else if (pos <= 9) { r->dst = MM2_D_SND; r->dnum = (uint8_t)(pos - 1); }
    else if (pos <= 13) { r->dst = MM2_D_TAPE; r->dnum = (uint8_t)(pos - 9); }
    else if (pos <= 17) { r->dst = MM2_D_MUTE; r->dnum = (uint8_t)(pos - 13); }
    else if (pos <= 49) { r->dst = MM2_D_MRK; r->dnum = (uint8_t)(pos - 17); }
    else if (pos == 50) { r->dst = MM2_D_MRP; r->dnum = 0; }
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
        while (st > 0) { pos = pos >= FLAT_LAST ? 0 : pos + 1; st--; }
        while (st < 0) { pos = pos <= 0 ? FLAT_LAST : pos - 1; st++; }
        flat_set(r, pos);
        if (!r->ch) r->ch = MM2_CH_ALL;                            /* ligne nouvellement assignée */
    } else if (knob == 3) {                                            /* rouge: fade / Shift: pistes */
        if (shift) {
            while (st > 0) { r->fmask = r->fmask >= MM2_FM_MAX ? MM2_FM_ALL : (uint8_t)(r->fmask + 1); st--; }
            while (st < 0) { r->fmask = r->fmask ? (uint8_t)(r->fmask - 1) : MM2_FM_MAX; st++; }
        } else {
            while (st > 0) { r->fade = r->fade >= 4 ? 0 : (uint8_t)(r->fade + 1); st--; }
            while (st < 0) { r->fade = r->fade ? (uint8_t)(r->fade - 1) : 4; st++; }
        }
    }
}
