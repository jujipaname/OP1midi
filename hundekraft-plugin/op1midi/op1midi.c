/* OP1midi — mapping MIDI generaliste (mm2 v3) comme feature plugin kennel, cible Hundekraft 3.0.3.
 * Copyright (C) 2026 OP1midi. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE).
 *
 * Entree: Shift + Help (mode clock) -> T3 (settings) -> rangée "MIDI MAP" (op1_switch enregistré
 * à OP1_EV_BOOT_DONE après les built-ins: 2e rangée = blanche): tourner = ON/OFF global, appui
 * blanc = ouvre l'éditeur.
 * Éditeur v4 (overlay modal sur voile + panneau noir):
 *   bleu: clic = type source (NOTE/CC/PC); Shift+clic = learn ON/OFF; rotation = n° source;
 *         Shift+rotation = canal global (la colonne type du curseur affiche CH n / ALL / OFF
 *         tant que Shift est tenu; learn armé, elle affiche LRN)
 *   vert: rotation = destination + n° en liste plate (OFF, SND 1..8, TAPE 1..4, MUTE 1..4,
 *         MRK 1..32, M+, M-)
 *   blanc: rotation = fade OFF/1s/2s/8s/32s (toast de confirmation, plus d'affichage permanent)
 *   rouge: rotation = curseur (16 slots, la page suit); Shift+clic = supprimer la ligne
 *   fleches = pages; sorties (sauvegarde systématique): Help (Shift ou non), touches de mode /
 *   T1..T4 / transport (la touche agit ensuite sauf Shift tenu). Curseur en rouge, colonnes aux
 *   couleurs des encodeurs.
 *
 * Sources: NOTE (note-on vel > 0; note-off mappée avalée via le pipeline), CC (front >= 64),
 * PC (chaque message). Destinations: SOUND 1..8 (learn par touche: Shift+clic bleu arme, la
 * prochaine touche sound T1..T4 / Shift+T1..T4 est capturée; replay par injection de touches
 * synthétiques dans la file stock — SYNTH puis Shift+T_n, adresses R246 0x01DD5044/0x01DD5244
 * /0x01DD5248/0x01DD524D, réveil 0x019C2F04, pattern op1r midi/mx.c), TAPE 1..4
 * (op1_tape_switch), MUTE 1..4 (octet mute R246 0xFF8010A0 + màj DSP 0x01967ED0), MARKER 1..32
 * (un ensemble partagé entre les tapes en 3.0.3: le n-ième marqueur, tape courante, sans
 * switch), MRK+/MRK- (saut, wrap).
 *
 * Fade (Shift + rotation blanche dans l'éditeur: OFF/1s/2s/8s/32s): en lecture, tout saut de
 * marker fond les 4 volumes de pistes tape (mots live 0xFF801928 + gammes 0xFF8018CC, R246)
 * vers 0, saute au silence (main loop), puis remonte et restaure. Piloté par tick (audio),
 * saut exécuté sur la main loop; STOP pendant un fade: saut annulé, volumes restaurés.
 * Les CC mappés sont retirés du chemin clés/encodeurs (op1_midi_take_cc); les notes mappées
 * sont avalées dans le pipeline de notes (source MIDI uniquement). */
#ifndef OP1_HOST
#pragma GCC optimize ("Os", "no-tree-loop-distribute-patterns")
#endif
#include "../../op1_feature.h"
#include "../../op1_feature_midi.h"
#include "../../op1_feature_tape.h"
#include "../../op1_feature_seq.h"
#include "mm2.h"
#include "../../screens/stroke_font.h"

#define FEAT "op1midi"

static void wire(void);

/* ---- ABI mm2 -> réglages kennel (espace de noms du plugin) ---- */
static int mm_get(const char *k, int d) { return (int)op1_setting_get(FEAT, k, (int32_t)d); }
static void mm_put(const char *k, int v) { op1_setting_set(FEAT, k, v); }

/* ---- état suivi ---- */
static volatile uint8_t cur_mode;                /* OP1_EV_MODE / op1_input.mode */
static volatile uint8_t sh_held;                 /* Shift tenu (mis à jour à chaque événement) */

/* ---- file d'actions (producteur: timer midi_raw; consommateur: main loop) ---- */
enum { K_SND = 1, K_TAPE, K_MUTE, K_MARKER, K_MNEXT, K_MPREV };
typedef struct { uint8_t kind; int16_t a, b; } pend_t;
static pend_t pq[8];
static volatile uint8_t pq_wr, pq_rd;

