/* MIDI CC mapping to tape marker actions (OP1midi MVP, spec midi-spec.md):
 *   CC default 80 -> next marker, 81 -> previous marker, 82 -> play from the current marker.
 * Trigger type: the action is marked once per rising edge to >= 64 (no repeat while the CC is held).
 * The mapping lives in op1rsettings ('mmch' channel 0 off / 1..16 / 17 all, 'mm0' 'mm1' 'mm2' CC
 * numbers, 0 = unassigned), edited on the settings page (screens/settings.c) or by
 * tools/op1midi_map.py. Like midi/mx.c: midi_poll (midi_cc_hook) only marks the pending actions and
 * queues the private key MM_KEY; mm_apply (KeyHandler thread) runs them:
 *   marker next / previous: tm_turn(+/-1024) (tape/markers.c: one jump per detent, wraps)
 *   play from marker: tm_locate_current() (the marker at or before the cursor), then one PLAY
 *     through the stock key dispatch, Shift cleared (as seq/seq_core.c does for eu_rec_play)
 * Guards: marker actions only in tape mode (CUR_MODE_ID 2, tape screen loaded, at least one marker);
 * play from marker only from stop (tape state 0), punch off (tape/punch.c pu_mode 0) and while the
 * count-in is not counting (tape/countin.c). Mapped CCs are consumed even when a guard skips the
 * action; CCs the stock paths use (mm_reserved) can never be mapped. Two actions on one CC: only
 * the first fires. */
#include "../op1_synth.h"
#include "mm.h"
#include "../screens/op1r_settings.h"

extern void tm_turn(int delta);                            /* tape/markers.c */
extern int  tm_locate_current(void);                       /* tape/markers.c */
extern int  tm_n;                                          /* tape/markers.c: marker count */
extern void *tm_scr;                                       /* tape/markers.c: tape screen, 0 until drawn */

#ifdef OP1_HOST
extern int host_mm_tape, host_mm_state, host_mm_punch, host_mm_count;
extern void host_mm_play(void);
#define MM_TAPE()   host_mm_tape
#define MM_STATE()  host_mm_state
#define MM_PU()     host_mm_punch
#define MM_COUNT()  host_mm_count
#define MM_PLAY()   host_mm_play()
#else
extern volatile uint8_t pu_mode;                           /* tape/punch.c */
extern int ci_counting(void);                              /* tape/countin.c */
#define CUR_MODE_ID (*(volatile int *)0xFF907D58)          /* ModeSwitcher: tape = 2 (dispatcher.c) */
#define MM_TAPE()   (CUR_MODE_ID == 2 && tm_scr)
#define MM_STATE()  (*(volatile int32_t *)0x018C17D4)      /* tape state, 0 = stopped (tape/punch.c) */
#define MM_PU()     pu_mode
#define MM_COUNT()  ci_counting()
#define MM_PLAY()   do { volatile uint8_t *sh = (volatile uint8_t *)0x01DD7F78; uint8_t s = *sh; *sh = 0; \
                         ((void (*)(int, int))0x019B6C74)(0x22, 1); ((void (*)(int, int))0x019B6C74)(0x22, 0); \
                         *sh = s; } while (0)
#endif

static uint8_t mm_ch = 1;                                  /* MM_CH_OFF / 1..16 / MM_CH_ALL */
static uint8_t mm_tab[MM_ACT_N] = { MM_CC_NEXT, MM_CC_PREV, MM_CC_PLAY };
static uint8_t mm_prev[MM_ACT_N];                          /* last CC value per action: edge detect */
static volatile uint8_t mm_pend;

static int mm_reserved(int cc)                             /* CCs the stock paths already use */
{
    return (cc >= 1 && cc <= 17) || (cc >= 21 && cc <= 26) || (cc >= 38 && cc <= 43) ||
           (cc >= 48 && cc <= 60) || (cc >= 72 && cc <= 79) || (cc >= 102 && cc <= 118);
}

