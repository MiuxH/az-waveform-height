/* SPDX-License-Identifier: GPL-3.0-only */
/* MiuxH: independent community port of the V4 four-deck height model.
 * Native objects are borrowed only during Component::paintComponentAndChildren
 * for a validated WaveformView on EP147's
 * message thread. No core internals, stale object dereferences or I/O in hooks. */
#define _GNU_SOURCE
#include "azmod-mod.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "firmware.h"
#include "height_model.h"
#include "icon_mask.h"
#define MOD_ID "miuxh-wave-height"
static const az_core_v1 *C;
static int enabled,level,conflict=-1;
static void (*original_paint)(void *,void *);
static struct four_wave_state outer;
static struct four_wave_inner_state inner[4];
static int in_paint;
static az_rect area;
static const uintptr_t widget_offset[3]={0x90,0x87f8,0x10f60};
static const uintptr_t widget_vtable[3]={FW_BLUE,FW_RGB,FW_BAND};
static int sane(uintptr_t p) {
    /* QEMU's arm64 guest uses high 48-bit userspace addresses (ffff....),
     * unlike the real deck's 39-bit mappings. Never admit kernel pointers. */
    return p>=0x10000 && p<UINT64_C(0x1000000000000) && !(p&7);
}
static uintptr_t word(uintptr_t p,int off) {return *(const uintptr_t *)(p+off);}
static int integer(uintptr_t p,int off) {return *(const int *)(p+off);}
static void set_integer(uintptr_t p,int off,int value) {*(int *)(p+off)=value;}

struct live_wave {uintptr_t field,box,owner,component[3];int deck;};
/* All candidates originate in this callback's live JUCE child tree. Require
 * exact parents, primary vtables, virtual-base adjustment and deck identity. */
static int borrow_wave(uintptr_t view,uintptr_t field,struct live_wave *out) {
    if(!sane(field) || word(field,0)!=FW_FIELD || word(field,0x18)!=view)return 0;
    uintptr_t children=word(field,0x40);int count=integer(field,0x50);
    if(!sane(children) || count<1 || count>64)return 0;
    for(int i=0;i<count;i++) {
        uintptr_t box=word(children,8*i);
        if(!sane(box) || word(box,0)!=FW_BOX || word(box,0x18)!=field)continue;
        intptr_t adjustment=*(const intptr_t *)(FW_BOX-16);
        if(adjustment!=-(intptr_t)0x198c8)return 0;
        uintptr_t owner=box+adjustment;
        if(!sane(owner) || word(owner,0)!=FW_OWNER)return 0;
        int grid=integer(owner,0x58),pad=integer(owner,0x50);
        if(grid<FOUR_WAVE_GRID_MIN || grid>32 || pad<0 || pad>32)return 0;
        int deck=integer(owner,0x197f0);
        if(deck<0 || deck>3)return 0;
        *out=(struct live_wave){.field=field,.box=box,.owner=owner,.deck=deck};
        for(int m=0;m<3;m++) {
            uintptr_t widget=owner+widget_offset[m],component=widget+0x86a0;
            if(word(widget,0)!=widget_vtable[m] || word(component,0x18)!=box)return 0;
            int y=integer(component,0x24),h=integer(component,0x2c);
            if(y<0 || y>32 || h<48 || h>200)return 0;
            out->component[m]=component;
        }
        return 1;
    }
    return 0;
}

static void fit_inner(const struct live_wave *w,int mode,int want,int amount) {
    int live[FWI_N],out[FWI_N];
    live[FWI_H]=integer(w->box,0x2c);
    live[FWI_PAD]=integer(w->owner,0x50);live[FWI_GRID_H]=integer(w->owner,0x58);
    for(int m=0;m<3;m++) {
        live[FWI_Y0+m]=integer(w->component[m],0x24);
        live[FWI_CH0+m]=integer(w->component[m],0x2c);
    }
    if(!four_wave_inner_update(&inner[w->deck],w->owner,live,mode,want,amount,
                              integer(w->field,0x2c),out))return;
    if(out[FWI_GRID_H]<FOUR_WAVE_GRID_MIN || out[FWI_H]-out[FWI_PAD]<=2*out[FWI_GRID_H])return;
    set_integer(w->owner,0x50,out[FWI_PAD]);set_integer(w->owner,0x58,out[FWI_GRID_H]);
    if(live[FWI_H]!=out[FWI_H] || live[FWI_PAD]!=out[FWI_PAD])
        ((void (*)(uintptr_t,int))FW_HEIGHT)(w->owner,out[FWI_H]);
    for(int m=0;m<3;m++) {
        uintptr_t comp=w->component[m];
        if(integer(comp,0x24)!=out[FWI_Y0+m] || integer(comp,0x2c)!=out[FWI_CH0+m])
            ((void (*)(uintptr_t,int,int,int,int))FW_BOUNDS)(comp,integer(comp,0x20),
                  out[FWI_Y0+m],integer(comp,0x28),out[FWI_CH0+m]);
    }
}