static void pq_push(int kind, int a, int b)
{
    uint8_t w = pq_wr, n = (uint8_t)((w + 1) & 7);
    if (n == pq_rd) return;
    pq[w].kind = (uint8_t)kind; pq[w].a = (int16_t)a; pq[w].b = (int16_t)b; pq_wr = n;
}

static void env_query(mm2_env *e)
{
    e->tape_mode = cur_mode == 2;                /* mode tape (ModeSwitcher, 0xFF907D58) */
    e->raw_state = *(volatile int32_t *)0x018C17D4;   /* 3 arrêt, 6 armé, 10 lecture, 5/7 rec */
    e->punch = pu_mode;                          /* données partagées du core */
    e->markers = tm_n;
}

/* ---- CC pris au chemin clés/encodeurs du core ---- */
static uint8_t taken[16];
static uint8_t takes_dirty;
static void apply_takes(void)
{
    uint8_t want[16];
    for (int i = 0; i < 16; i++) want[i] = 0;
    for (int i = 0; i < MM2_ROWS; i++) {
        const mm2_row *r = mm2_row_get(i);
        if (r->dst && r->stype == MM2_S_CC) want[r->snum >> 3] |= (uint8_t)(1u << (r->snum & 7));
    }
    for (int c = 0; c < 128; c++) {
        int w = want[c >> 3] >> (c & 7) & 1, h = taken[c >> 3] >> (c & 7) & 1;
        if (w != h) op1_midi_take_cc(c, w);
    }
    for (int i = 0; i < 16; i++) taken[i] = want[i];
    takes_dirty = 0;
}

/* ---- saut de marker (main loop), avec ou sans fade ---- */
#define TM_STEP 441                              /* 10 ms: le marqueur "sur" le curseur */
enum { J_MARKER = 1, J_NEXT, J_PREV };           /* kind: cible fixe / résolue au silence */
static uint8_t j_kind, j_pending;                /* saut armé (fade) */
static int32_t j_pos;

static int tape_live(void) { return op1_tape_current() + 1; }

static void do_jump(int32_t pos)                 /* tape courante: locate + play */
{
    op1_tape_pos_set(pos);
    op1_seq_press_play();
}

static void jump_resolve(int32_t *pos, int *tape)          /* cible au moment du silence */
{
    *tape = tape_live();
    if (j_kind == J_MARKER) { *pos = j_pos; return; }      /* ensemble partagé: tape courante */
    int32_t c = op1_tape_pos_get(), t = -1;
    if (j_kind == J_NEXT) {
        for (int i = 0; i < tm_n; i++) if (tm_pos[i] > c + TM_STEP) { t = tm_pos[i]; break; }
        if (t < 0) t = tm_pos[0];                             /* wrap */
    } else {
        for (int i = tm_n - 1; i >= 0; i--) if (tm_pos[i] < c - TM_STEP) { t = tm_pos[i]; break; }
        if (t < 0) t = tm_pos[tm_n - 1];
    }
    *pos = t;
}

static int32_t marker_pos(int local)             /* 1..tm_n -> position (échantillons) */
{
    return local >= 1 && local <= tm_n ? tm_pos[local - 1] : -1;
}

/* ---- moteur de fade (tick = thread audio; le saut se fait sur la main loop) ---- */
static const uint8_t fade_secs[5] = { 0, 1, 2, 8, 32 };
#define FADE_BPS 689                             /* blocs 64 éch. par seconde (44100/64) */
static volatile uint8_t fade_st;                 /* 0 idle, 1 out, 2 in */
static volatile uint8_t fade_silence;            /* 1: volume à 0, le main loop doit sauter */
static volatile uint8_t fade_secs_v;             /* secondes de fade de la ligne déclenchée */
static int16_t fade_orig[4], fade_lo[4];
static uint8_t fade_has[4];
static uint32_t fade_len, fade_blocks;

/* R246 (mêmes adresses que le chemin mixer du firmware): volumes live 0xFF801928 + t,
 * gammes {min, max, ?} 0xFF8018CC + t * 3. */
#define MIX_VOL(t)   ((volatile int16_t *)0xFF801928)[t]
#define MIX_RANGE(t) ((const volatile int16_t *)0xFF8018CC)[(t) * 3]

