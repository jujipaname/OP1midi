/* Test hôte MM2: codec, persistance, dispatch CC/PC, gardes, actions, éditeur.
 * cc -O1 -fsanitize=address,undefined mm2_test.c mm2_core.c mm2_edit.c -o mm2_test && ./mm2_test */
#include "mm2.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ---- faux réglages (l'ABI d'intégration est des pointeurs de fonction) ---- */
static struct { const char *k; int v; } st[64];
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
    if (nst < 64) { st[nst].k = k; st[nst].v = v; nst++; }
}
static void st_clear(void) { nst = 0; }

/* ---- compteurs d'actions ---- */
static int n_next, n_play, n_tape, n_instr, last_dir, last_tape, last_instr;
static void act_next(int d) { n_next++; last_dir = d; }
static void act_play(void) { n_play++; }
static void act_tape(int t) { n_tape++; last_tape = t; }
static void act_instr(int s) { n_instr++; last_instr = s; }
static mm2_env ENV = { 1, 1, 0, 0, 3 };    /* tape mode, arrêt, pas de punch, pas de count-in, 3 marqueurs */
static void env_query(mm2_env *e) { *e = ENV; }

static void reset(void)
{
    st_clear();
    n_next = n_play = n_tape = n_instr = 0; last_dir = last_tape = last_instr = -99;
    mm2_setting_get = fake_get; mm2_setting_put = fake_put;
    mm2_act_next = act_next; mm2_act_play = act_play; mm2_act_tape = act_tape; mm2_act_instr = act_instr;
    mm2_env_query = env_query;
    mm2_enable(1);
    mm2_load();                                            /* tout à zéro: mapping inactif */
    if (mm2_edit_view()) mm2_edit_close(0);
}