static void apply_height(uintptr_t view) {
    if(!sane(view) || word(view,0)!=FW_VIEW || C->view()!=AZ_VIEW_DECK)return;
    int mode=integer(view,0x3a8);if(mode<0 || mode>2)return;
    uintptr_t children=word(view,0x40);int count=integer(view,0x50);
    if(!sane(children) || count<1 || count>128)return;
    int n=0;unsigned seen=0;
    for(int i=0;i<count;i++) {
        uintptr_t child=word(children,8*i);
        if(!sane(child) || word(child,0)!=FW_FIELD)continue;
        struct live_wave wave;
        if(!borrow_wave(view,child,&wave) || n==4 || (seen&(1u<<wave.deck)))return;
        seen|=1u<<wave.deck;n++;
    }
    /* Unknown/partial structures pass through before any geometry is written. */
    if(n!=4 || seen!=15)return;
    int want=__atomic_load_n(&enabled,__ATOMIC_ACQUIRE);
    int amount=__atomic_load_n(&level,__ATOMIC_ACQUIRE);
    if(conflict>=0 && C->row_value(conflict)>0)want=0;
    int live[5],out[5];live[0]=integer(view,0x194);
    for(int i=0;i<4;i++)live[i+1]=integer(view,0x1a0+4*i);
    if(four_wave_update(&outer,view,live,mode,want,amount,out)) {
        set_integer(view,0x194,out[0]);
        for(int i=0;i<4;i++)set_integer(view,0x1a0+4*i,out[i+1]);
        if(mode==2)((void (*)(uintptr_t))FW_RELAYOUT)(view);
    }
    /* Relayout can rebuild children. Borrow again, never dereference the old
     * captures after calling it. The deck is still on the same message thread. */
    children=word(view,0x40);count=integer(view,0x50);
    if(!sane(children) || count<1 || count>128)return;
    for(int i=0;i<count;i++) {
        struct live_wave current;
        if(borrow_wave(view,word(children,8*i),&current))fit_inner(&current,mode,want,amount);
    }
}

static void paint(void *self,void *graphics) {
    /* This generic entry is also called for children. Restrict all geometry
     * work to the exact native WaveformView. Keep every original paint call. */
    uintptr_t p=(uintptr_t)self;
    if(!in_paint && sane(p) && word(p,0)==FW_VIEW){in_paint=1;apply_height(p);in_paint=0;}
    if(original_paint)original_paint(self,graphics);
}

