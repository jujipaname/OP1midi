/* Copyright (C) 2026 OP1midi. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
/* MM2 — mapping MIDI généraliste pour OP-1 OG (OP1midi), v3.
 * Module portable, sans dépendance op1r/Hundekraft: l'intégration câble les pointeurs ci-dessous
 * (settings, actions, sonde d'environnement), puis route les événements.
 *
 * 16 lignes de mapping. Chaque ligne: (type source NOTE/CC/PC, n° source) -> (destination, n°).
 * Destinations: COMBO (séquence de 1..3 touches, ± Shift, injectées en touches synthétiques),
 * SND 1..8 (learn par touche: la touche brute est stockée et rejeuée après un SYNTH), TAPE 1..4,
 * MUTE 1..4, MRK 1..32, M+, M-. Tout est trigger: CC front montant >= 64
 * (pas de répétition tant que tenu), NOTE = note-on avec vélocité > 0, PC = chaque message.
 * Première ligne correspondante gagnante. Canal MIDI global ('mmch': 0 off / 1..16 / 17 ALL).
 * Ligne inactive: dst == MM2_D_OFF. Réglage fade 'mmfade' (0 off, 1 = 1 s, 2 = 2 s, 3 = 8 s,
 * 4 = 32 s) — l'exécution du fade est côté intégration. Format versionné par la clé 'mmv'
 * (= MM2_FMT): une version différente remet les lignes à zéro. Combos stockés à part
 * ('mmk0'..'mmk15', 1 int32 par ligne: 3 octets, bit7 = Shift, 0 = absent).
 *
 * Éditeur (mm2_edit.c): machine à états pure, sans rendu. Rendu côté intégration: colonnes aux
 * couleurs des encodeurs, ligne du curseur en surbrillance.
 *   bleu: rotation = curseur (16 slots, la page suit); clic = mode d'affichage des notes
 *         (numéro / anglais C#4 / solfège DO#4 — global, clé 'mmnd'); Shift+clic = vider la ligne
 *   vert: rotation = n° source / Shift+rotation = canal de ligne; clic = type NOTE/CC/PC;
 *         Shift+clic = learn MIDI on-off (le premier NOTE/CC/PC capture)
 *   blanc: rotation = destination en liste plate (COMBO, OFF, SND 1..8, TAPE 1..4, MUTE 1..4,
 *         MRK 1..32, M+, M-); clic seul = sortie/validate du learn (touche ou combo);
 *         Shift+clic = learn on-off selon la destination (SND -> touche, COMBO -> combo,
 *         3 touches max, clic blanc = valider, 3e touche = validation auto)
 *   rouge: rotation = fade (OFF/1s/2s/8s/32s) / Shift+rotation = pistes du fade;
 *         Shift+clic = supprimer la ligne *   flèches = pages; Help (0x00) = valider + fermer; touches de mode/T/transport = quitter
 *   (sauvegarde systématique; sans Shift la touche agit: retour 0) — SAUF learn armé: toutes
 *   les touches sont avalées (retour 1), Help reste valide. */
#ifndef MM2_H
#define MM2_H
#include <stdint.h>

#define MM2_ROWS    16
#define MM2_CH_OFF  0                 /* 'mmch': mapping désactivé */
#define MM2_CH_ALL  17                /* 'mmch': tous les canaux */
#define MM2_DETENT  1024              /* delta d'encodeur par cran (convention op1r) */
/* masque de pistes du fade (Shift + rotation rouge): ALL, 1, 2, 3, 4, 1+2, 3+4 */
enum { MM2_FM_ALL = 0, MM2_FM_1 = 1, MM2_FM_2 = 2, MM2_FM_3 = 3, MM2_FM_4 = 4,
       MM2_FM_12 = 5, MM2_FM_34 = 6 };
#define MM2_FM_MAX  6
#define MM2_FMT     4                 /* version du format des lignes ('mmv') */

enum { MM2_S_NOTE = 0, MM2_S_CC = 1, MM2_S_PC = 2 };
enum { MM2_D_OFF = 0, MM2_D_SND = 1, MM2_D_TAPE = 2, MM2_D_MUTE = 3,
       MM2_D_MRK = 4, MM2_D_MRP = 5, MM2_D_MRM = 6, MM2_D_COMBO = 7 };
