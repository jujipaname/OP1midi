/* MM2 — mapping MIDI généraliste pour OP-1 OG (OP1midi).
 * Module portable, sans dépendance op1r/Hundekraft: l'intégration câble les pointeurs
 * ci-dessous (settings, actions, sondes d'environnement), puis route les événements.
 *
 * 16 lignes de mapping. Chaque ligne: (canal, type CC/PC, numéro) -> (action, valeur).
 * CC: trigger front montant >= 64, pas de répétition tant que tenu. PC: chaque message.
 * Première ligne correspondante gagnante. Ligne inactive: act == MM2_OFF.
 *
 * Éditeur (mm2_edit.c): machine à états pure, sans rendu. Le rendu lit l'état
 * (mm2_edit_cursor/page/editing, mm2_row) — ligne courante en rouge, autres en blanc.
 *   bleu tourne = canal; vert tourne = numéro, appui vert = CC<->PC;
 *   blanc tourne = action (sinon valeur en édition), appui blanc = édition valeur on/off;
 *   rouge tourne = curseur (16 lignes, page suit); flèches 0x24/0x25 = pages;
 *   Shift + Help (0x00 + shift) = valider (mm2_save) + fermer. */
#ifndef MM2_H
#define MM2_H
#include <stdint.h>

#define MM2_ROWS    16
#define MM2_CH_ALL  17                /* canal "tous" d'une ligne */
#define MM2_DETENT  1024              /* delta d'encodeur par cran (convention op1r) */

enum { MM2_OFF = 0, MM2_NEXT = 1, MM2_PREV = 2, MM2_PLAY = 3, MM2_TAPE = 4, MM2_INSTR = 5 };
#define MM2_ACT_MAX 5                 /* dernier id d'action; extensible vers le haut */

typedef struct { uint8_t ch, type, num, act, val; } mm2_row;
/* ch 1..16 / MM2_CH_ALL; type 0 = CC, 1 = PC; num 0..127; act: enum; val 0..255 */

/* ---- ABI d'intégration (à câbler avant usage; 0 = fonctionnalité inerte, sans crash) ---- */
extern int  (*mm2_setting_get)(const char *key, int dflt);
extern void (*mm2_setting_put)(const char *key, int val);
extern void (*mm2_act_next)(int dir);       /* +1 suivant, -1 précédent (saut marqueur) */
extern void (*mm2_act_play)(void);          /* lecture depuis le marqueur courant */
extern void (*mm2_act_tape)(int tape);      /* 1..4 (builds 4 tapes) */
extern void (*mm2_act_instr)(int slot);     /* slot instrument/preset */
typedef struct { int tape_mode, stopped, punch, counting, markers; } mm2_env;
extern void (*mm2_env_query)(mm2_env *e);   /* gardes des actions marqueur */

/* ---- cycle de vie / persistance (clés 'mmen', 'mmr0'..'mmr15') ---- */
void mm2_load(void);
void mm2_save(void);
int  mm2_enabled(void);
void mm2_enable(int on);

/* ---- dispatch (n'importe quel thread; 1 = consommé) ---- */
int  mm2_cc(int ch, int cc, int v);         /* ch 0..15 (octet de statut & 15) */
int  mm2_pc(int ch, int num);

/* ---- éditeur ---- */
void mm2_edit_open(void);
void mm2_edit_close(int save);              /* save: valider (mm2_save) avant fermeture */
int  mm2_edit_view(void);
int  mm2_edit_key(int code, int pressed, int shift);   /* 1 = consommé */
void mm2_edit_turn(int knob, int delta);    /* 0 bleu, 1 vert, 2 blanc, 3 rouge */
int  mm2_edit_cursor(void);                 /* 0..15 */
int  mm2_edit_page(void);                   /* 0..3 */
int  mm2_edit_editing(void);                /* 1: valeur de la ligne courante en édition */
const mm2_row *mm2_row_get(int i);              /* 0..15, lecture seule */
mm2_row *mm2_row_edit(int i);            /* écriture (éditeur / intégration) */

/* ---- codec de ligne (clés mmrN) ---- */
int      mm2_row_pack(const mm2_row *r);    /* int32, -1 si ligne invalide */
mm2_row  mm2_row_unpack(int v);             /* ligne OFF si invalide */
#endif