static void fade_start(void)                     /* main loop: armé la descente */
{
    for (int t = 0; t < 4; t++) {
        fade_orig[t] = MIX_VOL(t);
        fade_lo[t] = MIX_RANGE(t);
        fade_has[t] = (uint8_t)(fade_orig[t] != fade_lo[t]);
    }
    fade_blocks = 0;
    fade_len = (uint32_t)fade_secs[fade_secs_v % 5] * FADE_BPS;
    fade_st = 1;
    fade_silence = 0;
}

static void fade_cancel(void)                    /* main loop: restauration immédiate */
{
    for (int t = 0; t < 4; t++) if (fade_has[t]) MIX_VOL(t) = fade_orig[t];
    fade_st = 0;
    fade_silence = 0;
    j_pending = 0;
}

static void fade_tick(void)                      /* audio thread: 4 mots par bloc */
{
    if (!fade_st) return;
    if (++fade_blocks < fade_len) {
        int32_t p = (int32_t)((uint32_t)fade_blocks * 32768u / fade_len);
        for (int t = 0; t < 4; t++)
            if (fade_has[t]) MIX_VOL(t) = (int16_t)(fade_st == 1 ?
                fade_orig[t] + ((fade_lo[t] - fade_orig[t]) * p >> 15) :
                fade_lo[t] + ((fade_orig[t] - fade_lo[t]) * p >> 15));
        return;
    }
    for (int t = 0; t < 4; t++) if (fade_has[t]) MIX_VOL(t) = fade_st == 1 ? fade_lo[t] : fade_orig[t];
    if (fade_st == 1) { fade_silence = 1; }      /* silence: le main loop saute puis relance */
    else fade_st = 0;
}

/* ---- hooks mm2: mise en file seulement (timer context) ---- */
static void snd_inject(int slot);

static void act_snd(int key) { pq_push(K_SND, key, 0); }
static void act_tape(int tape) { pq_push(K_TAPE, tape, 0); }
static void act_mute(int track, int fade) { pq_push(K_MUTE, track, fade); }
static void act_marker(int local, int fade) { pq_push(K_MARKER, local, fade); }
static void act_mnext(int fade) { pq_push(K_MNEXT, fade, 0); }
static void act_mprev(int fade) { pq_push(K_MPREV, fade, 0); }

static void start_fade_in(void)
{
    for (int t = 0; t < 4; t++) { fade_lo[t] = MIX_VOL(t); }
    fade_blocks = 0;
    fade_st = 2;
}

/* ---- injection de touches (adresses R246, pattern op1r midi/mx.c): sélection de sound key ---- */
#define KQ_DATA   ((volatile uint16_t *)0x01DD5044)
#define KQ_RD     ((volatile uint32_t *)0x01DD5244)
#define KQ_WR     ((volatile uint32_t *)0x01DD5248)
#define KQ_BUSY   (*(volatile uint8_t *)0x01DD524D)
#define KQ_WAKE   ((void (*)(void))0x019C2F04)

static void inject_keys(const uint8_t *seq, int n)          /* seq: {code, pressed} x n */
{
    uint32_t m;
    __asm__ volatile ("cli %0;" : "=d"(m));
    for (int i = 0; i < n; i++) {
        volatile uint32_t *rd = KQ_RD, *wr = KQ_WR;
        uint32_t w = *wr, nx = (w + 1) & 0xFF;
        if (nx == *rd || KQ_BUSY) break;                    /* plein ou ISR mid-push: jette le reste */
        KQ_DATA[w] = (uint16_t)(seq[i * 2] | seq[i * 2 + 1] << 8);
        *wr = nx;
    }
    __asm__ volatile ("sti %0;" : : "d"(m));
    KQ_WAKE();
}

static void snd_inject(int slot)                 /* sélection de sound key par touches synthétiques */
{
    uint8_t seq[10];                             /* {code, pressed} x 5 */
    int n = 0;
    seq[n++] = 0x02; seq[n++] = 1;               /* SYNTH press (idempotent si déjà en synth) */
    seq[n++] = 0x02; seq[n++] = 0;               /* SYNTH release */
    if (slot > 4) { seq[n++] = 0x26; seq[n++] = 1; }           /* Shift press (slots 5..8) */
    int tk = 0x05 + (slot > 4 ? slot - 4 : slot);              /* T1..T4 = 0x06..0x09 */
    seq[n++] = (uint8_t)tk; seq[n++] = 1;
    seq[n++] = (uint8_t)tk; seq[n++] = 0;
    if (slot > 4) { seq[n++] = 0x26; seq[n++] = 0; }           /* Shift release */
    inject_keys(seq, n / 2);
}

