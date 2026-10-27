/* MIDI CC mapping to tape marker actions (OP1midi, midi-spec.md MVP) */
#ifndef MM_H
#define MM_H
#define MM_KEY       0x40             /* private key code: run pending mapped actions (KeyHandler thread) */
#define MM_KEY_DOWN  0xA5             /* its state byte, as MX_KEY_DOWN (midi/mx.h) */
#define MM_ACT_N     3                /* actions: 0 marker next, 1 marker previous, 2 play from marker */
#define MM_CC_NEXT   80               /* default CC numbers */
#define MM_CC_PREV   81
#define MM_CC_PLAY   82
#define MM_CH_OFF    0                /* op1rsettings 'mmch': mapping disabled */
#define MM_CH_ALL    17               /* op1rsettings 'mmch': any channel */
int  mm_cc(int ch, int cc, int v);       /* midi_poll thread; 1 if the CC belongs to the mapping */
void mm_apply(void);                     /* KeyHandler thread: MM_KEY from the queue */
void mm_load(void);                      /* op1rsettings -> live table (GUI / KeyHandler thread) */
int  mm_setting(int act);                /* effective CC of an action, 0 = unassigned */
#endif