void mm_load(void)                                         /* boot and after every settings change */
{
    static const char *const keys[MM_ACT_N] = { "mm0", "mm1", "mm2" };
    int ch = op1r_setting_get("mmch", 1);
    mm_ch = (uint8_t)(ch < MM_CH_OFF || ch > MM_CH_ALL ? 1 : ch);
    for (int i = 0; i < MM_ACT_N; i++) {
        static const uint8_t dflt[MM_ACT_N] = { MM_CC_NEXT, MM_CC_PREV, MM_CC_PLAY };
        int v = op1r_setting_get(keys[i], dflt[i]);
        mm_tab[i] = (uint8_t)(v < 0 || v > 127 || mm_reserved(v) ? 0 : v);
    }
    for (int i = 1; i < MM_ACT_N; i++)                     /* duplicate CC: only the first acts */
        for (int k = 0; k < i; k++)
            if (mm_tab[i] && mm_tab[i] == mm_tab[k]) mm_tab[i] = 0;
    for (int i = 0; i < MM_ACT_N; i++) mm_prev[i] = 0;
}

int mm_setting(int act) { return act >= 0 && act < MM_ACT_N ? mm_tab[act] : 0; }

#ifndef OP1_HOST
static volatile uint8_t queued;
static uint32_t irq_off(void) { uint32_t m; __asm__ volatile ("cli %0;" : "=d"(m)); return m; }
static void irq_on(uint32_t m) { __asm__ volatile ("sti %0;" : : "d"(m)); }
#endif

int mm_cc(int ch, int cc, int v)                           /* midi_poll thread (midi_cc_hook) */
{
    if (!mm_ch || cc < 0 || cc > 127 || v < 0 || v > 127) return 0;
    if (mm_ch != MM_CH_ALL && ch != mm_ch - 1) return 0;
    int act = -1;
    for (int i = 0; i < MM_ACT_N; i++) if (mm_tab[i] == cc) { act = i; break; }
    if (act < 0) return 0;                                 /* not ours: mx / stock keys keep it */
    uint8_t now = (uint8_t)v, edge = (uint8_t)(now >= 64 && mm_prev[act] < 64);
    mm_prev[act] = now;
    if (!edge) return 1;
#ifndef OP1_HOST
    uint32_t m = irq_off();
    mm_pend |= (uint8_t)(1u << act);
    int push = !queued;
    if (push) {                                            /* one pending MM_KEY at a time (as mx.c) */
        volatile uint32_t *rd = (volatile uint32_t *)0x01DD5244, *wr = (volatile uint32_t *)0x01DD5248;
        uint32_t w = *wr, n = (w + 1) & 0xFF;
        if (n != *rd && !*(volatile uint8_t *)0x01DD524D) { ((volatile uint16_t *)0x01DD5044)[w] = (uint16_t)(MM_KEY | MM_KEY_DOWN << 8); *wr = n; queued = 1; }
        else push = 0;
    }
    irq_on(m);
    if (push) ((void (*)(void))0x019C2F04)();              /* wake KeyHandler */
#else
    mm_pend |= (uint8_t)(1u << act);
#endif
    return 1;
}

static void mm_run(int act)                                /* KeyHandler thread */
{
    if (!MM_TAPE()) return;                                /* marker actions live in tape mode */
    if (act == 2) {                                        /* play from the current marker */
        if (MM_STATE() != 0 || MM_PU() || MM_COUNT()) return;
        tm_locate_current();                               /* no marker at / before the cursor: stays */
        MM_PLAY();
        return;
    }
    if (!tm_n) return;                                     /* no markers: nothing to jump to */
    tm_turn(act ? -1024 : 1024);
}

void mm_apply(void)                                        /* KeyHandler thread: MM_KEY from the queue */
{
#ifndef OP1_HOST
    uint32_t m = irq_off();
#endif
    uint8_t p = mm_pend;
    mm_pend = 0;
#ifndef OP1_HOST
    queued = 0;
    irq_on(m);
#endif
    for (int i = 0; i < MM_ACT_N; i++) if (p >> i & 1) mm_run(i);
}
