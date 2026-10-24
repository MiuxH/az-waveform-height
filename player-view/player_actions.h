/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef AZMOD_PLAYER_ACTIONS_H
#define AZMOD_PLAYER_ACTIONS_H
/* AZ ErP commands verified by xdj-az-emu/tools/az-drive.py, DECK_BUTTONS.
 * These are AZ hardware bits, not CDJ MIDI codes. Never select a hardware deck. */
enum { PV_ACTION_MT=1, PV_ACTION_RANGE, PV_ACTION_MASTER, PV_ACTION_SYNC,
       PV_ACTION_RELOOP, PV_ACTION_SLIP, PV_ACTION_TEMPO_RESET, PV_ACTION_KEY_SYNC,
       PV_ACTION_QUANTIZE_SHARED, PV_ACTION_LOOP4, PV_ACTION_LOOP8 };
static inline unsigned pv_action_bits(int action) {
    static const unsigned commands[]={0,0x0808,0x0910,0x0810,0x0802,
        0x0908,0x0804,0x0904,0x0902,0x0a01,0x0c04,0x0c08};
    return action>0 && action<(int)(sizeof commands/sizeof *commands)?commands[action]:0;
}
struct pv_button_pulse { int deck, action, phase; long at; };
static inline unsigned pv_button_step(struct pv_button_pulse *s,int live,int deck,
                                      int request,int hardware,long now) {
    if (!live || hardware || (s->phase && deck!=s->deck)) {
        *s=(struct pv_button_pulse){0}; return 0;
    }
    if (!s->phase && pv_action_bits(request))
        *s=(struct pv_button_pulse){deck,request,1,now};
    if (s->phase==1 && now-s->at>=60) { s->phase=2; s->at=now; }
    if (s->phase==2 && now-s->at>=60) *s=(struct pv_button_pulse){0};
    return s->phase==1?pv_action_bits(s->action):0;
}
#endif
