/* Copyright (C) 2026 OP1midi. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
/* Test hôte MM2 v3: codec, migration mmv, dispatch NOTE/CC/PC, gardes, destinations, canal/fade,
 * éditeur (édition live, Shift, learn, sorties). */
#include "mm2.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#define A2(c,m) do{ assert(c); (void)(m); }while(0)

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
static int last_snd, last_tape, last_mute, last_mlocal, last_fade;
static void act_snd(int k) { n_snd++; last_snd = k; }
static void act_tape(int t) { n_tape++; last_tape = t; }
static int last_fmask;
static void act_mute(int t, int f, int m) { n_mute++; last_mute = t; last_fade = f; last_fmask = m; }
static void act_marker(int local, int f, int m) { n_marker++; last_mlocal = local; last_fade = f; last_fmask = m; }
static void act_mnext(int f, int m) { n_mnext++; last_fade = f; last_fmask = m; }
static void act_mprev(int f, int m) { n_mprev++; last_fade = f; last_fmask = m; }
static mm2_env ENV = { 1, 3, 0, 5 };       /* mode tape, arrêt (3), pas de punch, 5 marqueurs */
static void env_query(mm2_env *e) { *e = ENV; }

static void reset(void)
{
    st_clear();
    n_snd = n_tape = n_mute = n_marker = n_mnext = n_mprev = 0;
    last_snd = last_tape = last_mute = last_mlocal = last_fade = last_fmask = -99;
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
    /* ---- codec v5 (ch + fade par ligne) ---- */
    mm2_row r = { 3, MM2_S_CC, 80, MM2_D_MRK, 45, 2, MM2_FM_12 };
    int p = mm2_row_pack(&r);
    mm2_row q = mm2_row_unpack(p);
    assert(q.ch == 3 && q.stype == MM2_S_CC && q.snum == 80 && q.dst == MM2_D_MRK &&
           q.dnum == 45 && q.fade == 2);
    mm2_row bad = { 0, 9, 0, 0, 0, 0 };
    assert(mm2_row_pack(&bad) == -1);
    mm2_row back = mm2_row_unpack(7 << 14);                /* dst 7 > MM2_D_MAX: invalidée */
    assert(back.dst == MM2_D_OFF && back.snum == 0 && back.dnum == 0);
    mm2_row back2 = mm2_row_unpack(3 << 5);                /* stype 3 > MM2_S_MAX: invalidée */
    assert(back2.dst == MM2_D_OFF && back2.stype == 0 && back2.snum == 0);
    mm2_row back3 = mm2_row_unpack(20);                    /* ch 20 > ALL: invalidée */
    assert(back3.ch == 0 && back3.dst == MM2_D_OFF);

    /* ---- dispatch: rien de mappé -> rien de consommé ---- */
    reset();
    assert(!mm2_cc(0, 80, 127) && !mm2_pc(0, 5) && !mm2_note(0, 60, 100));

    /* ---- ligne CC -> MARKER plat (ensemble partagé 1..32, tape courante) ---- */
    reset();
    ENV.markers = 20;
    mm2_row_edit(0)->stype = MM2_S_CC; mm2_row_edit(0)->snum = 80; mm2_row_edit(0)->dst = MM2_D_MRK;
    mm2_row_edit(0)->dnum = 5;
    /* ch de ligne non réglé (0) = ALL: le canal 2 aussi déclenche */
    assert(mm2_cc(1, 80, 127) && n_marker == 1 && last_mlocal == 5);
    assert(mm2_cc(0, 80, 0) && mm2_cc(0, 80, 127) && n_marker == 2 && last_mlocal == 5);
    assert(mm2_cc(0, 80, 100) && n_marker == 2);           /* tenu: pas de répétition */
    assert(mm2_cc(0, 80, 0) && mm2_cc(0, 80, 64) && n_marker == 3);

    /* ---- gardes marker: états / punch / marqueurs insuffisants ---- */
    reset();
    ENV.markers = 20;
    mm2_row_edit(0)->dst = MM2_D_MRK; mm2_row_edit(0)->stype = MM2_S_PC;
    mm2_row_edit(0)->snum = 7;
    mm2_row_edit(0)->dnum = 10;
    ENV.raw_state = 0;                                     /* arrêt variante 0: autorisé */
    assert(mm2_pc(0, 7) && n_marker == 1 && last_mlocal == 10);
    ENV.raw_state = 5;                                     /* enregistrement: refusé */
    assert(mm2_pc(0, 7) && n_marker == 1);
    ENV.raw_state = 6;                                     /* armé (count-in): refusé */
    assert(mm2_pc(0, 7) && n_marker == 1);
    ENV.raw_state = 10;                                    /* lecture: autorisé (fade/saut) */
    assert(mm2_pc(0, 7) && n_marker == 2 && last_mlocal == 10);
    ENV.raw_state = 3;
    ENV.tape_mode = 0; assert(mm2_pc(0, 7) && n_marker == 2); ENV.tape_mode = 1;
    ENV.punch = 1; assert(mm2_pc(0, 7) && n_marker == 2); ENV.punch = 0;
    mm2_row_edit(0)->dnum = 20;                            /* == ENV.markers: la borne passe */
    assert(mm2_pc(0, 7) && n_marker == 3 && last_mlocal == 20);
    mm2_row_edit(0)->dnum = 33;                            /* au-delà de l'ensemble partagé: ignoré */
    assert(mm2_pc(0, 7) && n_marker == 3);
    ENV.markers = 40;                                      /* plus de 32 posés: la plage prime */
    assert(mm2_pc(0, 7) && n_marker == 3);
    ENV.markers = 1;
    mm2_row_edit(0)->dnum = 2;                             /* 2 > 1 marqueur posé: ignoré */
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

    /* ---- ON/OFF global ---- */
    reset();
    mm2_row_edit(0)->stype = MM2_S_CC; mm2_row_edit(0)->snum = 80; mm2_row_edit(0)->dst = MM2_D_MRP;
    mm2_enable(0);
    assert(!mm2_cc(0, 80, 127) && n_mnext == 0);
    mm2_enable(1);
    assert(mm2_cc(0, 80, 127) && n_mnext == 1);

    /* ---- canal PAR LIGNE (0 = non réglé: ALL) ---- */
    reset();
    mm2_row_edit(0)->stype = MM2_S_CC; mm2_row_edit(0)->snum = 80; mm2_row_edit(0)->dst = MM2_D_MRP;
    /* ch non réglé (0) -> ALL */
    assert(mm2_cc(5, 80, 127) && n_mnext == 1);
    mm2_row_edit(0)->ch = 4;                               /* ligne sur le canal 4 */
    assert(!mm2_cc(0, 80, 0) && !mm2_cc(2, 80, 127) && mm2_cc(3, 80, 0) && mm2_cc(3, 80, 64) && n_mnext == 2);
    mm2_row_edit(0)->ch = MM2_CH_ALL;
    assert(mm2_cc(9, 80, 0) && mm2_cc(9, 80, 127) && n_mnext == 3);
    mm2_row_edit(0)->ch = 5;                               /* ligne 0: canal 6 (hors chemin) */
    mm2_row_edit(1)->stype = MM2_S_CC; mm2_row_edit(1)->snum = 80; mm2_row_edit(1)->dst = MM2_D_MRP;
    mm2_row_edit(1)->ch = 2;                               /* ligne 1: canal 2 */
    assert(!mm2_cc(0, 80, 0) && mm2_cc(1, 80, 0) && mm2_cc(1, 80, 127) && n_mnext == 4);
    mm2_row_edit(0)->ch = 0; mm2_row_edit(1)->ch = 0;      /* retour ALL pour la suite */

    /* ---- fade PAR LIGNE (hooks reçoivent la valeur de la ligne) ---- */
    reset();
    mm2_row_edit(0)->stype = MM2_S_CC; mm2_row_edit(0)->snum = 90; mm2_row_edit(0)->dst = MM2_D_MUTE;
    mm2_row_edit(0)->dnum = 2; mm2_row_edit(0)->fade = 3;
    assert(mm2_cc(0, 90, 127) && n_mute == 1 && last_mute == 2 && last_fade == 3);
    mm2_row_edit(0)->dst = MM2_D_MRP; mm2_row_edit(0)->fade = 1;
    assert(mm2_cc(0, 90, 0) && mm2_cc(0, 90, 127) && n_mnext == 1 && last_fade == 1);
    mm2_row_edit(0)->dst = MM2_D_MRK; mm2_row_edit(0)->dnum = 3; mm2_row_edit(0)->fade = 4;
    assert(mm2_cc(0, 90, 0) && mm2_cc(0, 90, 127) && n_marker == 1 && last_fade == 4);
    mm2_row_edit(0)->dst = MM2_D_SND; mm2_row_edit(0)->dnum = 2; mm2_row_edit(0)->fade = 4;
    assert(mm2_cc(0, 90, 0) && mm2_cc(0, 90, 127) && n_snd == 1);   /* SND: pas de fade */

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
        w->ch = (uint8_t)(i % 17); w->stype = (uint8_t)(i % 3); w->snum = (uint8_t)(i * 8);
        w->dst = (uint8_t)(i % 7); w->dnum = (uint8_t)(i * 3 + 1); w->fade = (uint8_t)(i % 5); w->fmask = (uint8_t)(i % 7);
    }
    mm2_save();
    for (int i = 0; i < MM2_ROWS; i++) memset((void *)mm2_row_get(i), 0, sizeof(mm2_row));
    mm2_load();
    for (int i = 0; i < MM2_ROWS; i++) {
        const mm2_row *w = mm2_row_get(i);
        assert(w->ch == (uint8_t)(i % 17) && w->stype == (uint8_t)(i % 3) && w->snum == (uint8_t)(i * 8) &&
               w->dst == (uint8_t)(i % 7) && w->dnum == (uint8_t)(i * 3 + 1) && w->fade == (uint8_t)(i % 5) &&
               w->fmask == (uint8_t)(i % 7));
    }
    fake_put("mmv", 3);                                    /* ancien format: remise à zéro au load */
    mm2_load();
    assert(mm2_row_get(0)->dst == MM2_D_OFF && fake_get("mmv", -1) == MM2_FMT);
    for (int i = 0; i < MM2_ROWS; i++) assert(mm2_row_get(i)->dst == MM2_D_OFF);

    /* ---- éditeur v6: bleu=curseur, vert=source+canal+learn, blanc=destination+key-learn+delete, rouge=fade ---- */
    reset();
    mm2_edit_open();
    A2(mm2_edit_view() && mm2_edit_cursor()==0 && mm2_edit_learn_state()==MM2_L_NONE, "éditeur ouvert");
    mm2_edit_turn(0,6*MM2_DETENT,0); A2(mm2_edit_cursor()==6&&mm2_edit_page()==1,"bleu +6: slot 7 page 2");
    mm2_edit_turn(0,-6*MM2_DETENT,0); A2(mm2_edit_cursor()==0,"bleu -6: retour slot 1");
    mm2_edit_turn(1,3*MM2_DETENT,0); A2(mm2_row_get(0)->snum==3,"vert: snum 3");
    mm2_edit_key(0x3C,1,0); A2(mm2_row_get(0)->stype==MM2_S_CC,"vert clic: NOTE->CC");
    mm2_edit_key(0x3C,1,0); mm2_edit_key(0x3C,1,0);
    A2(mm2_row_get(0)->stype==MM2_S_NOTE,"vert clic x3: wrap PC->NOTE");
    mm2_edit_turn(2,1*MM2_DETENT,0);
    A2(mm2_row_get(0)->dst==MM2_D_SND&&mm2_row_get(0)->dnum==1&&mm2_row_get(0)->ch==MM2_CH_ALL,
       "blanc +1: SND 1 + ch ALL auto");
    mm2_edit_turn(2,7*MM2_DETENT,0); A2(mm2_row_get(0)->dst==MM2_D_SND&&mm2_row_get(0)->dnum==8,"blanc +7: SND 8");
    mm2_edit_turn(2,1*MM2_DETENT,0); A2(mm2_row_get(0)->dst==MM2_D_TAPE&&mm2_row_get(0)->dnum==1,"blanc: SND8->TAPE 1");
    mm2_edit_turn(2,4*MM2_DETENT,0); A2(mm2_row_get(0)->dst==MM2_D_MUTE&&mm2_row_get(0)->dnum==1,"blanc: TAPE4->MUT 1");
    mm2_edit_turn(2,4*MM2_DETENT,0); A2(mm2_row_get(0)->dst==MM2_D_MRK&&mm2_row_get(0)->dnum==1,"blanc: MUT1->MRK 1");
    mm2_edit_turn(2,31*MM2_DETENT,0); A2(mm2_row_get(0)->dst==MM2_D_MRK&&mm2_row_get(0)->dnum==32,"blanc +31: MRK 32");
    mm2_edit_turn(2,2*MM2_DETENT,0); A2(mm2_row_get(0)->dst==MM2_D_MRM,"blanc +2: MRK32->M+->M-");
    mm2_edit_turn(2,-1*MM2_DETENT,0); A2(mm2_row_get(0)->dst==MM2_D_MRP,"blanc -1: M-->M+");
    mm2_edit_turn(2,3*MM2_DETENT,0); A2(mm2_row_get(0)->dst==MM2_D_SND&&mm2_row_get(0)->dnum==1,"blanc +3: M+->M-->OFF->SND1");
    mm2_edit_turn(3,1*MM2_DETENT,0); A2(mm2_row_get(0)->fade==1,"rouge: fade ligne 0->1");
    mm2_edit_turn(3,4*MM2_DETENT,0); A2(mm2_row_get(0)->fade==0,"rouge +4: fade wrap -> 0");
    mm2_edit_turn(3,-1*MM2_DETENT,0); A2(mm2_row_get(0)->fade==4,"rouge -1: fade 0->32s");
    mm2_edit_turn(3,1*MM2_DETENT,0); A2(mm2_row_get(0)->fade==0,"retour OFF");
    mm2_edit_turn(1,1*MM2_DETENT,1); A2(mm2_row_get(0)->ch==1,"Shift+vert: canal ALL->1");
    mm2_edit_turn(1,16*MM2_DETENT,1); A2(mm2_row_get(0)->ch==MM2_CH_ALL,"canal: 1->...->ALL (16 crans)");
    mm2_edit_turn(1,1*MM2_DETENT,1); A2(mm2_row_get(0)->ch==1,"canal: ALL->1 (wrap)");
    mm2_edit_turn(1,-1*MM2_DETENT,1); A2(mm2_row_get(0)->ch==MM2_CH_ALL,"canal: 1->ALL (sens arrière)");
    /* learn MIDI: Shift+clic vert arme, clic vert seul désarme */
    mm2_edit_key(0x3C,1,1); A2(mm2_edit_learn_state()==MM2_L_MIDI,"learn MIDI armé (Shift+clic vert)");
    mm2_edit_key(0x3C,1,1); A2(mm2_edit_learn_state()==MM2_L_NONE,"learn MIDI désarmé (2e Shift+clic)");
    mm2_edit_key(0x3C,1,1); A2(mm2_edit_learn_state()==MM2_L_MIDI,"réarmé");
    mm2_edit_key(0x3C,1,0); A2(mm2_edit_learn_state()==MM2_L_NONE,"clic vert SEUL: sortie du learn");
    mm2_edit_key(0x3C,1,1); A2(mm2_edit_learn_state()==MM2_L_MIDI,"réarmé (feed)");
    mm2_edit_learn_feed(MM2_S_CC,44); A2(mm2_edit_learn_state()==MM2_L_NONE&&mm2_row_get(0)->snum==44&&
       mm2_row_get(0)->stype==MM2_S_CC,"learn capture CC44");
    A2(mm2_row_get(0)->dst==MM2_D_SND&&mm2_row_get(0)->ch==MM2_CH_ALL,"learn: garde dst + canal");
    /* learn touche: Shift+clic blanc arme (dst SND), clic blanc seul désarme */
    mm2_edit_key(0x3D,1,1); A2(mm2_edit_learn_state()==MM2_L_KEY,"learn touche armé (Shift+clic blanc)");
    mm2_edit_learn_feed_key(7); A2(mm2_edit_learn_state()==MM2_L_NONE&&mm2_row_get(0)->dnum==7,"learn touche: slot 7");
    mm2_edit_key(0x3D,1,1); A2(mm2_edit_learn_state()==MM2_L_KEY,"réarmé (touche)");
    mm2_edit_key(0x3D,1,0); A2(mm2_edit_learn_state()==MM2_L_NONE,"clic blanc SEUL: sortie du key learn");
    /* Shift+clic blanc hors SND: rien */
    mm2_edit_turn(2,-1*MM2_DETENT,0); A2(mm2_row_get(0)->dst==MM2_D_SND&&mm2_row_get(0)->dnum==6,"vert -1: SND7->SND6");
    mm2_edit_turn(2,3*MM2_DETENT,0); A2(mm2_row_get(0)->dst==MM2_D_TAPE&&mm2_row_get(0)->dnum==1,"vert +3: SND6->TAPE 1 (hors SND)");
    mm2_edit_key(0x3D,1,1); A2(mm2_edit_learn_state()==MM2_L_NONE,"Shift+clic blanc hors SND: pas de key learn");
    /* Shift+clic rouge: supprimer */
    mm2_edit_key(0x3E,1,1);
    A2(mm2_row_get(0)->dst==MM2_D_OFF&&mm2_row_get(0)->snum==0&&mm2_row_get(0)->stype==0&&
       mm2_row_get(0)->dnum==0&&mm2_row_get(0)->ch==0&&mm2_row_get(0)->fade==0&&mm2_row_get(0)->fmask==0,
       "Shift+clic rouge: ligne supprimée (tout à zéro)");
    mm2_edit_key(0x3E,1,0);                                /* release: avalée */
    A2(mm2_edit_key(0x3E,1,0)==1&&mm2_edit_view(),"rouge clic sans shift: consommé (rien sans shift)");
    A2(mm2_edit_key(0x3B,1,0)==0&&mm2_edit_view(),"bleu clic sans shift: 0 (la glue avale)");
    A2(mm2_edit_key(0x25,1,0)==1&&mm2_edit_view(),"flèche droite en éditeur: page suivante");
    A2(mm2_edit_key(0x24,1,0)==1&&mm2_edit_page()==0,"flèche gauche: page 1");
    A2(mm2_edit_key(0x00,1,0)==1&&!mm2_edit_view(),"Help: ferme (consommé)");
    mm2_edit_open();
    A2(mm2_edit_key(0x04,1,0)==0&&!mm2_edit_view(),"sortie TAPE sans shift: passthrough");
    assert(fake_get("mmr0",-1)>=0);
    mm2_edit_open();
    A2(mm2_edit_key(0x04,1,1)==1&&!mm2_edit_view(),"sortie TAPE avec shift: consommée");
    A2(!mm2_edit_key(0x25,1,0)&&!mm2_edit_key(0x3B,1,0)&&!mm2_edit_key(0x3C,1,0)&&!mm2_edit_key(0x3D,1,0),"éditeur fermé: touches passent");

    printf("mm2_test: ok\n");
    return 0;
}
/* (tests swallow pipeline ajoutés ci-dessous par sed) */
