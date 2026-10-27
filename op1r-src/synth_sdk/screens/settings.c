/* Settings page (clock / card + T3, docs/25), in the style of the playmode page like the USB out page:
 * blue "COUNT-IN" with 4 / 8 / 16 beats (tape/countin.c); green "MIDI" channel for the CC mapping
 * (midi/mm.c: off / 1..16 / all) with the play-from-marker CC shown read-only (edited by
 * tools/op1midi_map.py); white "NEXT" and red "PREV": the CC numbers of the marker actions, 0 = "--"
 * unassigned. Turning wraps within each row. Saved in op1rsettings (keys 'countin', 'mmch', 'mm0',
 * 'mm1'; 'mm2' stays as the tool wrote it). */
#include "../op1_synth.h"
#include "stroke_font.h"
#include "op1r_settings.h"
#include "../midi/mm.h"

extern uint8_t ci_beats;
static const uint8_t ci_vals[3] = { 4, 8, 16 };
static uint8_t ci_idx;                                      /* index into ci_vals */
static uint8_t ch_idx = 1;                                  /* 0 off, 1..16 channel, MM_CH_ALL */
static uint8_t cc0 = 80, cc1 = 81;                          /* CC numbers of next / previous, 0 = off */

static void st_load(void)
{
    int v = op1r_setting_get("countin", -1); if (v >= 0 && v <= 2) ci_idx = (uint8_t)v;
    v = op1r_setting_get("mmch", -1); if (v >= 0 && v <= MM_CH_ALL) ch_idx = (uint8_t)v;
    v = op1r_setting_get("mm0", -1); if (v >= 0 && v <= 127) cc0 = (uint8_t)v;
    v = op1r_setting_get("mm1", -1); if (v >= 0 && v <= 127) cc1 = (uint8_t)v;
    mm_load();                                              /* refresh the live mapping (midi/mm.c) */
}
static void st_save(void)                                   /* KeyHandler thread (settings_flush) */
{
    op1r_setting_put("countin", ci_idx);
    op1r_setting_put("mmch", ch_idx);
    op1r_setting_put("mm0", cc0);
    op1r_setting_put("mm1", cc1);
    mm_load();
}

static int32_t acc;
static uint8_t st_dirty;
void settings_flush(void) { if (st_dirty) { st_dirty = 0; st_save(); } }   /* at the next key event (as scale_flush) */
void settings_turn(int knob, int delta)
{
    if (knob > 3) return;
    if ((acc > 0 && delta < 0) || (acc < 0 && delta > 0)) acc = 0;
    acc += delta;
    int st = acc / 1024; if (!st) return;
    acc = 0;
    if (knob == 0) {                                        /* blue: count-in, clamped */
        int v = ci_idx + (st > 0 ? 1 : -1);
        if (v < 0 || v > 2) return;
        ci_idx = (uint8_t)v; ci_beats = ci_vals[ci_idx];
    } else if (knob == 1) {                                 /* green: channel, wraps */
        int v = ch_idx + (st > 0 ? 1 : -1);
        if (v < MM_CH_OFF) v = MM_CH_ALL;
        if (v > MM_CH_ALL) v = MM_CH_OFF;
        ch_idx = (uint8_t)v;
    } else {                                                /* white / red: CC number, wraps */
        uint8_t *cc = knob == 2 ? &cc0 : &cc1;
        *cc = (uint8_t)(st > 0 ? (*cc == 127 ? 0 : *cc + 1) : (*cc ? *cc - 1 : 127));
    }
    st_dirty = 1;
}

static const uint8_t c_blue[4] = { 0x4D, 0x9E, 0xFF, 0xFF };
static const uint8_t c_green[4] = { 0x00, 0xED, 0x95, 0xFF }, c_white[4] = { 0xFF, 0xFF, 0xFF, 0xFF },
                     c_red[4] = { 0xFF, 0x3A, 0x5D, 0xFF };
#define ST_S   OP1_Q10(5) / 2                               /* as the USB out page: 15 px caps */
#define ST_W   0x600
#define ST_LX  OP1_Q10(24)
#define ST_VX  OP1_Q10(164)
#define ST_PX  OP1_Q10(216)
#define ST_Y0  OP1_Q10(40)
#define ST_DY  OP1_Q10(30)

static char *st_num(char *e, int v)                         /* 0..127, 0 terminated */
{
    if (v >= 100) *e++ = (char)('0' + v / 100);
    if (v >= 10) *e++ = (char)('0' + v / 10 % 10);
    *e++ = (char)('0' + v % 10);
    *e = 0;
    return e;
}

static void st_init(void *scr) { (void)scr; st_load(); ci_beats = ci_vals[ci_idx]; }
static void st_update(void *scr, int dt)
{
    op1_screen_draw(scr, dt);
    void *r = op1_raster();
    static char nch[6], n0[6], n1[6], np[8];
    static const char *const txt[3] = { "4", "8", "16" };
    sf_text(r, ST_LX, ST_Y0, ST_S, "COUNT-IN", ST_W, c_blue);
    sf_text(r, ST_VX, ST_Y0, ST_S, txt[ci_idx], ST_W, c_blue);
    if (ch_idx == MM_CH_OFF) { nch[0] = 'O'; nch[1] = 'F'; nch[2] = 'F'; nch[3] = 0; }
    else if (ch_idx == MM_CH_ALL) { nch[0] = 'A'; nch[1] = 'L'; nch[2] = 'L'; nch[3] = 0; }
    else st_num(nch, ch_idx);
    sf_text(r, ST_LX, ST_Y0 + ST_DY, ST_S, "MIDI", ST_W, c_green);
    sf_text(r, ST_VX, ST_Y0 + ST_DY, ST_S, nch, ST_W, c_green);
    int p = mm_setting(2);                                  /* play from marker: shown, edited by the tool */
    if (p) { np[0] = 'P'; np[1] = ' '; st_num(np + 2, p); }
    else { np[0] = 'P'; np[1] = ' '; np[2] = '-'; np[3] = '-'; np[4] = 0; }
    sf_text(r, ST_PX, ST_Y0 + ST_DY, ST_S, np, ST_W, c_green);
    if (cc0) st_num(n0, cc0); else { n0[0] = '-'; n0[1] = '-'; n0[2] = 0; }
    sf_text(r, ST_LX, ST_Y0 + 2 * ST_DY, ST_S, "NEXT", ST_W, c_white);
    sf_text(r, ST_VX, ST_Y0 + 2 * ST_DY, ST_S, n0, ST_W, c_white);
    if (cc1) st_num(n1, cc1); else { n1[0] = '-'; n1[1] = '-'; n1[2] = 0; }
    sf_text(r, ST_LX, ST_Y0 + 3 * ST_DY, ST_S, "PREV", ST_W, c_red);
    sf_text(r, ST_VX, ST_Y0 + 3 * ST_DY, ST_S, n1, ST_W, c_red);
}
const op1_screen_t SCREEN_settings = { st_init, st_update };
