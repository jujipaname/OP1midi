/* Copyright (C) 2026 OP1midi. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
/* MM2 noyau v3: 16 lignes (source NOTE/CC/PC -> destination SND/TAPE/MUTE/MRK/M+/M-), dispatch,
 * gardes, actions via hooks, persistance versionnée ('mmv'), canal 'mmch', fade 'mmfade'.
 * Voir mm2.h pour le contrat complet. Aucune dépendance: l'intégration fournit
 * mm2_setting_get/put, mm2_act_*, mm2_env_query. */
#include "mm2.h"

static mm2_row rows[MM2_ROWS];
static uint8_t mm_en = 1;
static uint8_t prev[MM2_ROWS];             /* dernière valeur CC par ligne (front) */

/* ABI d'intégration: définies ici, assignées par l'hôte au démarrage (mm2.h) */
int  (*mm2_setting_get)(const char *key, int dflt);
void (*mm2_setting_put)(const char *key, int val);
void (*mm2_act_snd)(int key);
void (*mm2_act_tape)(int tape);
void (*mm2_act_mute)(int track, int fade, int fmask);
void (*mm2_act_marker)(int local, int fade, int fmask);
void (*mm2_act_mnext)(int fade, int fmask);
void (*mm2_act_mprev)(int fade, int fmask);
void (*mm2_env_query)(mm2_env *e);

/* clés constantes: les implémentations hôte des réglages stockent le pointeur */
static const char *const rowkeys[MM2_ROWS] = {
    "mmr0", "mmr1", "mmr2", "mmr3", "mmr4", "mmr5", "mmr6", "mmr7",
    "mmr8", "mmr9", "mmr10", "mmr11", "mmr12", "mmr13", "mmr14", "mmr15"
};

int mm2_row_pack(const mm2_row *r)
{
    if (!r || r->ch > MM2_CH_ALL || r->stype > MM2_S_MAX || r->snum > 127 ||
        r->dst > MM2_D_MAX || r->fade > 4 || r->fmask > MM2_FM_MAX) return -1;
    return (int)((uint32_t)r->ch | (uint32_t)r->stype << 5 | (uint32_t)r->snum << 7 |
                 (uint32_t)r->dst << 14 | (uint32_t)r->dnum << 17 | (uint32_t)r->fade << 25 |
                 (uint32_t)r->fmask << 28);
}

mm2_row mm2_row_unpack(int v)
{
    mm2_row r;
    uint32_t u = (uint32_t)v;
    r.ch = (uint8_t)(u & 31);
    r.stype = (uint8_t)(u >> 5 & 3);
    r.snum = (uint8_t)(u >> 7 & 127);
    r.dst = (uint8_t)(u >> 14 & 7);
    r.dnum = (uint8_t)(u >> 17 & 255);
    r.fade = (uint8_t)(u >> 25 & 7);
    r.fmask = (uint8_t)(u >> 28 & 7);
    if (r.ch > MM2_CH_ALL || r.stype > MM2_S_MAX || r.dst > MM2_D_MAX || r.fade > 4 ||
        r.fmask > MM2_FM_MAX) {
        r.ch = r.stype = r.snum = r.dnum = r.fade = r.fmask = 0; r.dst = MM2_D_OFF;
    }
    return r;
}

const mm2_row *mm2_row_get(int i) { return i >= 0 && i < MM2_ROWS ? &rows[i] : 0; }
mm2_row *mm2_row_edit(int i) { return i >= 0 && i < MM2_ROWS ? &rows[i] : 0; }

void mm2_load(void)
{
    if (!mm2_setting_get) return;
    if (mm2_setting_get("mmv", 0) != MM2_FMT) {                /* format autre/absent: remise à zéro */
        mm2_setting_put("mmv", MM2_FMT);
        for (int i = 0; i < MM2_ROWS; i++) mm2_setting_put(rowkeys[i], 0);
    }
    mm_en = mm2_setting_get("mmen", 1) ? 1 : 0;
    for (int i = 0; i < MM2_ROWS; i++) {
        rows[i] = mm2_row_unpack(mm2_setting_get(rowkeys[i], 0));
        prev[i] = 0;
    }
}

void mm2_save(void)
{
    if (!mm2_setting_put) return;
    mm2_setting_put("mmv", MM2_FMT);
    mm2_setting_put("mmen", mm_en);
    for (int i = 0; i < MM2_ROWS; i++) mm2_setting_put(rowkeys[i], mm2_row_pack(&rows[i]));
}