static void drain(void)                          /* main loop */
{
    while (pq_rd != pq_wr) {
        pend_t p = pq[pq_rd];
        pq_rd = (uint8_t)((pq_rd + 1) & 7);
        if (p.kind == K_SND) {
            if (p.a >= 1 && p.a <= 8) snd_inject(p.a);
        } else if (p.kind == K_TAPE) {
            if (!pu_mode) op1_tape_switch(p.a);  /* erreurs rec/disk ignorées */
        } else if (p.kind == K_MUTE) {
            if (p.a >= 1 && p.a <= 4) {
                volatile uint8_t *m = (volatile uint8_t *)0xFF8010A0;
                m[p.a - 1] ^= 1;
                ((void (*)(void))0x01967ED0)();  /* Core: mute words for the DSP */
            }
        } else {                             /* K_MARKER / K_MNEXT / K_MPREV */
            mm2_env e;
            env_query(&e);
            if (!e.tape_mode || e.punch || (e.raw_state != 3 && e.raw_state != 10)) continue;
            if (e.raw_state == 10 && p.b) {                      /* en lecture: fade de la ligne si réglé */
                if (fade_st) continue;                           /* fade en cours: ignore */
                j_kind = p.kind == K_MARKER ? J_MARKER : (p.kind == K_MNEXT ? J_NEXT : J_PREV);
                j_pos = p.kind == K_MARKER ? marker_pos(p.a) : 0;
                if (j_kind == J_MARKER && j_pos < 0) continue;
                j_pending = 1;
                fade_secs_v = p.b;                               /* fade de la ligne */
                fade_start();
                continue;
            }
            /* immédiat (arrêté, ou lecture sans fade) */
            if (p.kind == K_MARKER) {
                int32_t pos = marker_pos(p.a);
                if (pos >= 0) do_jump(pos);          /* ensemble partagé: tape courante */
            } else {
                int32_t pos; int tape;
                j_kind = p.kind == K_MNEXT ? J_NEXT : J_PREV;
                jump_resolve(&pos, &tape);
                if (pos >= 0) do_jump(pos);
            }
        }
    }
    if (fade_silence) {                          /* volume à 0: sauter maintenant */
        fade_silence = 0;
        mm2_env e;
        env_query(&e);
        if (j_pending && e.tape_mode && !e.punch && e.raw_state == 10) {
            int32_t pos; int tape;
            jump_resolve(&pos, &tape);
            if (pos >= 0) { do_jump(pos); start_fade_in(); }
            else fade_cancel();
        } else fade_cancel();                    /* état changé (STOP...): restaurer sans sauter */
        j_pending = 0;
    }
    if (takes_dirty) apply_takes();
}

/* ---- MIDI: learn + dispatch (timer) ---- */
static void midi_raw(const uint8_t *m, int len)
{
    wire();
    if (len < 2) return;
    int st = m[0] & 0xF0;
    if (mm2_edit_learning()) {                   /* learn: MIDI seulement si la destination != SND */
        if (mm2_row_get(mm2_edit_cursor())->dst == MM2_D_SND) return;   /* learn touche en cours */
        if (st == 0xB0 && len >= 3) { mm2_edit_learn_feed(MM2_S_CC, m[1]); op1_midi_take_cc(m[1], 1); takes_dirty = 1; }
        else if (st == 0xC0) mm2_edit_learn_feed(MM2_S_PC, m[1]);
        else if (st == 0x90 && len >= 3 && m[2] > 0) mm2_edit_learn_feed(MM2_S_NOTE, m[1]);
        return;
    }
    if (st == 0xB0 && len >= 3) mm2_cc(m[0] & 15, m[1], m[2]);
    else if (st == 0xC0) mm2_pc(m[0] & 15, m[1]);
    else if (st == 0x90 && len >= 3) mm2_note(m[0] & 15, m[1], m[2]);
    else if ((st == 0x80 || st == 0x90) && len >= 3) mm2_note(m[0] & 15, m[1], 0);   /* note-off: avalé si mappée */
}