static void save_level(int value) {
    if(value<0)value=0;
    if(value>10)value=10;
    __atomic_store_n(&level,value,__ATOMIC_RELEASE);
    C->cfg_set(MOD_ID,"height",value);C->request_redraw();
}
static void toggle(void) {
    int value=!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE);
    __atomic_store_n(&enabled,value,__ATOMIC_RELEASE);
    C->cfg_set(MOD_ID,"enabled",value);C->request_redraw();
}
static void draw(az_draw *d,az_rect r,void *user) {
    (void)user;area=r;
    if(r.w<320 || r.h<450)return;
    uint32_t text=C->color(AZ_COL_TEXT),hi=C->color(AZ_COL_HI),dim=C->color(AZ_COL_TEXT_DIM);
    C->draw_header(d,"4-Deck Waveform Height");
    int x=r.x+24,y=r.y+90,w=r.w-48;
    C->draw_checkbox(d,x,y,36,__atomic_load_n(&enabled,__ATOMIC_ACQUIRE),0);
    C->draw_text(d,x+54,y,"Enable",26,text);
    int amount=__atomic_load_n(&level,__ATOMIC_ACQUIRE);char label[64];
    snprintf(label,sizeof label,"Height: %d%%",amount*10);
    C->draw_text(d,x,y+74,label,28,text);
    C->fill_rect(d,x,y+146,w,8,dim);
    C->fill_rect(d,x,y+146,w*amount/10,8,hi);
    for(int i=0;i<=10;i++)C->fill_rect(d,x+(w-2)*i/10,y+137,2,26,dim);
    C->fill_rect(d,x+(w-24)*amount/10,y+128,24,44,hi);
    C->draw_text(d,x,y+194,"Original",20,dim);
    C->draw_text(d,x+w-140,y+194,"Maximum",20,dim);
    C->draw_text(d,x,y+242,w>=840?"Tap the slider or turn the browse knob. BACK returns.":"Tap / knob, BACK.",20,text);
    C->draw_text(d,x,y+280,w>=700?"Changes only the native 4-DECK waveform layout.":"4-DECK only.",18,dim);
}
static int input(const az_event *e,void *user) {
    (void)user;
    if(e->kind==AZ_EV_BACK)return 0;
    if(area.w<320 || area.h<450)return 0;
    if(e->kind==AZ_EV_KNOB){save_level(__atomic_load_n(&level,__ATOMIC_ACQUIRE)+e->dir);return 1;}
    if(e->kind==AZ_EV_CLICK){toggle();return 1;}
    if(e->kind!=AZ_EV_TAP)return 0;
    int x=area.x+24,y=area.y+90,w=area.w-48;
    if(e->x<x || e->x>x+w)return 0;
    if(e->y>=y-8 && e->y<=y+52){toggle();return 1;}
    if(w>0 && e->y>=y+112 && e->y<=y+190){save_level(((e->x-x)*10+w/2)/w);return 1;}
    return 0;
}
static const az_screen_v1 screen={.size=sizeof(az_screen_v1),.on_draw=draw,.on_input=input};
static int init(const az_core_v1 *core) {
    C=core;
    if(!core || core->abi!=AZ_MOD_ABI || !AZ_HAS(core,claim_hook) || !AZ_HAS(core,register_row) ||
       !AZ_HAS(core,view) || !AZ_HAS(core,row_lookup) || !AZ_HAS(core,row_value) ||
       !AZ_HAS(core,fw_tag) || !AZ_HAS(core,register_screen) || !AZ_HAS(core,cfg_get) ||
       !AZ_HAS(core,cfg_set) || !AZ_HAS(core,request_redraw) || !AZ_HAS(core,draw_header) ||
       !AZ_HAS(core,draw_text) || !AZ_HAS(core,fill_rect) || !AZ_HAS(core,draw_checkbox) || !AZ_HAS(core,color))return 1;
    if(!core->fw_tag() || strcmp(core->fw_tag(),HEIGHT_FW))return 2;
    __atomic_store_n(&enabled,!!C->cfg_get(MOD_ID,"enabled",0),__ATOMIC_RELEASE);
    int saved=C->cfg_get(MOD_ID,"height",0);if(saved<0)saved=0;if(saved>10)saved=10;
    __atomic_store_n(&level,saved,__ATOMIC_RELEASE);
    conflict=C->row_lookup("four-waveforms");
    if(C->register_screen(MOD_ID,&screen))return 3;
    az_row_v1 row={.size=sizeof row,.key="height",.label="4-Deck Waveform Height",
        .kind=AZ_ROW_SCREEN,.screen=&screen,.icon=waveform_icon,.help="Native waveform height in 4 DECK; Enable and 0-100%."};
    if(C->register_row(MOD_ID,&row)<0)return 4;
    /* EP147 2.00 vtables live in RX .rodata, outside claim_vslot's data-only
     * contract. Use the supported code-hook service at the verified native
     * component paint dispatcher, retaining the core's WaveformView hooks.
     * Ownership/range/late-hook checks still apply; no raw vtable writes. */
    if(C->claim_hook(MOD_ID,FW_COMPONENT_PAINT,(void *)paint,(void **)&original_paint))return 5;
    return 0;
}
static const char *const tags[]={HEIGHT_FW,NULL};
static const az_mod_v1 mod={.abi=AZ_MOD_ABI,.size=sizeof(az_mod_v1),.id=MOD_ID,.fw_tags=tags,.init=init};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void){return &mod;}
