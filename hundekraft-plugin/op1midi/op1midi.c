/* OP1midi — mapping MIDI generaliste (mm2) comme feature plugin kennel, cible Hundekraft 3.0.3.
 * Copyright (C) 2026 OP1midi. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE).
 *
 * Entree: Shift + Help (mode clock) -> T3 (settings) -> rangée "MIDI MAP" (op1_switch enregistré
 * à OP1_EV_BOOT_DONE après les built-ins: 2e rangée = blanche): tourner = ON/OFF global, appui
 * blanc = ouvre l'éditeur.
 * Éditeur (overlay modal sur voile + panneau noir): bleu = canal, vert = numéro (appui = CC<->PC),
 * blanc = action puis édition de valeur (re-appui = valider), rouge = curseur, fleches = pages.
 * Sorties (sauvegarde systématique): Help (Shift ou non), touches de mode / T1..T4 / transport
 * (la touche agit ensuite, sauf Shift tenu: consommée). Ligne du curseur en rouge, autres blancs;
 * valeur en cours d'édition en vert.
 *
 * midi_raw (timer): CC B0 -> mm2_cc, program change C0 -> mm2_pc. Les actions sont mises en file
 * et exécutées sur la main loop (OP1_EV_SERVICE): saut de marqueur (tm_pos/tm_n partagés +
 * op1_tape_pos_get/set), lecture depuis le marqueur courant (+ op1_seq_press_play), sélection de
 * tape (op1_tape_switch 1..4). INSTRUMENT: aucun service core pour l'instant -> toast unique.
 * Les CC mappés sont retirés au chemin clés/encodeurs du core (op1_midi_take_cc). */
#include "../../op1_feature.h"
#include "../../op1_feature_midi.h"
#include "../../op1_feature_tape.h"
#include "../../op1_feature_seq.h"
#include "mm2.h"
#include "../../screens/stroke_font.h"

#ifndef OP1_HOST
#pragma GCC optimize ("Os", "no-tree-loop-distribute-patterns")
#endif

#define FEAT "op1midi"

static void wire(void);

/* ---- ABI mm2 -> réglages kennel (espace de noms du plugin) ---- */
static int mm_get(const char *k, int d) { return (int)op1_setting_get(FEAT, k, (int32_t)d); }
static void mm_put(const char *k, int v) { op1_setting_set(FEAT, k, v); }

/* ---- état suivi (timer / events) ---- */
static volatile uint8_t cur_mode;                /* OP1_EV_MODE / op1_input.mode */
static volatile int32_t tape_raw = 3;            /* OP1_EV_TRANSPORT: 3 arrêt, 6 armé, 10 lecture */

/* ---- file d'actions (producteur: timer midi_raw; consommateur: main loop) ---- */
enum { K_JUMP = 1, K_PLAY, K_TAPE, K_INSTR };
typedef struct { uint8_t kind; int16_t arg; } pend_t;
static pend_t pq[8];
static volatile uint8_t pq_wr, pq_rd;

static void pq_push(int kind, int arg)
{
    uint8_t w = pq_wr, n = (uint8_t)((w + 1) & 7);
    if (n == pq_rd) return;                      /* plein: l'action est jetée */
    pq[w].kind = (uint8_t)kind;
    pq[w].arg = (int16_t)arg;
    pq_wr = n;
}

static void env_query(mm2_env *e)
{
    e->tape_mode = cur_mode == 2;                /* mode tape (ModeSwitcher, 0xFF907D58) */
    e->stopped = tape_raw == 3;                  /* 6 = armé (count-in): bloque aussi */
    e->punch = pu_mode;                          /* données partagées du core (op1_feature_tape.h) */
    e->counting = 0;                             /* couvert par l'état armé */
    e->markers = tm_n;
}

/* ---- CC pris au chemin clés/encodeurs du core ---- */
static uint8_t taken[16];                        /* bit par CC 0..127 */
static uint8_t takes_dirty;
static void apply_takes(void)
{
    uint8_t want[16];
    for (int i = 0; i < 16; i++) want[i] = 0;
    for (int i = 0; i < MM2_ROWS; i++) {
        const mm2_row *r = mm2_row_get(i);
        if (r->act && r->type == 0) want[r->num >> 3] |= (uint8_t)(1u << (r->num & 7));
    }
    for (int c = 0; c < 128; c++) {
        int w = want[c >> 3] >> (c & 7) & 1, h = taken[c >> 3] >> (c & 7) & 1;
        if (w != h) op1_midi_take_cc(c, w);
    }
    for (int i = 0; i < 16; i++) taken[i] = want[i];
    takes_dirty = 0;
}

/* ---- actions (main loop) ---- */
#define TM_STEP 441                              /* 10 ms: le marqueur "sur" le curseur */
static void jump(int dir)
{
    if (!tm_n) return;
    int32_t c = op1_tape_pos_get(), t = -1;
    if (dir > 0) {
        for (int i = 0; i < tm_n; i++) if (tm_pos[i] > c + TM_STEP) { t = tm_pos[i]; break; }
        if (t < 0) t = tm_pos[0];                /* wrap */
    } else {
        for (int i = tm_n - 1; i >= 0; i--) if (tm_pos[i] < c - TM_STEP) { t = tm_pos[i]; break; }
        if (t < 0) t = tm_pos[tm_n - 1];
    }
    op1_tape_pos_set(t);
}

