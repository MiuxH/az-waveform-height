/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#define HEIGHT_TEST
static uintptr_t vt_view[64],vt_box[64],vt_field[64],vt_owner[64],vt_blue[64],vt_rgb[64],vt_band[64];
static unsigned char *v,*owners[4],*fields[4];
static uintptr_t kids[4],fieldkids[4][1];
static int layouts,heights,bounds,paints,claims,claim_result,registered,current_view=4,official_on;
static void fake_layout(uintptr_t view);
static void fake_height(uintptr_t owner,int height);
static void fake_bounds(uintptr_t comp,int x,int y,int w,int h);
#define FW_VIEW ((uintptr_t)vt_view)
#define FW_BOX ((uintptr_t)(vt_box+2))
#define FW_FIELD ((uintptr_t)vt_field)
#define FW_OWNER ((uintptr_t)vt_owner)
#define FW_BLUE ((uintptr_t)vt_blue)
#define FW_RGB ((uintptr_t)vt_rgb)
#define FW_BAND ((uintptr_t)vt_band)
#define FW_RELAYOUT ((uintptr_t)fake_layout)
#define FW_HEIGHT ((uintptr_t)fake_height)
#define FW_BOUNDS ((uintptr_t)fake_bounds)
#include "../height.c"
static int checks;
#define CHECK(name,expr) do{checks++;if(!(expr)){fprintf(stderr,"FAIL %s\n",name);exit(1);}}while(0)
static void putp(uintptr_t at,int off,uintptr_t p){*(uintptr_t *)(at+off)=p;}
static void fake_bounds(uintptr_t comp,int x,int y,int w,int h){bounds++;set_integer(comp,0x20,x);set_integer(comp,0x24,y);set_integer(comp,0x28,w);set_integer(comp,0x2c,h);}
static void fake_height(uintptr_t owner,int h){heights++;set_integer(owner+0x198c8,0x2c,h);}
static void fake_layout(uintptr_t view){layouts++;int h=integer(view,0x3a8)==2?integer(view,0x194):198;for(int i=0;i<4;i++){set_integer((uintptr_t)fields[i],0x2c,h);set_integer((uintptr_t)owners[i]+0x198c8,0x2c,h);}}
static void fake_paint(void *self,void *g){(void)self;(void)g;paints++;}
static int service_view(void){return current_view;}
static int lookup(const char *key){(void)key;return -1;}
static int value(int h){(void)h;return official_on;}
static int get(const char *id,const char *key,int def){(void)id;(void)key;return def;}
static void set(const char *id,const char *key,int val){(void)id;(void)key;(void)val;}
static void redraw(void){}
static const char *tag(void){return HEIGHT_FW;}
static const char *bad_tag(void){return "ep147-0000000000000000";}
static int regscreen(const char *id,const az_screen_v1 *s){(void)id;(void)s;registered++;return 0;}
static int regrow(const char *id,const az_row_v1 *r){(void)id;(void)r;return 0;}
static int claim(const char *id,uintptr_t slot,void *fn,void **orig){(void)id;assert(slot==FW_VIEW+26*8);assert(fn);claims++;if(!claim_result)*orig=(void *)fake_paint;return claim_result;}
static void draw_header(az_draw *d,const char *s){(void)d;(void)s;}
static void draw_text(az_draw *d,int x,int y,const char *s,int px,uint32_t rgb){(void)d;(void)x;(void)y;(void)s;(void)px;(void)rgb;}
static void rect(az_draw *d,int x,int y,int w,int h,uint32_t rgb){(void)d;(void)x;(void)y;(void)w;(void)h;(void)rgb;}
static void checkbox(az_draw *d,int x,int y,int s,int on,int hi){(void)d;(void)x;(void)y;(void)s;(void)on;(void)hi;}
static uint32_t color(int role){(void)role;return 0xffffff;}
static az_core_v1 core={.abi=AZ_MOD_ABI,.size=sizeof(az_core_v1),.claim_vslot=claim,.register_row=regrow,.register_screen=regscreen,.view=service_view,.fw_tag=tag,.row_lookup=lookup,.row_value=value,.cfg_get=get,.cfg_set=set,.request_redraw=redraw,.draw_header=draw_header,.draw_text=draw_text,.fill_rect=rect,.draw_checkbox=checkbox,.color=color};
static void fixture(void){
    memset(&outer,0,sizeof outer);memset(inner,0,sizeof inner);in_paint=0;C=&core;enabled=1;level=10;conflict=-1;
    v=calloc(1,0x500);putp((uintptr_t)v,0,FW_VIEW);putp((uintptr_t)v,0x40,(uintptr_t)kids);set_integer((uintptr_t)v,0x50,4);set_integer((uintptr_t)v,0x3a8,2);set_integer((uintptr_t)v,0x194,92);
    vt_box[0]=(uintptr_t)(intptr_t)-0x198c8;
    for(int d=0;d<4;d++){
        set_integer((uintptr_t)v,0x1a0+4*d,80+128*d);
        owners[d]=calloc(1,0x19a00);fields[d]=calloc(1,0x100);
        uintptr_t p=(uintptr_t)owners[d],box=p+0x198c8,f=(uintptr_t)fields[d];kids[d]=f;fieldkids[d][0]=box;
        putp(p,0,FW_OWNER);putp(box,0,FW_BOX);putp(box,0x18,f);set_integer(box,0x2c,92);
        set_integer(p,0x50,16);set_integer(p,0x58,15);set_integer(p,0x197f0,d);
        putp(f,0,FW_FIELD);putp(f,0x18,(uintptr_t)v);putp(f,0x40,(uintptr_t)fieldkids[d]);set_integer(f,0x50,1);set_integer(f,0x2c,92);
        for(int m=0;m<3;m++){uintptr_t w=p+widget_offset[m],comp=w+0x86a0;putp(w,0,widget_vtable[m]);putp(comp,0x18,box);set_integer(comp,0x24,8);set_integer(comp,0x28,1220);set_integer(comp,0x2c,162);}
    }
    layouts=heights=bounds=paints=0;current_view=AZ_VIEW_DECK;original_paint=fake_paint;
}
static void release(void){free(v);for(int i=0;i<4;i++){free(owners[i]);free(fields[i]);}}
static void *change_settings(void *unused){(void)unused;for(int i=0;i<1000;i++){save_level(i%11);toggle();}return NULL;}
static void *native_frames(void *unused){(void)unused;for(int i=0;i<1000;i++)paint(v,NULL);return NULL;}
int main(void){
    az_core_v1 missing={0};CHECK("missing API refused",init(&missing)==1);
    core.fw_tag=bad_tag;CHECK("unknown firmware refused before claiming",init(&core)==2 && claims==0);core.fw_tag=tag;
    claim_result=-1;CHECK("occupied hook fails explicitly",init(&core)==5);claim_result=0;
    CHECK("valid init claims exactly the paint slot",init(&core)==0 && claims==2 && registered==2);
    fixture();paint(v,NULL);CHECK("native paint always runs",paints==1);
    CHECK("four fields grow inside original total span",integer((uintptr_t)v,0x194)==117 && layouts==1);
    CHECK("native grids stay fifteen pixels",integer((uintptr_t)owners[0],0x58)==15);
    CHECK("all native colour components fit their new drawing area",integer((uintptr_t)owners[3]+widget_offset[2]+0x86a0,0x2c)==117);
    int before=layouts+heights+bounds;paint(v,NULL);CHECK("stable frame does not resize",layouts+heights+bounds==before);
    enabled=0;paint(v,NULL);CHECK("disabled restores table and padding",integer((uintptr_t)v,0x194)==92 && integer((uintptr_t)owners[0],0x50)==16);release();
    fixture();set_integer((uintptr_t)owners[2],0x197f0,-1);paint(v,NULL);CHECK("unknown deck makes zero writes",!layouts && !bounds && !heights);release();
    fixture();set_integer((uintptr_t)owners[2],0x197f0,1);paint(v,NULL);CHECK("duplicate deck makes zero writes",!layouts && !bounds);release();
    fixture();set_integer((uintptr_t)owners[2],0x58,3);paint(v,NULL);CHECK("unsafe native ruler is refused before even outer relayout",!layouts && !bounds && !heights);release();
    fixture();current_view=AZ_VIEW_SOURCE;paint(v,NULL);CHECK("SOURCE remains unchanged",!layouts && !bounds);release();
    fixture();set_integer((uintptr_t)v,0x3a8,0);paint(v,NULL);CHECK("two-deck baseline stays unchanged",!layouts && !bounds);release();
    fixture();level=0;paint(v,NULL);CHECK("zero height keeps stock",!layouts && !bounds);release();
    fixture();paint(v,NULL);set_integer((uintptr_t)v,0x3a8,0);fake_layout((uintptr_t)v);paint(v,NULL);CHECK("leaving four decks restores padding without stale four-deck height",integer((uintptr_t)owners[0],0x50)==16 && integer((uintptr_t)owners[0]+0x198c8,0x2c)==198);release();
    fixture();conflict=1;official_on=1;paint(v,NULL);CHECK("official height mod takes priority",!layouts && !bounds);release();
    area=(az_rect){0,0,1000,600};enabled=0;level=5;
    az_event e={.kind=AZ_EV_TAP,.x=500,.y=236};input(&e,NULL);CHECK("slider selects midpoint",level==5 && !enabled);
    e.x=976;input(&e,NULL);CHECK("slider reaches maximum",level==10);
    e.kind=AZ_EV_KNOB;e.dir=1;input(&e,NULL);CHECK("knob clamps maximum",level==10);
    e.kind=AZ_EV_CLICK;input(&e,NULL);CHECK("click toggles only mod enable",enabled==1);
    e.kind=AZ_EV_BACK;CHECK("BACK belongs to host navigation",input(&e,NULL)==0);
    fixture();pthread_t ui,native;
    CHECK("settings/native fixture threads start",!pthread_create(&ui,NULL,change_settings,NULL) && !pthread_create(&native,NULL,native_frames,NULL));
    pthread_join(ui,NULL);pthread_join(native,NULL);__atomic_store_n(&enabled,0,__ATOMIC_RELEASE);paint(v,NULL);
    CHECK("concurrent settings remain bounded and restore stock",integer((uintptr_t)v,0x194)==92 && integer((uintptr_t)owners[0],0x50)==16);release();
    printf("%d community port checks passed\n",checks);return 0;
}