/* ---- pipeline de notes: avaler les notes mappées venues du MIDI ---- */
static void pip_note_on(const op1_note *n, int next)
{
    int src = op1_note_source();
    if (OP1_SRC_KIND(src) == OP1_SRC_MIDI && mm2_note_match(OP1_SRC_CH(src), n->note)) return;
    op1_note_on(next, n);
}
static void pip_note_off(const op1_note *n, int next)
{
    int src = op1_note_source();
    if (OP1_SRC_KIND(src) == OP1_SRC_MIDI && mm2_note_match(OP1_SRC_CH(src), n->note)) return;
    op1_note_off(next, n);
}

/* ---- entrées: ouverture (appui blanc en mode clock) + éditeur modal ---- */
static uint64_t seen_down;
static int input(const op1_input *in)
{
    wire();
    sh_held = in->shift;                         /* l'overlay lit l'état courant du Shift */
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
        if (mm2_edit_learning() && in->code >= 0x06 && in->code <= 0x09 &&
            mm2_row_get(mm2_edit_cursor())->dst == MM2_D_SND) {
            mm2_edit_learn_feed_key(in->code - 0x05 + (in->shift ? 4 : 0));   /* touche sound apprise */
            return 1;                                 /* avalée: rien ne bouge pendant l'apprentissage */
        }
        int was = mm2_edit_view();
        int consumed = mm2_edit_key(in->code, in->pressed, in->shift);
        if (was) takes_dirty = 1;                                 /* fermeture ou suppression: re-appliquer */
        if (!consumed && was && mm2_edit_view()) return 1;        /* toujours ouvert: modal */
        return consumed;                                          /* 0: sortie, la touche agit */
    }
    if (mm2_edit_view()) {
        int f0 = mm2_row_get(mm2_edit_cursor())->fade;
        mm2_edit_turn(in->code, in->delta, in->shift);
        if (in->type == OP1_IN_KNOB && in->code == 2 && mm2_row_get(mm2_edit_cursor())->fade != f0) {
            static const char *const fn[] = { "FADE OFF", "FADE 1S", "FADE 2S", "FADE 8S", "FADE 32S" };
            op1_toast(fn[mm2_row_get(mm2_edit_cursor())->fade]);
        }
        return 1;
    }
    return 0;
}

/* ---- rendu de l'éditeur (overlay, Gfx): voile + panneau noir opaque (motif du plugin
 * randomiser du kennel), colonnes aux couleurs des encodeurs, curseur en rouge ---- */
static const uint8_t c_red[4] = { 0xFF, 0x4E, 0x3F, 0xFF }, c_white[4] = { 0xFF, 0xFF, 0xFF, 0xFF },
                     c_green[4] = { 0x00, 0xED, 0x95, 0xFF }, c_black[4] = { 0, 0, 0, 0xFF },
                     c_blue[4] = { 0x4D, 0x9E, 0xFF, 0xFF };
static const uint8_t *stCol_v;                   /* couleur de la colonne type (contextuelle) */
#define ES   OP1_Q10(5) / 2
#define EW   0x600
#define X_SL OP1_Q10(8)                            /* n° de slot (rouge) */
#define X_ST OP1_Q10(34)                           /* type source (bleu) */
#define X_SN OP1_Q10(90)                           /* n° source (bleu) */
#define X_DST OP1_Q10(140)                         /* destination + n° (vert) */
#define X_CH OP1_Q10(240)                          /* canal de la ligne (blanc) */
#define Y_T  OP1_Q10(10)
#define Y0   OP1_Q10(40)
#define DY   OP1_Q10(26)
#define PN_X0 6
#define PN_X1 314
#define PN_Y0 4
#define PN_Y1 156

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

static void mm_panel(void *r)
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

static const char *const st_names[] = { "NOTE", "CC", "PC" };
static const char *const dst_names[] = { "--", "SND", "TAPE", "MUT", "MRK", "M+", "M-" };
static const char *const fade_names[] = { "OFF", "1S", "2S", "8S", "32S" };
static const char *rowChDisp(const mm2_row *w)             /* canal de la ligne */
{
    int ch = w->ch ? w->ch : MM2_CH_ALL;                   /* 0 = non réglé: ALL */
    if (ch == MM2_CH_ALL) return "ALL";
    static char b[6];
    char *e = put_str(b, "CH");
    put_num(e, ch);
    return b;
}

static void mm_cell(void *r, int x0, int x1, int y, const uint8_t *c)   /* fond de cellule (surbrillance) */
{
    for (int yy = y - 4; yy <= y + 16; yy += 2) {
        op1_pt a = { OP1_Q10(x0), OP1_Q10(yy) }, b = { OP1_Q10(x1), OP1_Q10(yy) };
        op1_raster_line(r, &a, &b, OP1_Q10(2), c);
    }
}

