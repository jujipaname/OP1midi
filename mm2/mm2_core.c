/* Copyright (C) 2026 OP1midi. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
/* MM2 noyau: table de 16 lignes, dispatch CC/PC, gardes, actions via hooks, persistance.
 * Voir mm2.h pour le contrat complet. Aucune dépendance: l'intégration fournit
 * mm2_setting_get/put, mm2_act_*, mm2_env_query. */
#include "mm2.h"

static mm2_row rows[MM2_ROWS];
static uint8_t mm_en = 1;
static uint8_t prev[MM2_ROWS];             /* dernière valeur CC par ligne (front) */

/* ABI d'intégration: définies ici, assignées par l'hôte au démarrage (mm2.h) */
int  (*mm2_setting_get)(const char *key, int dflt);
void (*mm2_setting_put)(const char *key, int val);
void (*mm2_act_next)(int dir);
void (*mm2_act_play)(void);
void (*mm2_act_tape)(int tape);
void (*mm2_act_instr)(int slot);
void (*mm2_env_query)(mm2_env *e);

int mm2_row_pack(const mm2_row *r)
{
    if (!r || r->ch > MM2_CH_ALL || r->type > 1 || r->num > 127 ||
        r->act > MM2_ACT_MAX) return -1;
    return (int)((uint32_t)r->ch | (uint32_t)r->type << 5 | (uint32_t)r->num << 6 |
                 (uint32_t)r->act << 13 | (uint32_t)r->val << 16);
}

mm2_row mm2_row_unpack(int v)
{
    mm2_row r = { 0, 0, 0, MM2_OFF, 0 };
    uint32_t u = (uint32_t)v;
    r.ch = (uint8_t)(u & 31);
    r.type = (uint8_t)(u >> 5 & 1);
    r.num = (uint8_t)(u >> 6 & 127);
    r.act = (uint8_t)(u >> 13 & 7);
    r.val = (uint8_t)(u >> 16 & 255);
    if (r.ch > MM2_CH_ALL || r.act > MM2_ACT_MAX) { r.ch = r.type = r.num = r.val = 0; r.act = MM2_OFF; }
    return r;
}

const mm2_row *mm2_row_get(int i) { return i >= 0 && i < MM2_ROWS ? &rows[i] : 0; }
mm2_row *mm2_row_edit(int i) { return i >= 0 && i < MM2_ROWS ? &rows[i] : 0; }

/* clés constantes: les implémentations hôte des réglages stockent le pointeur */
static const char *const rowkeys[MM2_ROWS] = {
    "mmr0", "mmr1", "mmr2", "mmr3", "mmr4", "mmr5", "mmr6", "mmr7",
    "mmr8", "mmr9", "mmr10", "mmr11", "mmr12", "mmr13", "mmr14", "mmr15"
};

void mm2_load(void)
{
    if (!mm2_setting_get) return;
    mm_en = mm2_setting_get("mmen", 1) ? 1 : 0;
    for (int i = 0; i < MM2_ROWS; i++) {
        rows[i] = mm2_row_unpack(mm2_setting_get(rowkeys[i], 0));
        prev[i] = 0;
    }
}

void mm2_save(void)
{
    if (!mm2_setting_put) return;
    mm2_setting_put("mmen", mm_en);
    for (int i = 0; i < MM2_ROWS; i++) mm2_setting_put(rowkeys[i], mm2_row_pack(&rows[i]));
}

int mm2_enabled(void) { return mm_en; }
void mm2_enable(int on) { mm_en = on ? 1 : 0; }

static void mm2_run(int i)
{
    const mm2_row *r = &rows[i];
    mm2_env e = { 0, 0, 0, 0, 0 };
    switch (r->act) {
    case MM2_NEXT:
    case MM2_PREV:
        if (!mm2_env_query) return;
        mm2_env_query(&e);
        if (!e.tape_mode || !e.markers) return;
        if (mm2_act_next) mm2_act_next(r->act == MM2_NEXT ? 1 : -1);
        return;
    case MM2_PLAY:
        if (!mm2_env_query) return;
        mm2_env_query(&e);
        if (!e.tape_mode || !e.stopped || e.punch || e.counting) return;
        if (mm2_act_play) mm2_act_play();
        return;
    case MM2_TAPE:
        if (mm2_act_tape && r->val >= 1 && r->val <= 4) mm2_act_tape(r->val);
        return;
    case MM2_INSTR:
        if (mm2_act_instr) mm2_act_instr(r->val);
        return;
    default:
        return;                                            /* MM2_OFF: inert */
    }
}

static int mm2_match(int ch, int type, int num, int *fire_now)
{
    for (int i = 0; i < MM2_ROWS; i++) {
        const mm2_row *r = &rows[i];
        if (!r->act || r->type != type || r->num != num) continue;
        if (r->ch != MM2_CH_ALL && r->ch != ch + 1) continue;
        *fire_now = i;
        return 1;
    }
    return 0;
}

int mm2_cc(int ch, int cc, int v)                          /* ch 0..15 */
{
    int i;
    if (!mm_en || cc < 0 || cc > 127 || v < 0 || v > 127) return 0;
    if (!mm2_match(ch, 0, cc, &i)) return 0;
    int fire = v >= 64 && prev[i] < 64;
    prev[i] = (uint8_t)v;
    if (fire) mm2_run(i);
    return 1;
}

int mm2_pc(int ch, int num)
{
    int i;
    if (!mm_en || num < 0 || num > 127) return 0;
    if (!mm2_match(ch, 1, num, &i)) return 0;
    mm2_run(i);                                            /* PC: chaque message déclenche */
    return 1;
}
