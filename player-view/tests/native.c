/* SPDX-License-Identifier: GPL-3.0-only */
#define PV_TEST
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include "../azmod-mod.h"
static uintptr_t view_vt[8],field_vt[8],box_vt[8],owner_vt[8],preview_vt[8],pi_vt[8],sw_vt[8],other_vt[8];
#define FW_VIEW ((uintptr_t)(view_vt+2))
#define FW_FIELD ((uintptr_t)(field_vt+2))
#define FW_BOX ((uintptr_t)(box_vt+2))
#define FW_OWNER ((uintptr_t)(owner_vt+2))
#define FW_PREVIEW ((uintptr_t)(preview_vt+2))
#define FW_PI2 ((uintptr_t)(pi_vt+2))
#define FW_PI4 ((uintptr_t)(pi_vt+2))
#define FW_SW2 ((uintptr_t)(sw_vt+2))
#define FW_SW4 ((uintptr_t)(sw_vt+2))
#define FW_FX_BODY ((uintptr_t)(other_vt+2))
#define FW_FX_CONTAINER ((uintptr_t)(other_vt+4))
#define FW_INFO ((uintptr_t)(other_vt+3))
#define FW_RO_BEGIN ((uintptr_t)0x10000)
#define FW_RO_END ((uintptr_t)0x1000000000000ULL)
static void mock_children(void *,void *);
static void mock_relayout(uintptr_t);
static void mock_repaint(void *,const int *);
static void mock_save(void *);
static void mock_restore(void *);
static int mock_clip(void *,const int *);
static void mock_transform(void *,const float *);
static void mock_colour(void *,const uint32_t *);
static void mock_fill(void *,float,float,float,float);
#define FW_PAINT_ENTIRE ((uintptr_t)0x123456)
static _Alignas(8) unsigned char at_bytes[32];
static uint64_t fixture_fingerprint;
#define FW_COMPONENT_ENTRY ((uintptr_t)at_bytes)
#define FW_COMPONENT_AT ((uintptr_t)(at_bytes+8))
#define FW_COMPONENT_FNV fixture_fingerprint
#define FW_PAINT_CHILDREN ((uintptr_t)mock_children)
#define FW_RELAYOUT ((uintptr_t)mock_relayout)
#define FW_REPAINT ((uintptr_t)mock_repaint)
#define FW_G_SAVE ((uintptr_t)mock_save)
#define FW_G_RESTORE ((uintptr_t)mock_restore)
#define FW_G_CLIP ((uintptr_t)mock_clip)
#define FW_G_TRANSFORM ((uintptr_t)mock_transform)
#define FW_G_COLOUR ((uintptr_t)mock_colour)
#define FW_G_FILL ((uintptr_t)mock_fill)
#include "../player.c"
static int checks;
#define CHECK(expr) do {checks++;if(!(expr)){fprintf(stderr,"failed line %d: %s\n",__LINE__,#expr);exit(1);}}while(0)
static _Alignas(8) unsigned char root[0x500],fields[4][0x100],owners[4][0x19a00],footers[4][0x200],previews[4][0x100];
static uintptr_t root_kids[8],field_kids[4][1],footer_kids[4][1];
static int row_values[8],nrows,original_calls,native_calls,saves,relayouts,repaints,shown,clips,texts,nsurfaces;
static int view_kind=AZ_VIEW_DECK;
static float last_matrix[6];
static void ptr(void *p,int off,uintptr_t v){memcpy((char *)p+off,&v,8);}
static void num(void *p,int off,int v){memcpy((char *)p+off,&v,4);}
static void component(void *p,uintptr_t vt,uintptr_t par,int x,int y,int w,int h){
 ptr(p,0,vt);ptr(p,0x18,par);num(p,0x20,x);num(p,0x24,y);num(p,0x28,w);num(p,0x2c,h);*((unsigned char *)p+0xc0)=2;
}
static void graph(void){
 memset(root,0,sizeof root);memset(fields,0,sizeof fields);memset(owners,0,sizeof owners);
 memset(footers,0,sizeof footers);memset(previews,0,sizeof previews);
 box_vt[0]=(uintptr_t)-(intptr_t)0x198c8;
 component(root,FW_VIEW,0,0,0,1280,800);ptr(root,0x40,(uintptr_t)root_kids);num(root,0x50,8);
 for(int d=0;d<4;d++){
  component(fields[d],FW_FIELD,(uintptr_t)root,0,96+d*100,1280,100);
  ptr(fields[d],0x40,(uintptr_t)field_kids[d]);num(fields[d],0x50,1);
  ptr(owners[d],0,FW_OWNER);num(owners[d],0x58,15);num(owners[d],0x197f0,d);
  uintptr_t box=(uintptr_t)owners[d]+0x198c8;
  component((void *)box,FW_BOX,(uintptr_t)fields[d],172,0,838,86);field_kids[d][0]=box;
  component(footers[d],FW_PI4,(uintptr_t)root,0,600+d*40,640,86);num(footers[d],0x128,d);
  ptr(footers[d],0x40,(uintptr_t)footer_kids[d]);num(footers[d],0x50,1);
  component(previews[d],FW_PREVIEW,(uintptr_t)footers[d],0,10,460,60);footer_kids[d][0]=(uintptr_t)previews[d];
  root_kids[d]=(uintptr_t)fields[d];root_kids[d+4]=(uintptr_t)footers[d];
 }
}
static const char *tag(void){return PV_FW;}
static int reg_screen(const char *id,const az_screen_v1 *s){(void)id;CHECK(s->on_draw!=NULL);return 0;}
static int view(void){return view_kind;}
static int reg(const char *id,const az_row_v1 *r){CHECK(!strcmp(id,MOD_ID));int n=nrows++;row_values[n]=r->kind==AZ_ROW_CHOICE?r->default_choice:0;return n;}
static int value(int r){return r>=0&&r<8?row_values[r]:-1;}
static int set(int r,int v){row_values[r]=v;return 0;}
static int lookup(const char *key){(void)key;return -1;}
static az_surface *create(const char *id,const az_surface_v1 *s){(void)id;CHECK(s->views==AZ_VIEW_BIT(AZ_VIEW_DECK));return (az_surface *)(uintptr_t)(++nsurfaces);}
static void place(az_surface *s,az_rect r){(void)s;CHECK(r.y>=96&&r.h>=30&&r.h<160&&r.w>=900);}
static void show(az_surface *s,int v){int bit=1<<((int)(uintptr_t)s-1);if(v)shown|=bit;else shown&=~bit;}
static void redraw(az_surface *s){(void)s;}
static int filter(const char *id,unsigned mask,az_frame_fn fn,void *user){(void)id;(void)fn;(void)user;CHECK(mask==(1u<<AZ_BUS_ERP_IN));return 0;}
static void orig(void *s,void *g,int a){(void)s;(void)g;(void)a;original_calls++;}
static void *at_orig(void *self,const int *point){(void)point;return self;}
static int claim(const char *id,uintptr_t a,void *fn,void **out){(void)id;(void)fn;CHECK(a==FW_PAINT_ENTIRE || a==FW_COMPONENT_AT);*out=a==FW_PAINT_ENTIRE?(void *)orig:(void *)at_orig;return 0;}
static int touch_filter(const char *id,az_touch_fn fn,void *user){(void)id;(void)user;CHECK(fn!=NULL);return 0;}
static void txt(az_draw *d,int x,int y,const char *s,int px,uint32_t c){(void)d;(void)x;(void)y;(void)s;(void)c;CHECK(px>=8&&px<=96);texts++;}
static int width(const char *s,int px){return (int)strlen(s)*px/2;}
static void fill_sdk(az_draw *d,int x,int y,int w,int h,uint32_t c){(void)d;(void)x;(void)y;(void)w;(void)h;(void)c;}
static void line_sdk(az_draw *d,int x,int y,int x1,int y1,int w,uint32_t c){(void)d;(void)x;(void)y;(void)x1;(void)y1;(void)w;(void)c;}
static int image_sdk(az_draw *d,int x,int y,const unsigned char *p,int w,int h){
 (void)d;(void)x;(void)y;CHECK(w==1280&&h==800);
 struct presented s;snapshot(&s);
 for(int i=0;i<s.count;i++){
  struct layout l=geometry(s.width,s.count,i);
  CHECK(p[(l.wave.y*1280+l.wave.x)*4+3]==0);
  CHECK(p[(l.preview.y*1280+l.preview.x)*4+3]==0);
 }
 return 0;
}
static int info(int d,az_deck_info *o){o->loaded=1;o->pos_s=10+d;o->len_s=240;o->bpm=150+d;snprintf(o->title,128,"Track %d",d+1);return 0;}
static int grid(int d,double *beat,double *bar){*beat=d*.1;*bar=-1;return 0;}
static void mock_children(void *s,void *g){(void)g;CHECK(sane((uintptr_t)s));native_calls++;}
static void mock_relayout(uintptr_t s){CHECK(s==(uintptr_t)root);relayouts++;}
static void mock_repaint(void *s,const int *r){CHECK(s==(void *)root&&r[1]==96);repaints++;}
static void mock_save(void *g){(void)g;saves++;}
static void mock_restore(void *g){(void)g;saves--;CHECK(saves>=0);}
static int mock_clip(void *g,const int *r){(void)g;CHECK(valid_rect((rect){r[0],r[1],r[2],r[3]},1280,800));clips++;return 1;}
static void mock_transform(void *g,const float *m){(void)g;memcpy(last_matrix,m,sizeof last_matrix);CHECK(isfinite(m[0])&&isfinite(m[4]));}
static void mock_colour(void *g,const uint32_t *c){(void)g;CHECK((*c&0xff000000u)==0xff000000u);}
static void mock_fill(void *g,float x,float y,float w,float h){(void)g;CHECK(x==0&&y==96&&w>=900&&h==704);}
static unsigned stress_done,stress_bad;
static void *publisher(void *unused) {
 (void)unused;
 for(int i=0;i<20000;i++) {
  int width=900+i%381;
  struct presented p={.width=width,.count=4,.focus=i%4,.left=0,.right=1,.valid=1,.stamp=(uint64_t)width*13};
  publish(&p);
 }
 STORE(&stress_done,1);return NULL;
}
static void *reader(void *unused) {
 (void)unused;
 while(!LOAD(&stress_done)) {
  struct presented p;
  if(snapshot(&p)&&p.valid&&p.stamp!=(uint64_t)p.width*13)STORE(&stress_bad,1);
 }
 return NULL;
}
int main(void){
 az_core_v1 core={.abi=AZ_MOD_ABI,.size=sizeof core,.fw_tag=tag,.register_screen=reg_screen,.register_row=reg,.row_value=value,.row_set=set,.row_lookup=lookup,.view=view,
  .surface_create=create,.surface_place=place,.surface_show=show,.surface_redraw=redraw,.frame_filter=filter,.claim_hook=claim,
  .touch_filter=touch_filter,.draw_text=txt,.text_width=width,.fill_rect=fill_sdk,.draw_line=line_sdk,.draw_image=image_sdk,.deck_info=info,.deck_grid=grid};
 fixture_fingerprint=code_fingerprint(at_bytes);
 at_bytes[0]=1;CHECK(init(&core)==14);CHECK(nrows==0);at_bytes[0]=0;
 CHECK(hit_profile_ok());CHECK(FW_COMPONENT_AT==FW_COMPONENT_ENTRY+8);
 CHECK(init(&core)==0);graph();row_values[enable_row]=1;
 for(int choice=0;choice<3;choice++){
  row_values[count_row]=choice;native_calls=0;paint(root,(void *)1,1);
  struct presented p;CHECK(snapshot(&p)&&p.valid&&p.count==count_for(choice));CHECK(p.waves==15u || choice<2);
  CHECK(native_calls==5*count_for(choice));CHECK(!saves);tick();CHECK(shown);
  surface_paint((az_draw *)1,(az_rect){0,96,1280,704},NULL);CHECK(texts>0);
 }
 CHECK(relayouts==1&&integer((uintptr_t)root,0x3a8)==2);
 unsigned char f[32]={0};f[9]=1;frames(AZ_BUS_ERP_IN,f,32,NULL);CHECK(LOAD(&focus_deck)==2&&LOAD(&left_deck)==2);
 f[9]=0;f[17]=1;frames(AZ_BUS_ERP_IN,f,32,NULL);CHECK(LOAD(&focus_deck)==3&&LOAD(&right_deck)==3);
 view_kind=AZ_VIEW_SOURCE;int calls=native_calls;paint(root,(void *)1,1);tick();CHECK(!shown&&native_calls==calls);
 CHECK(integer((uintptr_t)root,0x3a8)==0);view_kind=AZ_VIEW_DECK;
 row_values[enable_row]=0;paint(root,(void *)1,1);CHECK(!saves);
 row_values[enable_row]=1;ptr(fields[2],0,FW_OWNER);paint(root,(void *)1,1);struct presented p;snapshot(&p);CHECK(!p.valid);
 graph();row_values[count_row]=2;paint(root,(void *)1,1);
 CHECK(component_at(root,(int[]){100,400})==NULL);
 CHECK(component_at(root,(int[]){100,30})==root);
 CHECK(component_at(root,(int[]){1400,400})==root);
 view_kind=AZ_VIEW_SOURCE;CHECK(component_at(root,(int[]){100,400})==root);view_kind=AZ_VIEW_DECK;
 struct layout hit=geometry(1280,4,0);
 unsigned before=LOAD(&phase_mask);
 CHECK(touch(&(az_touch){0,hit.compare.x+8,hit.compare.y+8,1},NULL)==AZ_TOUCH_TAKE);
 CHECK(LOAD(&phase_mask)==(before^1u));
 CHECK(touch(&(az_touch){0,hit.compare.x+12,hit.compare.y+8,1},NULL)==AZ_TOUCH_TAKE);
 CHECK(LOAD(&phase_mask)==(before^1u));
 CHECK(touch(&(az_touch){0,1400,900,0},NULL)==AZ_TOUCH_PASS);
 CHECK(touch(&(az_touch){0,hit.compare.x+8,hit.compare.y+8,1},NULL)==AZ_TOUCH_TAKE);
 CHECK(LOAD(&phase_mask)==before);
 touch(&(az_touch){0,0,0,0},NULL);
 CHECK(touch(&(az_touch){0,hit.title.x+8,hit.title.y+8,1},NULL)==AZ_TOUCH_PASS);
 CHECK(touch(&(az_touch){0,hit.preview.x+8,hit.preview.y+8,1},NULL)==AZ_TOUCH_TAKE);
 touch(&(az_touch){0,0,0,0},NULL);
 rect src[3],dst[3];CHECK(slices((rect){0,0,600,240},838,86,15,src,dst));CHECK(dst[0].h==15&&dst[2].h==15&&dst[1].h==210);
 float matrix[6];CHECK(mapping(src[1],dst[1],matrix));CHECK(fabsf(src[1].y*matrix[4]+matrix[5]-dst[1].y)<.001f);
 for(int w=900;w<=1280;w+=19)for(int n=1;n<=4;n*=2)for(int i=0;i<n;i++){
  struct layout l=geometry(w,n,i);CHECK(valid_rect(l.wave,w,800)&&valid_rect(l.preview,w,800));
 }
 CHECK(original_calls==7);
 publish(&(struct presented){0});
 pthread_t a,b;CHECK(!pthread_create(&a,NULL,publisher,NULL));CHECK(!pthread_create(&b,NULL,reader,NULL));
 CHECK(!pthread_join(a,NULL));CHECK(!pthread_join(b,NULL));CHECK(!LOAD(&stress_bad));
 printf("community native/render: %d checks passed\n",checks);return 0;
}