static void locate_current(void)
{
    if (!tm_n) return;
    int32_t c = op1_tape_pos_get(), t = -1;
    for (int i = tm_n - 1; i >= 0; i--) if (tm_pos[i] <= c + TM_STEP) { t = tm_pos[i]; break; }
    if (t >= 0 && t != c) op1_tape_pos_set(t);
}

static uint8_t warned_instr;
static void drain(void)
{
    while (pq_rd != pq_wr) {
        pend_t p = pq[pq_rd];
        pq_rd = (uint8_t)((pq_rd + 1) & 7);
        mm2_env e;
        env_query(&e);
        if (p.kind == K_JUMP) { if (e.tape_mode && e.markers) jump(p.arg); }
        else if (p.kind == K_PLAY) {
            if (e.tape_mode && e.stopped && !e.punch) { locate_current(); op1_seq_press_play(); }
        } else if (p.kind == K_TAPE) {
            if (p.arg >= 1 && p.arg <= 4) op1_tape_switch(p.arg);   /* tapes 0..3 côté core */
        } else if (p.kind == K_INSTR) {
            if (!warned_instr) { op1_toast("MIDI MAP: NO CORE SERVICE"); warned_instr = 1; }
        }
    }
    if (takes_dirty) apply_takes();
}

/* ---- hooks mm2: mise en file seulement (timer context) ---- */
static void act_next(int dir) { pq_push(K_JUMP, dir); }
static void act_play(void) { pq_push(K_PLAY, 0); }
static void act_tape(int t) { pq_push(K_TAPE, t); }
static void act_instr(int s) { pq_push(K_INSTR, s); }

/* ---- MIDI: CC et program change (timer) ---- */
static void midi_raw(const uint8_t *m, int len)
{
    wire();
    if (len < 2) return;
    int st = m[0] & 0xF0;
    if (st == 0xB0 && len >= 3) mm2_cc(m[0] & 15, m[1], m[2]);
    else if (st == 0xC0 && len >= 2) mm2_pc(m[0] & 15, m[1]);
}

/* ---- entrées: ouverture (appui blanc en mode clock) + éditeur modal ---- */
static uint64_t seen_down;
static int input(const op1_input *in)
{
    wire();
    if (in->type == OP1_IN_KEY) {
        if (!mm2_edit_view()) {
            if (in->code == 0x3D && in->pressed && in->mode == 0x0C && !op1_text_entry_open()) {
                mm2_edit_open();
                seen_down = 0;
                return 1;
            }
            return 0;
        }
        if (in->pressed) seen_down |= (uint64_t)1 << (in->code & 63);
        else if (!(seen_down >> (in->code & 63) & 1)) return 0;   /* release tenue avant l'ouverture */
        seen_down &= ~((uint64_t)1 << (in->code & 63));
        int was = mm2_edit_view();
        int consumed = mm2_edit_key(in->code, in->pressed, in->shift);
        if (was && !mm2_edit_view()) takes_dirty = 1;             /* validé: re-appliquer les takes */
        if (!consumed && was && mm2_edit_view()) return 1;        /* toujours ouvert: modal */
        return consumed;                                          /* 0: sortie, la touche agit */
    }
    if (mm2_edit_view()) { mm2_edit_turn(in->code, in->delta); return 1; }
    return 0;
}

/* ---- rendu de l'éditeur (overlay, Gfx): voile + panneau noir opaque (motif du plugin
 * randomiser du kennel), curseur rouge, autres blancs, édition en vert ---- */
static const uint8_t c_red[4] = { 0xFF, 0x4E, 0x3F, 0xFF }, c_white[4] = { 0xFF, 0xFF, 0xFF, 0xFF },
                     c_green[4] = { 0x00, 0xED, 0x95, 0xFF }, c_black[4] = { 0, 0, 0, 0xFF };
#define ES   OP1_Q10(5) / 2
#define EW   0x600
#define X_CAN OP1_Q10(8)
#define X_NUM OP1_Q10(56)
#define X_ACT OP1_Q10(128)
#define X_VAL OP1_Q10(228)
#define Y_T  OP1_Q10(10)
#define Y0   OP1_Q10(40)
#define DY   OP1_Q10(26)
#define PN_X0 6                                               /* panneau, px */
#define PN_X1 314
#define PN_Y0 4
#define PN_Y1 156

/* Raster +0 pixels (RGB565), +4 largeur, +8 stride (octets), +0x0C hauteur (motif randomiser) */
typedef struct { uint16_t *px; uint32_t w, stride, h; } mm_raster;
static void mm_fade(void *r)
{
    const mm_raster *ra = r;
    uint8_t *row = (uint8_t *)ra->px;
    for (uint32_t y = 0; y < ra->h; y++, row += ra->stride & ~1u)
        for (uint32_t x = 0; x < ra->w; x++) ((uint16_t *)row)[x] = (uint16_t)(((uint16_t *)row)[x] >> 1 & 0x7BEF);
}