#define MM2_S_MAX   2
#define MM2_D_MAX   7
#define MM2_MRK_MAX 32                /* un ensemble partagé de marqueurs (core 3.0.3) */
#define MM2_COMBO_MAX 3               /* touches par combo (bit7 = Shift, 0 = absent) */

typedef struct { uint8_t ch, stype, snum, dst, dnum, fade, fmask; } mm2_row;
/* ch: canal de la ligne, 0 = non réglé (traité comme ALL), 1..16, MM2_CH_ALL;
 * stype: MM2_S_*; snum 0..127; dst: MM2_D_*; dnum 0..255 (plage validée par destination:
 * SND 1..8, TAPE 1..4, MUTE 1..4, MRK 1..MM2_MRK_MAX, M+/M-, OFF et COMBO sans paramètre
 * — pour COMBO, dnum = nombre de touches du combo, pur affichage);
 * fade: 0..4 (OFF/1s/2s/8s/32s) — utilisé par MUTE/MRK/M+/M- uniquement (côté intégration);
 * fmask: MM2_FM_* — les pistes que le fade affecte */

/* ---- ABI d'intégration (définies ici, assignées par l'hôte au démarrage; 0 = action ignorée) ---- */
extern int  (*mm2_setting_get)(const char *key, int dflt);
extern void (*mm2_setting_put)(const char *key, int val);
extern void (*mm2_act_snd)(int key);        /* 1..8 (aucun service core connu: toast côté intégration) */
extern void (*mm2_act_tape)(int tape);      /* 1..4 (op1_tape_switch; erreurs ignorées) */
extern void (*mm2_act_mute)(int track, int fade, int fmask);
extern void (*mm2_act_marker)(int local, int fade, int fmask);
extern void (*mm2_act_mnext)(int fade, int fmask);
extern void (*mm2_act_mprev)(int fade, int fmask);
extern void (*mm2_act_combo)(int row, const uint8_t *keys, int n);   /* 1..3 entrées (bit7 = Shift) */
typedef struct { int tape_mode, raw_state, punch, markers; } mm2_env;
extern void (*mm2_env_query)(mm2_env *e);   /* gardes: raw_state 3 arrêt / 10 lecture autorisés */

/* ---- cycle de vie / persistance ('mmv', 'mmen', 'mmnd', 'mmr0'..'mmr15') ---- */
void mm2_load(void);
void mm2_save(void);
int  mm2_enabled(void);
void mm2_enable(int on);
void mm2_dnum_range(int dst, int *lo, int *hi);        /* plage valide du n° (0..0 si sans paramètre) */
int  mm2_note_disp(void);                  /* affichage des notes: 0 numéro, 1 anglais, 2 solfège */
void mm2_note_disp_cycle(void);            /* clic bleu: numéro -> anglais -> solfège -> numéro */
const char *mm2_note_name(int note);       /* rendu selon le mode: "61" / "C#4" / "DO#4" (statique) */
int  mm2_combo_get(int row, uint8_t *keys);            /* combo stocké: nombre de touches 0..3 */
void mm2_combo_set(int row, const uint8_t *keys, int n);         /* 3 octets max, 0 = absent */

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
enum { MM2_L_NONE = 0, MM2_L_MIDI = 1, MM2_L_KEY = 2, MM2_L_COMBO = 3 };
int  mm2_edit_learn_state(void);            /* MM2_L_*: learn MIDI / touche / combo / aucun */
void mm2_edit_learn_feed(int stype, int num);          /* prochain NOTE/CC/PC appris -> ligne curseur */
void mm2_edit_learn_feed_key_code(int code, int shift);/* touche sound apprise (dst SND): code brut
                                            * +/- Shift stocké dans la ligne (combos), 1 touche */
void mm2_edit_learn_feed_combo(int code, int shift);   /* touche du combo (dst COMBO): rejets
                                            * internes (Help, Shift, clics encodeurs) */
int  mm2_edit_learn_stage(uint8_t out[3]);  /* touches du combo en cours d'enregistrement */
void mm2_edit_delete(void);                 /* Shift + clic rouge: ligne -> OFF, champs à zéro */
const mm2_row *mm2_row_get(int i);          /* 0..15, lecture seule */
mm2_row *mm2_row_edit(int i);               /* écriture (éditeur / intégration) */

/* ---- codec de ligne (clés mmrN, version 'mmv') ---- */
int      mm2_row_pack(const mm2_row *r);    /* int32, -1 si ligne invalide */
mm2_row  mm2_row_unpack(int v);             /* ligne OFF si invalide */
#endif
