/* Copyright (C) 2026 OP1midi. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
/* Test hôte MM2 v3: codec, migration mmv, dispatch NOTE/CC/PC, gardes, destinations, canal/fade,
 * éditeur (édition live, Shift, learn, sorties). */
#include "mm2.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ---- faux réglages ---- */
static struct { const char *k; int v; } st[80];
static int nst;
static int fake_get(const char *k, int d)
{
    for (int i = 0; i < nst; i++) {
        const char *a = st[i].k, *b = k;
        while (*a && *a == *b) a++, b++;
        if (!*a && !*b) return st[i].v;
    }
    return d;
}
static void fake_put(const char *k, int v)
{
    for (int i = 0; i < nst; i++) {
        const char *a = st[i].k, *b = k;
        while (*a && *a == *b) a++, b++;
        if (!*a && !*b) { st[i].v = v; return; }
    }
    if (nst < 80) { st[nst].k = k; st[nst].v = v; nst++; }
}
static void st_clear(void) { nst = 0; }

/* ---- compteurs d'actions ---- */
static int n_snd, n_tape, n_mute, n_marker, n_mnext, n_mprev;
static int last_snd, last_tape, last_mute, last_mtape, last_mlocal;
static void act_snd(int k) { n_snd++; last_snd = k; }
static void act_tape(int t) { n_tape++; last_tape = t; }
static void act_mute(int t) { n_mute++; last_mute = t; }
static void act_marker(int tape, int local) { n_marker++; last_mtape = tape; last_mlocal = local; }
static void act_mnext(void) { n_mnext++; }
static void act_mprev(void) { n_mprev++; }
static mm2_env ENV = { 1, 3, 0, 5 };       /* mode tape, arrêt (3), pas de punch, 5 marqueurs */
static void env_query(mm2_env *e) { *e = ENV; }

static void reset(void)
{
    st_clear();
    n_snd = n_tape = n_mute = n_marker = n_mnext = n_mprev = 0;
    last_snd = last_tape = last_mute = last_mtape = last_mlocal = -99;
    mm2_setting_get = fake_get; mm2_setting_put = fake_put;
    mm2_act_snd = act_snd; mm2_act_tape = act_tape; mm2_act_mute = act_mute;
    mm2_act_marker = act_marker; mm2_act_mnext = act_mnext; mm2_act_mprev = act_mprev;
    mm2_env_query = env_query;
    mm2_enable(1);
    mm2_load();                                            /* tout à zéro: mapping inactif */
    if (mm2_edit_view()) mm2_edit_close(0);
}