int mm2_enabled(void) { return mm_en; }
void mm2_enable(int on) { mm_en = on ? 1 : 0; }
/* canal et fade sont maintenant PAR LIGNE (mm2_row.ch / mm2_row.fade) */

void mm2_dnum_range(int dst, int *lo, int *hi)
{
    *lo = dst >= MM2_D_SND && dst <= MM2_D_MRK ? 1 : 0;
    *hi = dst == MM2_D_SND ? 8 : dst == MM2_D_TAPE ? 4 : dst == MM2_D_MUTE ? 4 :
          dst == MM2_D_MRK ? MM2_MRK_MAX : 0;
}

static void mm2_run(int i)
{
    const mm2_row *r = &rows[i];
    int ch = r->ch ? r->ch : MM2_CH_ALL;                   /* 0 = non réglé: ALL */
    mm2_env e = { 0, 0, 0, 0 };
    switch (r->dst) {
    case MM2_D_SND:
        if (mm2_act_snd && r->dnum >= 1 && r->dnum <= 8) mm2_act_snd(r->dnum);
        return;
    case MM2_D_TAPE:
        if (mm2_act_tape && r->dnum >= 1 && r->dnum <= 4) mm2_act_tape(r->dnum);
        return;
    case MM2_D_MUTE:
        if (mm2_act_mute && r->dnum >= 1 && r->dnum <= 4) mm2_act_mute(r->dnum, r->fade, r->fmask);
        return;
    case MM2_D_MRK: {
        if (!mm2_env_query || !mm2_act_marker) return;
        mm2_env_query(&e);
        if (!e.tape_mode || e.punch) return;
        if (e.raw_state != 0 && e.raw_state != 3 && e.raw_state != 10) return;   /* arrêt (0 ou 3) / lecture */
        if (r->dnum > MM2_MRK_MAX || r->dnum > e.markers) return;   /* plage + marqueurs posés */
        mm2_act_marker(r->dnum, r->fade, r->fmask);
        return;
    }
    case MM2_D_MRP:
    case MM2_D_MRM: {
        if (!mm2_env_query) return;
        mm2_env_query(&e);
        if (!e.tape_mode || e.punch) return;
        if (e.raw_state != 0 && e.raw_state != 3 && e.raw_state != 10) return;
        if (!e.markers) return;
        if (r->dst == MM2_D_MRP ? mm2_act_mnext : mm2_act_mprev)
            (r->dst == MM2_D_MRP ? mm2_act_mnext : mm2_act_mprev)(r->fade, r->fmask);
        return;
    }
    default:
        return;                                                /* MM2_D_OFF: inert */
    }
    (void)ch;
}

static int mm2_match(int ch, int stype, int num, int *hit)
{
    for (int i = 0; i < MM2_ROWS; i++) {
        const mm2_row *r = &rows[i];
        if (!r->dst || r->stype != stype || r->snum != num) continue;
        int rc = r->ch ? r->ch : MM2_CH_ALL;               /* 0 = non réglé: ALL */
        if (rc != MM2_CH_ALL && rc != ch + 1) continue;
        *hit = i;
        return 1;
    }
    return 0;
}

int mm2_cc(int ch, int cc, int v)
{
    int i;
    if (!mm_en || cc < 0 || cc > 127 || v < 0 || v > 127) return 0;
    if (!mm2_match(ch, MM2_S_CC, cc, &i)) return 0;
    int fire = v >= 64 && prev[i] < 64;
    prev[i] = (uint8_t)v;
    if (fire) mm2_run(i);
    return 1;
}

int mm2_pc(int ch, int num)
{
    int i;
    if (!mm_en || num < 0 || num > 127) return 0;
    if (!mm2_match(ch, MM2_S_PC, num, &i)) return 0;
    mm2_run(i);
    return 1;
}

int mm2_note_match(int ch, int note)
{
    int i;
    if (!mm_en || note < 0 || note > 127) return 0;
    return mm2_match(ch, MM2_S_NOTE, note, &i);
}

int mm2_note(int ch, int note, int vel)
{
    int i;
    if (!mm_en || note < 0 || note > 127) return 0;
    if (vel <= 0) return mm2_match(ch, MM2_S_NOTE, note, &i);   /* note-off mappée: avalé, rien */
    if (!mm2_match(ch, MM2_S_NOTE, note, &i)) return 0;
    mm2_run(i);
    return 1;
}