static void mm_hline(void *r, int x0, int x1, int y, int w_px, const uint8_t *c)
{
    op1_pt a = { OP1_Q10(x0), OP1_Q10(y) }, b = { OP1_Q10(x1), OP1_Q10(y) };
    op1_raster_line(r, &a, &b, OP1_Q10(w_px), c);
}

static void mm_panel(void *r)                                  /* fond noir opaque + bordure */
{
    for (int y = PN_Y0 + 1; y < PN_Y1; y += 2) mm_hline(r, PN_X0, PN_X1, y, 2, c_black);
    mm_hline(r, PN_X0 + 1, PN_X1 - 1, PN_Y0, 2, c_white);
    mm_hline(r, PN_X0 + 1, PN_X1 - 1, PN_Y1, 2, c_white);
    op1_pt a = { OP1_Q10(PN_X0), OP1_Q10(PN_Y0) }, b = { OP1_Q10(PN_X0), OP1_Q10(PN_Y1) };
    op1_raster_line(r, &a, &b, OP1_Q10(2), c_white);
    op1_pt c = { OP1_Q10(PN_X1), OP1_Q10(PN_Y0) }, d = { OP1_Q10(PN_X1), OP1_Q10(PN_Y1) };
    op1_raster_line(r, &c, &d, OP1_Q10(2), c_white);
}

static char *put_str(char *e, const char *s) { while (*s) *e++ = *s++; *e = 0; return e; }
static char *put_num(char *e, int v)                       /* 0..255 */
{
    if (v >= 100) *e++ = (char)('0' + v / 100);
    if (v >= 10) *e++ = (char)('0' + v / 10 % 10);
    *e++ = (char)('0' + v % 10);
    *e = 0;
    return e;
}

static const char *const act_names[] = { "--", "NEXT", "PREV", "PLAY M", "TAPE", "INSTR" };

static void overlay(void *r, int32_t screen)
{
    (void)screen;
    if (!mm2_edit_view()) return;
    mm_fade(r);
    mm_panel(r);
    int page = mm2_edit_page(), cur = mm2_edit_cursor(), ed = mm2_edit_editing();
    sf_text(r, X_CAN, Y_T, ES, "MIDI MAP", EW, c_white);
    char pb[10];
    char *e = put_str(pb, "PAGE ");
    e = put_num(e, page + 1);
    put_str(e, "/4");
    sf_text(r, X_VAL, Y_T, ES, pb, EW, c_white);
    for (int k = 0; k < 4; k++) {
        int i = page * 4 + k;
        const mm2_row *w = mm2_row_get(i);
        const uint8_t *col = i == cur ? c_red : c_white;
        int y = Y0 + k * DY;
        char b[8];
        if (w->ch == MM2_CH_ALL) put_str(b, "ALL"); else put_num(b, w->ch);
        sf_text(r, X_CAN, y, ES, b, EW, col);
        e = put_str(b, w->type ? "PC " : "CC ");
        put_num(e, w->num);
        sf_text(r, X_NUM, y, ES, b, EW, col);
        sf_text(r, X_ACT, y, ES, act_names[w->act], EW, col);
        if (w->act == MM2_TAPE || w->act == MM2_INSTR) put_num(b, w->val); else put_str(b, "--");
        sf_text(r, X_VAL, y, ES, b, EW, ed && i == cur ? c_green : col);
    }
}

/* ---- câblage ABI: au premier point d'entrée (les events n'arrivent qu'après enregistrement) ---- */
static uint8_t wired;
static void wire(void)
{
    if (wired) return;
    wired = 1;
    mm2_setting_get = mm_get;
    mm2_setting_put = mm_put;
    mm2_act_next = act_next;
    mm2_act_play = act_play;
    mm2_act_tape = act_tape;
    mm2_act_instr = act_instr;
    mm2_env_query = env_query;
}

/* ---- cycle de vie ---- */
static void event(int id, int32_t arg)
{
    wire();
    if (id == OP1_EV_BOOT_DONE) {
        /* après les built-ins (midiseq enregistre le sien ici aussi, kn_features.c BUILTIN avant
         * les fichiers): nous sommes le 2e switch = rangée blanche, MIDI SEQ reste vert */
        op1_switch(FEAT, "MIDI MAP");
        mm2_load();
        takes_dirty = 1;
    } else if (id == OP1_EV_MODE) {
        cur_mode = (uint8_t)arg;
    } else if (id == OP1_EV_TRANSPORT) {
        tape_raw = arg;
    } else if (id == OP1_EV_SERVICE) {
        drain();
    }
}

static const char *const claims[] = { "key:clock:white", 0 };
const op1_feature_t FEATURE_op1midi = {
    .api = OP1_FEATURE_API,
    .priority = 300,
    .name = FEAT,
    .event = event,
    .input = input,
    .midi_raw = midi_raw,
    .overlay = overlay,
    .claims = claims,
};

/* câblage ABI: voir wire() ci-dessus (lazy, au premier point d'entrée) */