int main(void)
{
    /* ---- codec v3 ---- */
    mm2_row r = { MM2_S_CC, 80, MM2_D_MRK, 45 };
    int p = mm2_row_pack(&r);
    mm2_row q = mm2_row_unpack(p);
    assert(q.stype == MM2_S_CC && q.snum == 80 && q.dst == MM2_D_MRK && q.dnum == 45);
    mm2_row bad = { 9, 0, 0, 0 };
    assert(mm2_row_pack(&bad) == -1);
    mm2_row back = mm2_row_unpack(7 << 9);                 /* dst 7 > MM2_D_MAX: ligne invalidée */
    assert(back.dst == MM2_D_OFF && back.snum == 0 && back.dnum == 0);
    mm2_row back2 = mm2_row_unpack(3);                     /* stype 3 > MM2_S_MAX: invalidée */
    assert(back2.dst == MM2_D_OFF && back2.stype == 0 && back2.snum == 0);

    /* ---- dispatch: rien de mappé -> rien de consommé ---- */
    reset();
    assert(!mm2_cc(0, 80, 127) && !mm2_pc(0, 5) && !mm2_note(0, 60, 100));

    /* ---- ligne CC -> MARKER global 45: tape 2, local 14 ---- */
    reset();
    ENV.markers = 20;                                      /* assez de marqueurs pour local 14 */
    mm2_row_edit(0)->stype = MM2_S_CC; mm2_row_edit(0)->snum = 80; mm2_row_edit(0)->dst = MM2_D_MRK;
    mm2_row_edit(0)->dnum = 45;
    assert(!mm2_cc(1, 80, 127));                           /* canal 2 != 1 */
    assert(mm2_cc(0, 80, 127) && n_marker == 1 && last_mtape == 2 && last_mlocal == 14);
    assert(mm2_cc(0, 80, 100) && n_marker == 1);           /* tenu: pas de répétition */
    assert(mm2_cc(0, 80, 0) && mm2_cc(0, 80, 64) && n_marker == 2);

    /* ---- gardes marker: états / punch / marqueurs insuffisants ---- */
    reset();
    mm2_row_edit(0)->dst = MM2_D_MRK; mm2_row_edit(0)->dnum = 40; mm2_row_edit(0)->stype = MM2_S_PC;
    mm2_row_edit(0)->snum = 7;
    ENV.raw_state = 5;                                     /* enregistrement: refusé */
    assert(mm2_pc(0, 7) && n_marker == 0);
    ENV.raw_state = 6;                                     /* armé (count-in): refusé */
    assert(mm2_pc(0, 7) && n_marker == 0);
    ENV.raw_state = 10;                                    /* lecture: autorisé (fade/saut) */
    assert(mm2_pc(0, 7) && n_marker == 1 && last_mtape == 2 && last_mlocal == 9);
    ENV.raw_state = 3;
    ENV.tape_mode = 0; assert(mm2_pc(0, 7) && n_marker == 1); ENV.tape_mode = 1;
    ENV.punch = 1; assert(mm2_pc(0, 7) && n_marker == 1); ENV.punch = 0;
    mm2_row_edit(0)->dnum = 113;                           /* tape 4, local 20 (= ENV.markers) */
    assert(mm2_pc(0, 7) && n_marker == 2 && last_mtape == 4 && last_mlocal == 20);
    mm2_row_edit(0)->dnum = 95;                            /* tape 4, local 2 */
    assert(mm2_pc(0, 7) && n_marker == 3 && last_mtape == 4 && last_mlocal == 2);
    ENV.markers = 1;
    mm2_row_edit(0)->dnum = 33;                            /* tape 2, local 2 > 1 marqueur: ignoré */
    assert(mm2_pc(0, 7) && n_marker == 3);
    ENV.markers = 5;

    /* ---- MRK+ / MRK- ---- */
    reset();
    mm2_row_edit(0)->stype = MM2_S_NOTE; mm2_row_edit(0)->snum = 60; mm2_row_edit(0)->dst = MM2_D_MRP;
    assert(mm2_note(0, 60, 100) && n_mnext == 1);
    assert(mm2_note(0, 60, 0) && n_mnext == 1);            /* vel 0 = note-off mappée: avalé, rien */
    mm2_row_edit(1)->stype = MM2_S_NOTE; mm2_row_edit(1)->snum = 61; mm2_row_edit(1)->dst = MM2_D_MRM;
    assert(mm2_note(0, 61, 100) && n_mprev == 1);
    ENV.raw_state = 10;
    assert(mm2_note(0, 60, 100) && n_mnext == 2);          /* marche aussi en lecture */
    ENV.raw_state = 3;
    ENV.markers = 0; assert(mm2_note(0, 60, 100) && n_mnext == 2); ENV.markers = 5;

    /* ---- SND / TAPE / MUTE + plages ---- */
    reset();
    mm2_row_edit(0)->stype = MM2_S_CC; mm2_row_edit(0)->snum = 90; mm2_row_edit(0)->dst = MM2_D_SND;
    mm2_row_edit(0)->dnum = 3;
    assert(mm2_cc(0, 90, 127) && n_snd == 1 && last_snd == 3);
    mm2_row_edit(0)->dnum = 9;                             /* hors 1..8: ignoré */
    assert(mm2_cc(0, 90, 0) && mm2_cc(0, 90, 127) && n_snd == 1);
    mm2_row_edit(0)->dst = MM2_D_TAPE; mm2_row_edit(0)->dnum = 4;
    assert(mm2_cc(0, 90, 0) && mm2_cc(0, 90, 127) && n_tape == 1 && last_tape == 4);
    mm2_row_edit(0)->dst = MM2_D_MUTE; mm2_row_edit(0)->dnum = 2;
    assert(mm2_cc(0, 90, 0) && mm2_cc(0, 90, 127) && n_mute == 1 && last_mute == 2);

    /* ---- NOTE / PC déclenchent chacun leur chemin ---- */
    reset();
    mm2_row_edit(0)->stype = MM2_S_CC; mm2_row_edit(0)->snum = 4; mm2_row_edit(0)->dst = MM2_D_TAPE;
    mm2_row_edit(0)->dnum = 1;
    assert(!mm2_note(0, 4, 100) && !mm2_pc(0, 4));         /* ligne CC: ni note ni PC */
    mm2_row_edit(0)->stype = MM2_S_PC;
    assert(mm2_pc(0, 4) && n_tape == 1);                   /* PC: chaque message */
    assert(mm2_pc(0, 4) && n_tape == 2);
    mm2_row_edit(0)->stype = MM2_S_NOTE;
    assert(mm2_note(0, 4, 1) && n_tape == 3);

    /* ---- première ligne correspondante gagnante ---- */
    reset();
    mm2_row_edit(0)->stype = MM2_S_CC; mm2_row_edit(0)->snum = 7; mm2_row_edit(0)->dst = MM2_D_MRP;
    mm2_row_edit(1)->stype = MM2_S_CC; mm2_row_edit(1)->snum = 7; mm2_row_edit(1)->dst = MM2_D_TAPE;
    mm2_row_edit(1)->dnum = 2;
    assert(mm2_cc(0, 7, 127) && n_mnext == 1 && n_tape == 0);

    /* ---- ON/OFF global + canal ---- */
    reset();
    mm2_row_edit(0)->stype = MM2_S_CC; mm2_row_edit(0)->snum = 80; mm2_row_edit(0)->dst = MM2_D_MRP;
    mm2_enable(0);
    assert(!mm2_cc(0, 80, 127) && n_mnext == 0);
    mm2_enable(1);
    assert(mm2_cc(0, 80, 127) && n_mnext == 1);
    mm2_save();
    mm2_load();                                            /* mmch par défaut = 1 (lignes conservées) */
    assert(mm2_channel() == 1);
    assert(!mm2_cc(2, 80, 127) && mm2_cc(0, 80, 127));
    mm2_ch_step(1);                                        /* 1 -> 2 */
    assert(mm2_channel() == 2 && !mm2_cc(0, 80, 10) && mm2_cc(1, 80, 127));
    for (int i = 0; i < 15; i++) mm2_ch_step(1);           /* -> ALL (17) */
    assert(mm2_channel() == MM2_CH_ALL && mm2_cc(9, 80, 5) && n_mnext > 0);
    mm2_ch_step(1);                                        /* ALL -> OFF */
    assert(mm2_channel() == MM2_CH_OFF && !mm2_cc(0, 80, 127));
    mm2_ch_step(-1);                                       /* OFF -> ALL */
    assert(mm2_channel() == MM2_CH_ALL);
    mm2_ch_step(-1);                                       /* ALL -> 16 */
    assert(mm2_channel() == 16 && fake_get("mmch", -1) == 16);

    /* ---- fade: cycle + persistance ---- */
    reset();
    assert(mm2_fade() == 0);
    mm2_fade_step(1); assert(mm2_fade() == 1);
    for (int i = 0; i < 3; i++) mm2_fade_step(1);
    assert(mm2_fade() == 4);
    mm2_fade_step(1); assert(mm2_fade() == 0);
    mm2_fade_step(-1); assert(mm2_fade() == 4);
    assert(fake_get("mmfade", -1) == 4);

    /* ---- hooks absents: consommé sans crash ---- */
    reset();
    mm2_row_edit(0)->stype = MM2_S_CC; mm2_row_edit(0)->snum = 90; mm2_row_edit(0)->dst = MM2_D_TAPE;
    mm2_row_edit(0)->dnum = 1;
    mm2_act_tape = 0;
    assert(mm2_cc(0, 90, 127) && n_tape == 0);

    /* ---- persistance + migration mmv ---- */
    reset();
    for (int i = 0; i < MM2_ROWS; i++) {
        mm2_row *w = mm2_row_edit(i);
        w->stype = (uint8_t)(i % 3); w->snum = (uint8_t)(i * 8);
        w->dst = (uint8_t)(i % 7); w->dnum = (uint8_t)(i * 3 + 1);
    }
    mm2_save();
    for (int i = 0; i < MM2_ROWS; i++) memset((void *)mm2_row_get(i), 0, sizeof(mm2_row));
    mm2_load();
    for (int i = 0; i < MM2_ROWS; i++) {
        const mm2_row *w = mm2_row_get(i);
        assert(w->stype == (uint8_t)(i % 3) && w->snum == (uint8_t)(i * 8) &&
               w->dst == (uint8_t)(i % 7) && w->dnum == (uint8_t)(i * 3 + 1));
    }
    fake_put("mmv", 2);                                    /* ancien format: remise à zéro au load */
    mm2_load();
    assert(mm2_row_get(0)->dst == MM2_D_OFF && fake_get("mmv", -1) == MM2_FMT);
    for (int i = 0; i < MM2_ROWS; i++) assert(mm2_row_get(i)->dst == MM2_D_OFF);

    /* ---- éditeur v3: édition live ---- */
    reset();
    mm2_edit_open();
    assert(mm2_edit_view() && mm2_edit_cursor() == 0 && !mm2_edit_learning());
    mm2_edit_turn(0, 3 * MM2_DETENT, 0);                   /* bleu: n° source 0 -> 3 */
    assert(mm2_row_get(0)->snum == 3);
    mm2_edit_key(0x3B, 1, 0);                              /* clic bleu: type NOTE -> CC */
    assert(mm2_row_get(0)->stype == MM2_S_CC);
    mm2_edit_key(0x3B, 1, 0);
    assert(mm2_row_get(0)->stype == MM2_S_PC);
    mm2_edit_key(0x3B, 1, 0);
    assert(mm2_row_get(0)->stype == MM2_S_NOTE);           /* wrap PC -> NOTE */
    mm2_edit_turn(1, 2 * MM2_DETENT, 0);                   /* vert: OFF -> SND -> TAPE */
    assert(mm2_row_get(0)->dst == MM2_D_TAPE);
    mm2_edit_turn(2, 5 * MM2_DETENT, 0);                   /* blanc: 0 -> 1 2 3 4 1 (cyclic) */
    assert(mm2_row_get(0)->dnum == 1);
    mm2_edit_turn(1, -1 * MM2_DETENT, 0);                  /* vert arrière: TAPE -> SND */
    assert(mm2_row_get(0)->dst == MM2_D_SND);
    mm2_edit_turn(2, -1 * MM2_DETENT, 0);                  /* blanc: 2 -> wrap haut 8 */
    assert(mm2_row_get(0)->dnum == 8);
    mm2_edit_turn(1, 6 * MM2_DETENT, 0);                   /* vert: SND->TAPE->MUT->MRK->M+->M-->OFF */
    assert(mm2_row_get(0)->dst == MM2_D_OFF);
    mm2_edit_turn(1, -1 * MM2_DETENT, 0);
    assert(mm2_row_get(0)->dst == MM2_D_MRM);
    mm2_edit_turn(2, 3 * MM2_DETENT, 0);                   /* blanc sur M-: pas de plage -> rien */
    assert(mm2_row_get(0)->dnum == 8);                     /* (valeur inchangée depuis SND) */

    /* Shift + vert: canal; Shift + blanc: fade */
    reset();
    mm2_edit_open();
    mm2_edit_turn(1, 1 * MM2_DETENT, 1);                   /* Shift+vert: canal 1 -> 2 */
    assert(mm2_channel() == 2 && fake_get("mmch", -1) == 2);
    mm2_edit_turn(2, 1 * MM2_DETENT, 1);                   /* Shift+blanc: fade 0 -> 1 */
    assert(mm2_fade() == 1 && fake_get("mmfade", -1) == 1);

    /* Shift + clic bleu: MIDI learn */
    reset();
    mm2_edit_open();
    mm2_edit_turn(3, 5 * MM2_DETENT, 0);                   /* curseur -> ligne 5 */
    assert(!mm2_edit_learning());
    assert(mm2_edit_key(0x3B, 1, 1) == 1 && mm2_edit_learning());
    mm2_edit_learn_feed(MM2_S_CC, 44);                     /* le prochain CC appris */
    assert(!mm2_edit_learning());
    assert(mm2_row_get(5)->stype == MM2_S_CC && mm2_row_get(5)->snum == 44);
    assert(mm2_row_get(5)->dst == MM2_D_TAPE);             /* ligne vide: destination par défaut */
    mm2_edit_learn_feed(MM2_S_PC, 9);                      /* désarmé: rien */
    assert(mm2_row_get(5)->snum == 44);
    /* learn sur une ligne déjà destinationnée: garde la destination */
    mm2_edit_turn(1, -1 * MM2_DETENT, 0);                  /* ligne 5: TAPE -> SND */
    assert(mm2_row_get(5)->dst == MM2_D_SND);
    assert(mm2_edit_key(0x3B, 1, 1) == 1 && mm2_edit_learning());
    mm2_edit_learn_feed(MM2_S_NOTE, 42);
    assert(mm2_row_get(5)->stype == MM2_S_NOTE && mm2_row_get(5)->dst == MM2_D_SND);

    /* curseur + pages */
    reset();
    mm2_edit_open();
    mm2_edit_turn(3, 6 * MM2_DETENT, 0);
    assert(mm2_edit_cursor() == 6 && mm2_edit_page() == 1);
    mm2_edit_key(0x24, 1, 0);
    assert(mm2_edit_page() == 0 && mm2_edit_cursor() == 0);
    mm2_edit_key(0x25, 1, 0);
    assert(mm2_edit_page() == 1 && mm2_edit_cursor() == 4);

    /* sorties: help seul, mode sans Shift (passthrough), mode avec Shift (consommé) */
    reset();
    mm2_edit_open();
    assert(mm2_edit_key(0x00, 1, 0) == 1 && !mm2_edit_view());
    reset();
    mm2_edit_open();
    mm2_row_edit(0)->dst = MM2_D_MRP;
    assert(mm2_edit_key(0x04, 1, 0) == 0 && !mm2_edit_view());
    assert(fake_get("mmr0", -1) >= 0);                     /* sauvegardé quand même */
    reset();
    mm2_edit_open();
    assert(mm2_edit_key(0x04, 1, 1) == 1 && !mm2_edit_view());
    reset();
    mm2_edit_open();
    assert(mm2_edit_key(0x22, 1, 0) == 0 && !mm2_edit_view());
    reset();
    mm2_edit_open();
    assert(mm2_edit_key(0x3E, 1, 0) == 0 && mm2_edit_view());   /* non-mode: 0 (modal côté glue) */
    mm2_edit_close(0);
    assert(!mm2_edit_key(0x25, 1, 0) && !mm2_edit_key(0x3B, 1, 0));

    /* ---- swallow pipeline: mm2_note_match ---- */
    reset();
    mm2_row_edit(0)->stype = MM2_S_NOTE; mm2_row_edit(0)->snum = 60; mm2_row_edit(0)->dst = MM2_D_MRP;
    assert(mm2_note_match(0, 60) && !mm2_note_match(1, 60) && !mm2_note_match(0, 61));
    mm2_enable(0);
    assert(!mm2_note_match(0, 60));
    mm2_enable(1);
    mm2_ch_step(1);                                        /* canal 2 */
    assert(!mm2_note_match(0, 60) && mm2_note_match(1, 60));

    printf("mm2_test: ok\n");
    return 0;
}
/* (tests swallow pipeline ajoutés ci-dessous par sed) */