static void overlay(void *r, int32_t screen)
{
    (void)screen;
    if (!mm2_edit_view()) return;
    mm_fade(r);
    mm_panel(r);
    int page = mm2_edit_page(), cur = mm2_edit_cursor();
    sf_text(r, X_SL, Y_T, ES, "MIDI MAP", EW, c_white);
    char b[12];
    char *e = put_str(b, "P");
    e = put_num(e, page + 1);
    put_str(e, "/4");
    sf_text(r, OP1_Q10(238), Y_T, ES, b, EW, c_white);
    for (int k = 0; k < 4; k++) {
        int i = page * 4 + k;
        const mm2_row *w = mm2_row_get(i);
        int cur_ = i == cur;
        const uint8_t *col = cur_ ? c_black : c_white;         /* curseur: typo noire sur fond */
        const uint8_t *csrc = cur_ ? c_black : c_blue;
        const uint8_t *cdst = cur_ ? c_black : c_green;
        const uint8_t *csl = cur_ ? c_black : c_red;
        int y = Y0 + k * DY;
        if (cur_) {                                            /* surbrillance: fond = couleur de colonne */
            mm_cell(r, 5, 32, y, c_red);
            mm_cell(r, 32, 88, y, c_blue);
            mm_cell(r, 88, 138, y, c_blue);
            mm_cell(r, 138, 236, y, c_green);
            mm_cell(r, 236, 314, y, c_white);
        }
        put_num(b, i + 1);
        sf_text(r, X_SL, y, ES, b, EW, csl);
        sf_text(r, X_ST, y, ES, st_names[w->stype], EW, csrc);
        const char *stTxt;                                     /* colonne type contextuelle */
        if (i == cur && mm2_edit_learning()) { stTxt = "LRN"; stCol_v = c_black; }
        else if (i == cur && sh_held) { stTxt = rowChDisp(w); stCol_v = c_black; }
        else { stTxt = st_names[w->stype]; stCol_v = csrc; }
        sf_text(r, X_ST, y, ES, stTxt, EW, stCol_v);
        put_num(b, w->snum);
        sf_text(r, X_SN, y, ES, b, EW, csrc);
        e = put_str(b, dst_names[w->dst]);                     /* destination + numéro fusionnés */
        int lo, hi;
        mm2_dnum_range(w->dst, &lo, &hi);
        if (hi > lo) { *e++ = ' '; put_num(e, w->dnum); }
        sf_text(r, X_DST, y, ES, b, EW, cdst);
        sf_text(r, X_CH, y, ES, fade_names[w->fade], EW, col); /* fade de la ligne (blanc) */
    }
}

/* ---- cycle de vie ---- */
static void event(int id, int32_t arg)
{
    wire();
    if (id == OP1_EV_BOOT_DONE) {
        op1_switch(FEAT, "MIDI MAP");            /* après les built-ins: 2e rangée = blanche */
        mm2_load();
        takes_dirty = 1;
    } else if (id == OP1_EV_MODE) {
        cur_mode = (uint8_t)arg;
    } else if (id == OP1_EV_SERVICE) {
        drain();
    }
}

static void tick(const op1_beat_t *beat)         /* audio thread: le fade */
{
    (void)beat;
    fade_tick();
}

static const char *const claims[] = { "key:clock:white", 0 };
const op1_feature_t FEATURE_op1midi = {
    .api = OP1_FEATURE_API,
    .priority = 300,
    .name = FEAT,
    .note_on = pip_note_on,
    .note_off = pip_note_off,
    .event = event,
    .tick = tick,
    .input = input,
    .midi_raw = midi_raw,
    .overlay = overlay,
    .claims = claims,
};

/* câblage ABI: au premier point d'entrée (les events n'arrivent qu'après enregistrement) */
static void wire(void)
{
    static uint8_t wired;
    if (wired) return;
    wired = 1;
    mm2_setting_get = mm_get;
    mm2_setting_put = mm_put;
    mm2_act_snd = act_snd;
    mm2_act_tape = act_tape;
    mm2_act_mute = act_mute;
    mm2_act_marker = act_marker;
    mm2_act_mnext = act_mnext;
    mm2_act_mprev = act_mprev;
    mm2_env_query = env_query;
}
