/* Copyright (C) 2026 OP1midi. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
/* MM2 — mapping MIDI généraliste pour OP-1 OG (OP1midi), v3.
 * Module portable, sans dépendance op1r/Hundekraft: l'intégration câble les pointeurs ci-dessous
 * (settings, actions, sonde d'environnement), puis route les événements.
 *
 * 16 lignes de mapping. Chaque ligne: (type source NOTE/CC/PC, n° source) -> (destination, n°).
 * Tout est trigger: CC front montant >= 64 (pas de répétition tant que tenu), NOTE = note-on avec
 * vélocité > 0, PC = chaque message. Première ligne correspondante gagnante. Canal MIDI global
 * ('mmch': 0 off / 1..16 / 17 ALL). Ligne inactive: dst == MM2_D_OFF. Réglage fade 'mmfade'
 * (0 off, 1 = 1 s, 2 = 2 s, 3 = 8 s, 4 = 32 s) — l'exécution du fade est côté intégration.
 * Format versionné par la clé 'mmv' (= MM2_FMT): une version différente remet les lignes à zéro.
 *
 * Éditeur (mm2_edit.c): machine à états pure, sans rendu. Rendu côté intégration: colonnes aux
 * couleurs des encodeurs, ligne du curseur en rouge.
 *   bleu: clic = cycle type source; Shift+clic = learn on/off; rotation = n° source;
 *         Shift+rotation = canal global (colonne type du curseur l'affiche tant que Shift tenu;
 *         learn armé, elle affiche LRN tant que rien n'est capturé)
 *   vert: rotation = n° de destination (SND 1..8, TAPE 1..4, MUTE 1..4, MRK 1..32)
 *   blanc: rotation = fade (OFF/1s/2s/8s/32s, plus de Shift)
 *   rouge: rotation = destination (SND TAPE MUT MRK M+ M- OFF); Shift+clic = supprimer la ligne
 *   flèches 0x24/0x25 = pages; Shift + Help (0x00) = valider + fermer; touches de
 *   mode/T/transport = quitter (sauvegarde systématique; sans Shift la touche agit: retour 0). */
#ifndef MM2_H
#define MM2_H
#include <stdint.h>

#define MM2_ROWS    16
#define MM2_CH_OFF  0                 /* 'mmch': mapping désactivé */
#define MM2_CH_ALL  17                /* 'mmch': tous les canaux */
#define MM2_DETENT  1024              /* delta d'encodeur par cran (convention op1r) */
#define MM2_FMT     3                 /* version du format des lignes ('mmv') */

enum { MM2_S_NOTE = 0, MM2_S_CC = 1, MM2_S_PC = 2 };
enum { MM2_D_OFF = 0, MM2_D_SND = 1, MM2_D_TAPE = 2, MM2_D_MUTE = 3,
       MM2_D_MRK = 4, MM2_D_MRP = 5, MM2_D_MRM = 6 };
#define MM2_S_MAX   2
#define MM2_D_MAX   6
#define MM2_MRK_MAX 32                /* un ensemble partagé de marqueurs (core 3.0.3) */

typedef struct { uint8_t stype, snum, dst, dnum; } mm2_row;
/* stype: MM2_S_*; snum 0..127; dst: MM2_D_*; dnum 0..255 (plage validée par destination:
 * SND 1..8, TAPE 1..4, MUTE 1..4, MRK 1..MM2_MRK_MAX, M+/M- et OFF sans paramètre) */

/* ---- ABI d'intégration (définies ici, assignées par l'hôte au démarrage; 0 = action ignorée) ---- */
extern int  (*mm2_setting_get)(const char *key, int dflt);
extern void (*mm2_setting_put)(const char *key, int val);
extern void (*mm2_act_snd)(int key);        /* 1..8 (aucun service core connu: toast côté intégration) */
extern void (*mm2_act_tape)(int tape);      /* 1..4 (op1_tape_switch; erreurs ignorées) */
extern void (*mm2_act_mute)(int track);     /* 1..4: toggle mute */
extern void (*mm2_act_marker)(int local);   /* n-ième marqueur (ensemble partagé), tape courante */
extern void (*mm2_act_mnext)(void);         /* marker suivant */
extern void (*mm2_act_mprev)(void);         /* marker précédent */
typedef struct { int tape_mode, raw_state, punch, markers; } mm2_env;
extern void (*mm2_env_query)(mm2_env *e);   /* gardes: raw_state 3 arrêt / 10 lecture autorisés */

/* ---- cycle de vie / persistance ('mmv', 'mmch', 'mmfade', 'mmr0'..'mmr15') ---- */
void mm2_load(void);
void mm2_save(void);
int  mm2_enabled(void);
void mm2_enable(int on);
int  mm2_channel(void);                     /* MM2_CH_OFF / 1..16 / MM2_CH_ALL */
void mm2_ch_step(int dir);                  /* Shift + rotation verte (éditeur): canal ±1, wrap */
int  mm2_fade(void);                        /* 0..4 (0 = off) */
void mm2_fade_step(int dir);                /* Shift + rotation blanche (éditeur): fade ±1, wrap */
void mm2_dnum_range(int dst, int *lo, int *hi);        /* plage valide du n° (0..0 si sans paramètre) */

/* ---- dispatch (n'importe quel thread; 1 = consommé) ---- */
int  mm2_cc(int ch, int cc, int v);         /* ch 0..15 (octet de statut & 15) */
int  mm2_pc(int ch, int num);
int  mm2_note(int ch, int note, int vel);   /* note-on; vel > 0 déclenche; vel <= 0 mappée: avalé */
int  mm2_note_match(int ch, int note);      /* 1 si une ligne NOTE correspond (swallow pipeline) */

/* ---- éditeur ---- */
void mm2_edit_open(void);
void mm2_edit_close(int save);              /* save: valider (mm2_save) avant fermeture */
int  mm2_edit_view(void);
int  mm2_edit_key(int code, int pressed, int shift);   /* 1 = consommé; 0 = sortie sans Shift */
void mm2_edit_turn(int knob, int delta, int shift);   /* 0 bleu, 1 vert, 2 blanc, 3 rouge */
int  mm2_edit_cursor(void);                 /* 0..15 */
int  mm2_edit_page(void);                   /* 0..3 */
int  mm2_edit_learning(void);               /* learn armé (MIDI, ou touche sound si dst SND) */
void mm2_edit_learn_feed(int stype, int num);          /* prochain NOTE/CC/PC appris -> ligne curseur */
void mm2_edit_learn_feed_key(int slot);     /* prochaine touche sound apprise (dst SND): slot 1..8 */
void mm2_edit_delete(void);                 /* Shift + clic rouge: ligne -> OFF, champs à zéro */
const mm2_row *mm2_row_get(int i);          /* 0..15, lecture seule */
mm2_row *mm2_row_edit(int i);               /* écriture (éditeur / intégration) */

/* ---- codec de ligne (clés mmrN, version 'mmv') ---- */
int      mm2_row_pack(const mm2_row *r);    /* int32, -1 si ligne invalide */
mm2_row  mm2_row_unpack(int v);             /* ligne OFF si invalide */
#endif
