/* Host test for midi/mm.c: CC ownership, channel filter, trigger edges, guards and the action calls.
 * The action side (tm_turn / tm_locate_current / PLAY) and the tape state are mocked. */
#include "../op1_synth.h"
#include <assert.h>
#include <stdio.h>
#include "../screens/op1r_settings.c"

int host_mm_tape = 1, host_mm_state = 0, host_mm_punch = 0, host_mm_count = 0;
static int play_calls;
void host_mm_play(void) { play_calls++; }
void *tm_scr = (void *)1;
int tm_n;
static int turn_arg[16], turns;
void tm_turn(int d) { turn_arg[turns++] = d; }
static int loc_calls, loc_ret = 1;
int tm_locate_current(void) { loc_calls++; return loc_ret; }

#include "../midi/mm.c"

static void reset(void)
{
    op1r_setting_host_clear();
    mm_pend = 0; turns = 0; loc_calls = 0; play_calls = 0; loc_ret = 1;
    host_mm_tape = 1; host_mm_state = 0; host_mm_punch = 0; host_mm_count = 0;
    tm_n = 2;
    mm_load();
}

int main(void)
{
    reset();
    assert(mm_setting(0) == 80 && mm_setting(1) == 81 && mm_setting(2) == 82);   /* defaults */
    /* CCs of the stock paths and invalid values are never ours */
    assert(!mm_cc(0, 39, 127) && !mm_cc(0, 53, 100) && !mm_cc(0, 72, 10) && !mm_cc(0, 102, 5) && !mm_cc(0, 118, 64));
    assert(!mm_cc(0, 80, -1) && !mm_cc(0, 200, 10) && !mm_cc(0, 80, 128) && !mm_cc(0, -1, 10));

    /* channel filter (configured channel 1) */
    assert(!mm_cc(1, 80, 127));                            /* channel 2 */
    assert(mm_pend == 0);
    assert(mm_cc(0, 80, 127) && mm_pend == 1);             /* channel 1: act 0 pending */
    mm_apply();
    assert(turns == 1 && turn_arg[0] == 1024 && mm_pend == 0);

    /* trigger edges: once per rising edge to >= 64 */
    assert(mm_cc(0, 80, 127) && mm_cc(0, 80, 100));        /* held high: no new edge */
    mm_apply(); assert(turns == 1);
    assert(mm_cc(0, 80, 63)); mm_apply(); assert(turns == 1);                 /* below threshold */
    assert(mm_cc(0, 80, 0) && mm_cc(0, 80, 64)); mm_apply();                  /* fresh edge */
    assert(turns == 2 && turn_arg[1] == 1024);

    /* previous marker: one jump back per trigger */
    reset();
    assert(mm_cc(0, 81, 127)); mm_apply();
    assert(turns == 1 && turn_arg[0] == -1024);

    /* play from marker: locate + PLAY */
    reset();
    assert(mm_cc(0, 82, 127)); mm_apply();
    assert(loc_calls == 1 && play_calls == 1 && mm_pend == 0);
    mm_apply(); assert(play_calls == 1);                   /* drained */

    /* play from marker guards */
    reset(); host_mm_state = 10;                           /* playing */
    assert(mm_cc(0, 82, 127)); mm_apply(); assert(play_calls == 0);
    reset(); host_mm_punch = 1;                            /* punch armed */
    assert(mm_cc(0, 82, 127)); mm_apply(); assert(play_calls == 0);
    reset(); host_mm_count = 1;                            /* count-in running */
    assert(mm_cc(0, 82, 127)); mm_apply(); assert(play_calls == 0);
    reset(); host_mm_tape = 0;                             /* not in tape mode */
    assert(mm_cc(0, 82, 127)); mm_apply(); assert(play_calls == 0 && loc_calls == 0);
    reset(); loc_ret = 0;                                  /* no marker at / before the cursor */
    assert(mm_cc(0, 82, 127)); mm_apply(); assert(loc_calls == 1 && play_calls == 1);

    /* marker jumps need tape mode and at least one marker; the CC is consumed either way */
    reset(); tm_n = 0;
    assert(mm_cc(0, 80, 127)); mm_apply(); assert(turns == 0);
    reset(); host_mm_tape = 0;
    assert(mm_cc(0, 80, 127) && mm_cc(0, 81, 127)); mm_apply(); assert(turns == 0);

    /* several pending actions run in one apply */
    reset();
    assert(mm_cc(0, 80, 127) && mm_cc(0, 81, 127) && mm_cc(0, 82, 127));
    mm_apply();
    assert(turns == 2 && turn_arg[0] == 1024 && turn_arg[1] == -1024 && play_calls == 1 && loc_calls == 1);

    /* settings: duplicates, reserved CCs, out of range -> unassigned (0) */
    reset();
    op1r_setting_put("mm1", 80); mm_load();
    assert(mm_setting(1) == 0 && mm_setting(0) == 80);
    op1r_setting_put("mm0", 39); mm_load(); assert(mm_setting(0) == 0);
    op1r_setting_put("mm0", 53); mm_load(); assert(mm_setting(0) == 0);
    op1r_setting_put("mm0", 102); mm_load(); assert(mm_setting(0) == 0);
    op1r_setting_put("mm0", 200); mm_load(); assert(mm_setting(0) == 0);
    op1r_setting_put("mm0", -5); mm_load(); assert(mm_setting(0) == 0);
    op1r_setting_put("mm0", 61); mm_load(); assert(mm_setting(0) == 61);      /* free range kept */
    op1r_setting_put("mm0", 127); mm_load(); assert(mm_setting(0) == 127);

    /* channel settings: off / all / exact */
    op1r_setting_put("mmch", 0); mm_load();
    assert(!mm_cc(0, 80, 127) && mm_pend == 0);            /* disabled: nothing consumed */
    op1r_setting_put("mmch", 17); mm_load();
    assert(mm_cc(5, 80, 127)); mm_apply(); assert(turns == 1);                /* omni */
    op1r_setting_put("mmch", 4); mm_load();
    assert(!mm_cc(0, 80, 10) && mm_cc(3, 80, 127)); mm_apply(); assert(turns == 2);
    op1r_setting_put("mmch", 99); mm_load();               /* bogus saved value: channel 1 */
    assert(!mm_cc(3, 80, 127) && mm_cc(0, 80, 127));

    printf("mm_test: ok\n");
    return 0;
}