int main(void)
{
    /* ---- codec ---- */
    mm2_row r = { 5, 1, 80, MM2_TAPE, 3 };
    int p = mm2_row_pack(&r);
    mm2_row q = mm2_row_unpack(p);
    assert(q.ch == 5 && q.type == 1 && q.num == 80 && q.act == MM2_TAPE && q.val == 3);
    mm2_row bad = { 0, 0, 0, MM2_OFF, 0 };
    assert(mm2_row_pack(&bad) >= 0);                       /* ligne vide valide (inactive) */
    mm2_row over = { 20, 0, 0, MM2_OFF, 0 };
    assert(mm2_row_pack(&over) == -1);
    mm2_row back = mm2_row_unpack(18);                     /* ch 18 > ALL: ligne invalidée */
    assert(back.act == MM2_OFF && back.ch == 0 && back.num == 0);

    /* ---- dispatch: rien de mappé -> rien de consommé ---- */
    reset();
    assert(!mm2_cc(0, 80, 127) && !mm2_pc(0, 5));

    /* ---- une ligne CC -> MARKER NEXT, canal 1 ---- */
    reset();
    mm2_row_edit(0)->ch = 1; mm2_row_edit(0)->type = 0; mm2_row_edit(0)->num = 80;
    mm2_row_edit(0)->act = MM2_NEXT;
    assert(!mm2_cc(1, 80, 127));                           /* canal 2 != 1 */
    assert(mm2_cc(0, 80, 127) && n_next == 1 && last_dir == 1);
    assert(mm2_cc(0, 80, 100) && n_next == 1);             /* tenu haut: pas de répétition */
    assert(mm2_cc(0, 80, 63) && n_next == 1);
    assert(mm2_cc(0, 80, 0) && mm2_cc(0, 80, 64) && n_next == 2);

    /* ---- PREV + gardes ---- */
    reset();
    mm2_row_edit(0)->act = MM2_PREV; mm2_row_edit(0)->num = 81; mm2_row_edit(0)->ch = 1;
    assert(mm2_cc(0, 81, 127) && n_next == 1 && last_dir == -1);
    ENV.tape_mode = 0; assert(mm2_cc(0, 81, 0) && mm2_cc(0, 81, 127) && n_next == 1);
    ENV.tape_mode = 1; ENV.markers = 0; assert(mm2_cc(0, 81, 0) && mm2_cc(0, 81, 127) && n_next == 1);
    ENV.markers = 3;

    /* ---- PLAY: gardes arrêt / punch / count-in ---- */
    reset();
    mm2_row_edit(0)->act = MM2_PLAY; mm2_row_edit(0)->num = 82; mm2_row_edit(0)->ch = 1;
    assert(mm2_cc(0, 82, 127) && n_play == 1);
    ENV.stopped = 0; assert(mm2_cc(0, 82, 0) && mm2_cc(0, 82, 127) && n_play == 1);
    ENV.stopped = 1; ENV.punch = 1; assert(mm2_cc(0, 82, 0) && mm2_cc(0, 82, 127) && n_play == 1);
    ENV.punch = 0; ENV.counting = 1; assert(mm2_cc(0, 82, 0) && mm2_cc(0, 82, 127) && n_play == 1);
    ENV.counting = 0;

    /* ---- TAPE (valeur 1..4) + INSTRUMENT ---- */
    reset();
    mm2_row_edit(0)->act = MM2_TAPE; mm2_row_edit(0)->num = 90; mm2_row_edit(0)->ch = 1;
    mm2_row_edit(0)->val = 3;
    assert(mm2_cc(0, 90, 127) && n_tape == 1 && last_tape == 3);
    mm2_row_edit(0)->val = 5;                           /* hors 1..4: ignoré */
    assert(mm2_cc(0, 90, 0) && mm2_cc(0, 90, 127) && n_tape == 1);
    mm2_row_edit(0)->act = MM2_INSTR; mm2_row_edit(0)->val = 12;
    assert(mm2_cc(0, 90, 0) && mm2_cc(0, 90, 127) && n_instr == 1 && last_instr == 12);

    /* ---- PC: chaque message déclenche ---- */
    reset();
    mm2_row_edit(0)->act = MM2_TAPE; mm2_row_edit(0)->type = 1; mm2_row_edit(0)->num = 4;
    mm2_row_edit(0)->ch = MM2_CH_ALL; mm2_row_edit(0)->val = 2;
    assert(mm2_pc(3, 4) && n_tape == 1 && last_tape == 2); /* ALL: n'importe quel canal */
    assert(mm2_pc(9, 4) && n_tape == 2);
    assert(!mm2_cc(0, 4, 127));                            /* ligne PC: pas vue par le chemin CC */

    /* ---- première ligne correspondante gagnante ---- */
    reset();
    mm2_row_edit(0)->act = MM2_NEXT; mm2_row_edit(0)->num = 7; mm2_row_edit(0)->ch = 1;
    mm2_row_edit(1)->act = MM2_PLAY; mm2_row_edit(1)->num = 7; mm2_row_edit(1)->ch = 1;
    assert(mm2_cc(0, 7, 127) && n_next == 1 && n_play == 0);

    /* ---- ON/OFF global ---- */
    reset();
    mm2_row_edit(0)->act = MM2_NEXT; mm2_row_edit(0)->num = 80; mm2_row_edit(0)->ch = 1;
    mm2_enable(0);
    assert(!mm2_cc(0, 80, 127) && n_next == 0);            /* off: rien n'est consommé */
    mm2_enable(1);
    assert(mm2_cc(0, 80, 127) && n_next == 1);

    /* ---- hooks absents: consommé sans crash ---- */
    reset();
    mm2_row_edit(0)->act = MM2_TAPE; mm2_row_edit(0)->num = 90; mm2_row_edit(0)->ch = 1;
    mm2_row_edit(0)->val = 2;
    mm2_act_tape = 0;
    assert(mm2_cc(0, 90, 127) && n_tape == 0);

    /* ---- persistance: save -> load ---- */
    reset();
    for (int i = 0; i < MM2_ROWS; i++) {
        mm2_row *w = mm2_row_edit(i);
        w->ch = (uint8_t)(i % 16 + 1); w->type = (uint8_t)(i & 1); w->num = (uint8_t)(i * 8);
        w->act = (uint8_t)(i % 6); w->val = (uint8_t)(i * 3);
    }
    mm2_enable(0);
    mm2_save();
    for (int i = 0; i < MM2_ROWS; i++) memset((void *)mm2_row_get(i), 0, sizeof(mm2_row));
    mm2_load();
    assert(!mm2_enabled());
    for (int i = 0; i < MM2_ROWS; i++) {
        const mm2_row *w = mm2_row_get(i);
        assert(w->ch == (uint8_t)(i % 16 + 1) && w->type == (uint8_t)(i & 1) && w->num == (uint8_t)(i * 8) &&
               w->act == (uint8_t)(i % 6) && w->val == (uint8_t)(i * 3));
    }

    /* ---- éditeur: ouvert, champs, curseur, pages ---- */
    reset();
    mm2_edit_open();
    assert(mm2_edit_view() && mm2_edit_cursor() == 0 && mm2_edit_page() == 0 && !mm2_edit_editing());
    mm2_edit_turn(3, 2 * MM2_DETENT);                      /* rouge: curseur -> ligne 2 */
    assert(mm2_edit_cursor() == 2 && mm2_edit_page() == 0);
    mm2_edit_turn(3, 3 * MM2_DETENT);                      /* -> ligne 5: page 1 */
    assert(mm2_edit_cursor() == 5 && mm2_edit_page() == 1);
    mm2_edit_turn(3, -13 * MM2_DETENT);                    /* clamp à 0 */
    assert(mm2_edit_cursor() == 0 && mm2_edit_page() == 0);
    mm2_edit_key(0x25, 1, 0);                              /* flèche droite: page 1 */
    assert(mm2_edit_page() == 1 && mm2_edit_cursor() == 4);
    mm2_edit_key(0x24, 1, 0);                              /* flèche gauche: page 0 */
    assert(mm2_edit_page() == 0 && mm2_edit_cursor() == 0);
    mm2_edit_key(0x24, 1, 0);                              /* wrap: page 3 */
    assert(mm2_edit_page() == 3 && mm2_edit_cursor() == 12);
    mm2_edit_key(0x25, 1, 0);                              /* wrap: page 0 */

    /* colonnes de la ligne 0 */
    mm2_edit_turn(0, 3 * MM2_DETENT);                      /* bleu: canal 1 -> 4 */
    assert(mm2_row_get(0)->ch == 4);
    mm2_edit_turn(0, -2 * MM2_DETENT);
    assert(mm2_row_get(0)->ch == 2);
    mm2_edit_turn(1, 5 * MM2_DETENT);                      /* vert: numéro 0 -> 5 */
    assert(mm2_row_get(0)->num == 5);
    assert(mm2_row_get(0)->type == 0);
    mm2_edit_key(0x3C, 1, 0);                              /* appui vert: PC */
    assert(mm2_row_get(0)->type == 1);
    mm2_edit_key(0x3C, 1, 0);                              /* retour CC */
    assert(mm2_row_get(0)->type == 0);
    mm2_edit_turn(2, 4 * MM2_DETENT);                      /* blanc: act OFF -> TAPE */
    assert(mm2_row_get(0)->act == MM2_TAPE);
    assert(!mm2_edit_editing());
    mm2_edit_key(0x3D, 1, 0);                              /* appui blanc: édition valeur */
    assert(mm2_edit_editing());
    mm2_edit_turn(2, MM2_DETENT);                          /* blanc: valeur 0 -> 1 */
    assert(mm2_row_get(0)->val == 1);
    mm2_edit_turn(2, 10 * MM2_DETENT);                     /* 1+10 -> wrap bas de 1..4 */
    assert(mm2_row_get(0)->val == 1);
    mm2_edit_turn(2, -1 * MM2_DETENT);                     /* wrap haut: 4 */
    assert(mm2_row_get(0)->val == 4);
    mm2_edit_key(0x3D, 1, 0);                              /* re-appui: valider */
    assert(!mm2_edit_editing());
    mm2_edit_turn(2, -1 * MM2_DETENT);                     /* blanc: TAPE -> PLAY */
    assert(mm2_row_get(0)->act == MM2_PLAY);
    mm2_edit_key(0x3D, 1, 0);                              /* appui blanc sur action sans valeur: rien */
    assert(!mm2_edit_editing());

    /* Shift + Help: valide + ferme */
    mm2_edit_key(0x00, 1, 1);
    assert(!mm2_edit_view());
    assert(fake_get("mmen", -1) == 1);
    char k[8] = "mmr0";
    int packed = fake_get(k, -1);
    mm2_row saved = mm2_row_unpack(packed);
    assert(saved.ch == 2 && saved.type == 0 && saved.num == 5 && saved.act == MM2_PLAY && saved.val == 4);

    /* éditeur fermé: les touches passent au stock */
    assert(!mm2_edit_key(0x25, 1, 0) && !mm2_edit_key(0x3C, 1, 0));

    /* ---- édition valeur d'un INSTRUMENT (0..255 wrap) ---- */
    reset();
    mm2_edit_open();
    mm2_edit_turn(2, 5 * MM2_DETENT);                      /* act = INSTR */
    assert(mm2_row_get(0)->act == MM2_INSTR);
    mm2_edit_key(0x3D, 1, 0);
    mm2_edit_turn(2, 300 * MM2_DETENT);                    /* 0+300 > 255 -> wrap à 0 */
    assert(mm2_row_get(0)->val == 0);
    mm2_edit_turn(2, -1 * MM2_DETENT);                     /* wrap bas: 255 */
    assert(mm2_row_get(0)->val == 255);
    mm2_edit_close(0);

    printf("mm2_test: ok\n");
    return 0;
}
