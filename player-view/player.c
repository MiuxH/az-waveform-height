/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include "azmod-mod.h"
#include "layout.h"
#include "firmware.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#define MOD_ID "miuxh-player-view"
int community_render_init(const az_core_v1 *);
void community_render(az_draw *,int,int,int,int,int,unsigned,unsigned,const az_deck_info [4]);
static const az_core_v1 *C;
static int enable_row=-1,count_row=-1,compare_row=-1;
static int conflict_rows[3]={-1,-1,-1};
static az_surface *surfaces[4]; /* title/status strips for up to two player rows */
static void (*original)(void *,void *,int);
static void *(*original_component_at)(void *,const int *);
static int ready;
static int drawing;
static uintptr_t mode_identity;
static int saved_mode;
/* No native pointer crosses threads. The presentation is a copied numeric
 * snapshot; every field is atomic, published under one sequence counter. */
struct presented {
    int width,count,focus,left,right;
    unsigned waves,previews,valid;
    uint64_t stamp;
    int fx_y,fx_h;
};
static struct {
    unsigned seq;
    int width,count,focus,left,right;
    unsigned waves,previews,valid;
    uint64_t stamp;
    int fx_y,fx_h;
} pub;
static int focus_deck,left_deck=0,right_deck=1;
static unsigned select_last[2];
static unsigned elapsed_mask,phase_mask;
static int native_conflicts;
static int active_flags[4];
static uint64_t repaint_at;
static uint64_t now_ns(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000u+t.tv_nsec;
}
#define LOAD(p) __atomic_load_n((p),__ATOMIC_ACQUIRE)
#define STORE(p,v) __atomic_store_n((p),(v),__ATOMIC_RELEASE)
static void publish(const struct presented *v) {
    __atomic_fetch_add(&pub.seq,1,__ATOMIC_ACQ_REL);
    STORE(&pub.width,v->width);STORE(&pub.count,v->count);STORE(&pub.focus,v->focus);
    STORE(&pub.left,v->left);STORE(&pub.right,v->right);STORE(&pub.waves,v->waves);
    STORE(&pub.previews,v->previews);STORE(&pub.valid,v->valid);STORE(&pub.stamp,v->stamp);
    STORE(&pub.fx_y,v->fx_y);STORE(&pub.fx_h,v->fx_h);
    __atomic_fetch_add(&pub.seq,1,__ATOMIC_RELEASE);
}
static int snapshot(struct presented *v) {
    for(int tries=0;tries<3;tries++) {
        unsigned a=LOAD(&pub.seq);if(a&1)continue;
        *v=(struct presented){LOAD(&pub.width),LOAD(&pub.count),LOAD(&pub.focus),LOAD(&pub.left),
            LOAD(&pub.right),LOAD(&pub.waves),LOAD(&pub.previews),LOAD(&pub.valid),LOAD(&pub.stamp),
            LOAD(&pub.fx_y),LOAD(&pub.fx_h)};
        if(a==LOAD(&pub.seq))return 1;
    }
    memset(v,0,sizeof *v);return 0;
}
static int sane(uintptr_t p){return p>=0x10000 && p<UINT64_C(0x1000000000000) && !(p&7);}
static uint64_t code_fingerprint(const unsigned char *bytes) {
    uint64_t h=UINT64_C(0xcbf29ce484222325);
    for(int i=0;i<32;i++)h=(h^bytes[i])*UINT64_C(0x100000001b3);
    return h;
}
static int hit_profile_ok(void) {
    return FW_COMPONENT_AT==FW_COMPONENT_ENTRY+8 &&
        code_fingerprint((const unsigned char *)FW_COMPONENT_ENTRY)==FW_COMPONENT_FNV;
}
static uintptr_t word(uintptr_t p,int n){return *(const uintptr_t *)(p+n);}
static int integer(uintptr_t p,int n){return *(const int *)(p+n);}
static int ro(uintptr_t p,size_t n){return p>=FW_RO_BEGIN && p<=FW_RO_END-n;}
static uintptr_t primary(uintptr_t comp) {
    if(!sane(comp))return 0;
    uintptr_t vt=word(comp,0);
    if(!ro(vt-16,24))return 0;
    intptr_t off=*(const intptr_t *)(vt-16);
    if(off>0 || off<-(intptr_t)0x20000)return 0;
    uintptr_t p=comp+off;
    return sane(p) && ro(word(p,0),8)?p:0;
}
static int class_name(uintptr_t comp,const char *name) {
    if(!sane(comp))return 0;
    uintptr_t vt=word(comp,0);
    if(!ro(vt-8,8))return 0;
    uintptr_t ti=word(vt,-8);
    if(!ro(ti,16))return 0;
    uintptr_t text=word(ti,8);size_t n=strlen(name)+1;
    return ro(text,n) && !memcmp((const void *)text,name,n);
}
static int children(uintptr_t p,uintptr_t *kids) {
    if(!sane(p))return 0;
    int n=integer(p,0x50);*kids=word(p,0x40);
    return n>0 && n<=128 && sane(*kids)?n:0;
}
static int visible(uintptr_t p){return !!(*(const unsigned char *)(p+0xc0)&2);}
static rect bounds(uintptr_t p){return (rect){integer(p,0x20),integer(p,0x24),integer(p,0x28),integer(p,0x2c)};}
struct native_parts {uintptr_t box[4],owner[4],preview[4],fx;rect fx_rect;int modal,visited,truncated;unsigned families;};
/* Borrow a bounded live child tree only during native message-thread paint.
 * Parent links and known primary vtables establish deck ownership. */
static void census(uintptr_t root,struct native_parts *n) {
    struct item {uintptr_t p;int deck,depth,x,y,shown;} queue[2048];
    int tail=1;
    queue[0]=(struct item){root,-1,0,0,0,1};
    memset(n,0,sizeof *n);
    /* Depth first: detailed waveform marker subtrees must not exhaust a
     * breadth-first queue before the footer/preview families are visited. */
    while(tail && n->visited<8192) {
        struct item q=queue[--tail];n->visited++;
        uintptr_t p=q.p,full=primary(p);
        if(!full)continue;
        uintptr_t vt=word(full,0);
        int deck=q.deck;
        if(vt==FW_PI2 || vt==FW_PI4)deck=integer(full,0x128);
        else if(vt==FW_SW2 || vt==FW_SW4)deck=integer(full,0x98);
        if(deck<0 || deck>3)deck=-1;
        if(word(p,0)==FW_BOX) {
            intptr_t offset=*(const intptr_t *)(FW_BOX-16);
            uintptr_t field=word(p,0x18);
            if(offset==-(intptr_t)0x198c8 && sane(field) &&
                word(field,0)==FW_FIELD && word(field,0x18)==root) {
                uintptr_t owner=p+offset;
                int d=sane(owner)?integer(owner,0x197f0):-1;
                if(sane(owner) && word(owner,0)==FW_OWNER && d>=0 && d<4) {
                    n->box[d]=p;n->owner[d]=owner;
                }
            }
        }
        if(vt==FW_PREVIEW_FAMILY && deck>=0 && q.shown) {
            n->preview[deck]=p;n->families|=1u<<deck;
        } else if(vt==FW_PREVIEW && deck>=0 && q.shown && !(n->families&(1u<<deck)))
            n->preview[deck]=p;
        /* Reserve the container, including its expand/collapse arrow.
         * Reserving only FxBody would paint over the native drawer control. */
        if((vt==FW_FX_CONTAINER || (vt==FW_FX_BODY && !n->fx)) && q.shown) {
            rect r=bounds(p);r.x=q.x;r.y=q.y;
            if(valid_rect(r,1280,800) && r.x>=900 && r.y>=70){n->fx=p;n->fx_rect=r;}
        }
        if(q.shown && (vt==FW_INFO ||
            class_name(p,"N3gui8shortcut12ShortcutViewE") ||
            class_name(p,"N3gui7utility11UtilityViewE")))n->modal=1;
        /* Their descendants are painted natively and need no census. */
        if(word(p,0)==FW_BOX || vt==FW_PREVIEW_FAMILY)continue;
        if(q.depth>=16){n->truncated=1;continue;}
        uintptr_t list=0;int count=children(p,&list);
        for(int i=count-1;i>=0;i--) {
            if(tail>=2048){n->truncated=1;break;}
            uintptr_t child=word(list,8*i);
            if(!sane(child) || word(child,0x18)!=p)continue;
            rect r=bounds(child);
            queue[tail++]=(struct item){child,deck,q.depth+1,q.x+r.x,q.y+r.y,q.shown&&visible(child)};
        }
    }
    if(tail)n->truncated=1;
}
static void native_save(void *g){((void (*)(void *))FW_G_SAVE)(g);}
static void native_restore(void *g){((void (*)(void *))FW_G_RESTORE)(g);}
static void native_fill(void *g,rect r,uint32_t rgb) {
    uint32_t c=rgb|0xff000000u;
    ((void (*)(void *,const uint32_t *))FW_G_COLOUR)(g,&c);
    ((void (*)(void *,float,float,float,float))FW_G_FILL)(g,(float)r.x,(float)r.y,(float)r.w,(float)r.h);
}
static int native_part(void *g,uintptr_t comp,rect src,rect dst) {
    float matrix[6];
    if(!sane(comp) || !valid_rect(dst,1280,800) || !mapping(src,dst,matrix))return 0;
    native_save(g);
    int clip[4]={dst.x,dst.y,dst.w,dst.h};
    int shown=((int (*)(void *,const int *))FW_G_CLIP)(g,clip);
    if(shown) {
        ((void (*)(void *,const float *))FW_G_TRANSFORM)(g,matrix);
        /* The dispatcher paints this exact native object's normal child
         * graph: native colour cache, beatgrid, playhead and cue labels. */
        ((void (*)(void *,void *))FW_PAINT_CHILDREN)((void *)comp,g);
    }
    native_restore(g);return 1;
}
static int native_wave(void *g,uintptr_t box,uintptr_t owner,rect dst,int compare) {
    rect b=bounds(box),src[3],out[3];int grid=integer(owner,0x58);
    if(b.w<200 || b.w>1280 || b.h<48 || b.h>300 || grid<7 || grid>32)return 0;
    if(compare) {
        /* A compact comparison is the native audio interior, without making
         * its tiny beat/cue labels collide with the main ruler. */
        return native_part(g,box,(rect){0,grid,b.w,b.h-2*grid},dst);
    }
    if(!slices(dst,b.w,b.h,grid,src,out))return 0;
    for(int i=0;i<3;i++)if(!native_part(g,box,src[i],out[i]))return 0;
    return 1;
}
static int enabled(void) {
    return LOAD(&ready) && enable_row>=0 && C->row_value(enable_row)>0 && !LOAD(&native_conflicts);
}
/* QEMU's direct X11 mouse does not use the touch-panel filter. Stop native
 * hit testing from descending into moved/hidden audio controls. The native
 * header, right-hand FX container and every other view retain their hit tests. */
static void *component_at(void *self,const int *point) {
    uintptr_t p=(uintptr_t)self;
    if(point && sane(p) && word(p,0)==FW_VIEW && enabled() && C->view()==AZ_VIEW_DECK) {
        struct presented state;
        if(snapshot(&state) && state.valid && now_ns()-state.stamp<250000000u &&
           point[0]>=0 && point[0]<1280 && point[1]>=70 && point[1]<800 &&
           !(point[0]>=state.width && point[1]>=state.fx_y && point[1]<state.fx_y+state.fx_h))return NULL;
    }
    return original_component_at(self,point);
}
static void native_phase(void *g,rect r,int deck,int ref) {
    double a=-1,b=-1,bar=-1;
    if(C->deck_grid(deck,&a,&bar) || C->deck_grid(ref,&b,&bar) ||
        a<0 || a>=1 || b<0 || b>=1)return;
    int x=r.x+r.w/2,y=r.y+r.h/2;
    double diff=a-b;if(diff>.5)diff-=1;if(diff<-.5)diff+=1;
    native_fill(g,(rect){x,r.y+2,2,r.h-4},0xff8200);
    int slave=x+(int)(diff*r.w/2);
    native_fill(g,(rect){slave,y,2,r.y+r.h-2-y},0xf4f4f4);
    for(int k=-4;k<=4;k++)
        native_fill(g,(rect){x+k*r.w/10,r.y+3,1,7},k%4?0xf4f4f4:0xff3038);
}
static void paint(void *self,void *g,int ignore_alpha) {
    uintptr_t view=(uintptr_t)self;
    if(drawing || !sane(view) || word(view,0)!=FW_VIEW) {original(self,g,ignore_alpha);return;}
    drawing=1;
    int on=enabled() && C->view()==AZ_VIEW_DECK;
    struct native_parts parts;
    census(view,&parts);
    int mode=integer(view,0x3a8);
    if(on && !parts.modal && mode>=0 && mode<=2) {
        if(mode_identity!=view){mode_identity=view;saved_mode=mode;}
        if(mode!=2) {
            *(int *)(view+0x3a8)=2;
            ((void (*)(uintptr_t))FW_RELAYOUT)(view);
            census(view,&parts);
        }
    } else if(!on && mode_identity==view) {
        if(mode==2 && saved_mode!=2) {
            *(int *)(view+0x3a8)=saved_mode;
            ((void (*)(uintptr_t))FW_RELAYOUT)(view);
        }
        mode_identity=0;
    }
    /* Component painting can leave a reduced graphics state behind. The
     * composition must use the caller's original clip/transform. */
    native_save(g);
    original(self,g,ignore_alpha);native_restore(g);
    /* The original can run native layout callbacks. Never reuse captures
     * taken before it; obtain the current tree again. */
    census(view,&parts);
    struct presented state={0};
    state.stamp=now_ns();
    if(!on || parts.modal || integer(view,0x3a8)!=2 ||
        integer(view,0x28)!=1280 || integer(view,0x2c)!=800)goto done;
    /* Unknown partial trees remain stock, never a fabricated waveform. */
    for(int d=0;d<4;d++)if(!parts.box[d] || !parts.owner[d])goto done;
    state.width=parts.fx?parts.fx_rect.x:1280;
    if(parts.fx){state.fx_y=parts.fx_rect.y;state.fx_h=parts.fx_rect.h;}
    state.count=count_for(C->row_value(count_row));
    state.focus=LOAD(&focus_deck);state.left=LOAD(&left_deck);state.right=LOAD(&right_deck);
    if(state.focus<0 || state.focus>3 || state.left<0 || state.left>3 || state.right<0 || state.right>3)goto done;
    native_save(g);
    native_fill(g,(rect){0,70,1280,730},0x050505);
    unsigned loaded=0;
    for(int d=0;d<4;d++) {
        az_deck_info info={.size=sizeof info};
        if(!C->deck_info(d,&info) && info.loaded==1)loaded|=1u<<d;
    }
    for(int i=0;i<state.count;i++) {
        int d=player_for(state.count,i,state.focus,state.left,state.right);
        struct layout l=geometry(state.width,state.count,i);
        if(!(loaded&(1u<<d)))continue;
        if(native_wave(g,parts.box[d],parts.owner[d],l.wave,0))state.waves|=1u<<d;
        int ref=d==state.left?state.right:state.left;
        if(ref==d)ref=(d+1)%4;
        unsigned phase=LOAD(&phase_mask);
        if(loaded&(1u<<ref)) {
            if(!(phase&(1u<<d)) && C->row_value(compare_row)==0)
                native_wave(g,parts.box[ref],parts.owner[ref],l.compare,1);
            else native_phase(g,l.compare,d,ref);
        }
        if(parts.preview[d]) {
            rect b=bounds(parts.preview[d]);
            if(b.w>=100 && b.w<=1280 && b.h>=12 && b.h<=256 &&
                native_part(g,parts.preview[d],(rect){0,0,b.w,b.h},l.preview))state.previews|=1u<<d;
        }
    }
    /* Clear ALL stock player pixels, then repaint only the actual shared FX
     * container, preserving its native children, parameters and input paths. */
    if(parts.fx) {
        rect r=parts.fx_rect;
        native_part(g,parts.fx,(rect){0,0,r.w,r.h},r);
    }
    native_restore(g);
    state.valid=1;
    /* Reuse native invalidations and promote the following native frame to
     * the complete composed area. No private 20/30-FPS waveform timer. */
    if(state.stamp-repaint_at>=15000000u) {
        int r[4]={0,70,1280,730};
        ((void (*)(void *,const int *))FW_REPAINT)(self,r);repaint_at=state.stamp;
    }
done:
    publish(&state);drawing=0;
}
/* SDK drawing uses the core's installed AZ typefaces. No bundled font files,
 * FreeType dependency or drawing outside a valid surface/page context. */
static void surface_paint(az_draw *d,az_rect area,void *user) {
    (void)area;(void)user;
    struct presented p;if(!snapshot(&p) || !p.valid || !enabled() || C->view()!=AZ_VIEW_DECK)return;
    az_deck_info all[4];memset(all,0,sizeof all);
    for(int deck=0;deck<4;deck++) {
        az_deck_info *info=&all[deck];info->size=sizeof *info;
        if(C->deck_info(deck,info)) {info->loaded=-1;info->pos_s=info->len_s=-1;info->bpm=0;info->title[0]=0;}
        info->title[sizeof info->title-1]=0;info->artist[sizeof info->artist-1]=0;info->key[sizeof info->key-1]=0;
    }
    community_render(d,p.width,p.count,p.focus,p.left,p.right,LOAD(&elapsed_mask),LOAD(&phase_mask),all);
}
static void status_draw(az_draw *d,az_rect r,void *user) {
    (void)user;
    if(r.w<240 || r.h<120)return;
    struct presented p;snapshot(&p);
    char line[96];
    snprintf(line,sizeof line,"Native view: %s",p.valid?"composed":"stock / not captured");
    C->draw_text(d,r.x+8,r.y+8,line,16,0xffffff);
    snprintf(line,sizeof line,"Detail mask: %X   Preview mask: %X",p.waves,p.previews);
    C->draw_text(d,r.x+8,r.y+38,line,14,0xffffff);
    snprintf(line,sizeof line,"Players: %d   Width: %d",p.count,p.width);
    C->draw_text(d,r.x+8,r.y+66,line,14,0xffffff);
    C->draw_text(d,r.x+8,r.y+94,"Masks show drawn players; F = four",12,0xaaaaaa);
}
static const az_screen_v1 status_screen={.size=sizeof(az_screen_v1),.on_draw=status_draw};
static int surface_input(const az_event *e,void *user) {
    (void)user;
    struct presented p;if(!snapshot(&p) || !p.valid || !enabled())return 0;
    if(e->kind==AZ_EV_BACK)return 0;
    if(e->kind!=AZ_EV_TAP)return 1;
    for(int i=0;i<p.count;i++) {
        int deck=player_for(p.count,i,p.focus,p.left,p.right);
        struct layout l=geometry(p.width,p.count,i);
        if(inside(l.compare,e->x,e->y))__atomic_fetch_xor(&phase_mask,1u<<deck,__ATOMIC_RELEASE);
        else if(inside(l.status,e->x,e->y))__atomic_fetch_xor(&elapsed_mask,1u<<deck,__ATOMIC_RELEASE);
    }
    /* Deliberately consume moved waveform/preview touches: they must never
     * reach an unrelated invisible native seek, hot cue or loop control. */
    return 1;
}
static int touch(const az_touch *t,void *user) {
    (void)user;
    static int was_down;
    int first=t->down && !was_down;
    if(!t->down)was_down=0;
    struct presented p;
    if(t->slot!=0 || !enabled() || C->view()!=AZ_VIEW_DECK ||
       !snapshot(&p) || !p.valid || now_ns()-p.stamp>250000000u)return AZ_TOUCH_PASS;
    if(t->x>=0 && t->x<1280 && t->y>=70 && t->y<96 &&
       !(t->x>=p.width && t->y>=p.fx_y && t->y<p.fx_y+p.fx_h)) {
        was_down=t->down;return AZ_TOUCH_TAKE;
    }
    for(int i=0;i<p.count;i++) {
        struct layout l=geometry(p.width,p.count,i);
        if(inside(l.panel,t->x,t->y)) {
            if(inside(l.title,t->x,t->y) || inside(l.status,t->x,t->y))return AZ_TOUCH_PASS;
            /* Contact ownership is latched by the SDK. Only one action on
             * first down; movement must not repeatedly toggle comparison. */
            if(first && inside(l.compare,t->x,t->y)) {
                int deck=player_for(p.count,i,p.focus,p.left,p.right);
                __atomic_fetch_xor(&phase_mask,1u<<deck,__ATOMIC_RELEASE);
            }
            was_down=t->down;
            return AZ_TOUCH_TAKE;
        }
    }
    if(t->x>=p.width && t->x<1280 && t->y>=96 && t->y<800 &&
       !(t->y>=p.fx_y && t->y<p.fx_y+p.fx_h)) {was_down=t->down;return AZ_TOUCH_TAKE;}
    return AZ_TOUCH_PASS;
}
static int frames(int bus,unsigned char *f,int len,void *user) {
    (void)user;
    if(bus!=AZ_BUS_ERP_IN || len!=32)return AZ_FRAME_OK;
    for(int side=0;side<2;side++) {
        int off=8+side*8;
        unsigned bits=(f[off]&1)|((f[off+1]&1)<<1),rise=bits&~select_last[side];
        select_last[side]=bits;
        int deck=rise&1?side:rise&2?side+2:-1;
        if(deck>=0){STORE(side?&right_deck:&left_deck,deck);STORE(&focus_deck,deck);}
    }
    return AZ_FRAME_OK; /* read-only selection observation, never change input */
}
static void tick(void) {
    int conflicts=0;
    for(int i=0;i<3;i++)if(conflict_rows[i]>=0 && C->row_value(conflict_rows[i])>0)conflicts=1;
    STORE(&native_conflicts,conflicts);
    if(conflicts && C->row_value(enable_row)>0)C->row_set(enable_row,0);
    struct presented p;snapshot(&p);
    static int last_count=-1,last_width=-1,last_valid=-1;
    static unsigned last_waves=~0u,last_previews=~0u;
    if(AZ_HAS(C,log) && (last_count!=p.count || last_width!=p.width || last_valid!=(int)p.valid ||
       last_waves!=p.waves || last_previews!=p.previews)) {
        C->log(MOD_ID,"native layout valid=%u players=%d width=%d drawn-wave=%X drawn-preview=%X fx-y=%d fx-h=%d",
            p.valid,p.count,p.width,p.waves,p.previews,p.fx_y,p.fx_h);
        last_count=p.count;last_width=p.width;last_valid=p.valid;last_waves=p.waves;last_previews=p.previews;
    }
    int show=enabled() && C->view()==AZ_VIEW_DECK && p.valid && now_ns()-p.stamp<500000000u;
    for(int k=0;k<4;k++) {
        int row=k/2,exists=show && (row==0 || p.count==4);
        if(exists) {
            int index=row?2:0;
            struct layout l=geometry(p.width,p.count,index);
            rect r=k%2?l.status:l.title;
            C->surface_place(surfaces[k],(az_rect){0,r.y,p.width,r.h});
        }
        if(exists!=active_flags[k]){C->surface_show(surfaces[k],exists);active_flags[k]=exists;}
        if(exists)C->surface_redraw(surfaces[k]);
    }
}
static int init(const az_core_v1 *core) {
    C=core;
    if(!core || core->abi!=AZ_MOD_ABI || !AZ_HAS(core,claim_hook) ||
       !AZ_HAS(core,fw_tag) || !AZ_HAS(core,register_row) || !AZ_HAS(core,row_value) ||
       !AZ_HAS(core,row_lookup) || !AZ_HAS(core,row_set) || !AZ_HAS(core,surface_create) || !AZ_HAS(core,surface_place) ||
       !AZ_HAS(core,surface_show) || !AZ_HAS(core,surface_redraw) || !AZ_HAS(core,deck_info) ||
       !AZ_HAS(core,deck_grid) || !AZ_HAS(core,view) || !AZ_HAS(core,frame_filter) || !AZ_HAS(core,touch_filter) ||
       !AZ_HAS(core,draw_image) || !AZ_HAS(core,fill_rect) || !AZ_HAS(core,draw_text) || !AZ_HAS(core,text_width) ||
       !AZ_HAS(core,draw_line) || !AZ_HAS(core,register_screen))return 1;
    if(!C->fw_tag() || strcmp(C->fw_tag(),PV_FW))return 2;
    /* Check the exact unpatched entry before registering or claiming anything.
     * Store only its fingerprint in this project, never firmware code bytes. */
    if(!hit_profile_ok())return 14;
    az_row_v1 row={.size=sizeof row,.key="enabled",.label="CDJ Player View",.kind=AZ_ROW_TOGGLE,
        .help="Native AZ waveforms, grid and cue markers. Experimental community port."};
    enable_row=C->register_row(MOD_ID,&row);if(enable_row<0)return 3;
    static const char *const counts[]={"1 Player","2 Players side by side","4 Players"};
    row=(az_row_v1){.size=sizeof row,.key="players",.label="Players",.kind=AZ_ROW_CHOICE,
        .parent="enabled",.choices=counts,.n_choices=3,.help="Independent presentation; no track reload."};
    count_row=C->register_row(MOD_ID,&row);if(count_row<0)return 4;
    static const char *const compare[]={"Stacked waveform","Phase comparison"};
    row=(az_row_v1){.size=sizeof row,.key="comparison",.label="Comparison",.kind=AZ_ROW_CHOICE,
        .parent="enabled",.choices=compare,.n_choices=2,.help="Phase compares the selected opposite player, not an inferred sync master."};
    compare_row=C->register_row(MOD_ID,&row);if(compare_row<0)return 5;
    if(C->register_screen(MOD_ID,&status_screen))return 12;
    row=(az_row_v1){.size=sizeof row,.key="native-status",.label="Native renderer status",
        .kind=AZ_ROW_SCREEN,.screen=&status_screen,.help="Last native component census. F means all four decks."};
    if(C->register_row(MOD_ID,&row)<0)return 13;
    static const char *const names[]={"phase-meter","four-waveforms","hide-track-title"};
    for(int i=0;i<3;i++)conflict_rows[i]=C->row_lookup(names[i]);
    if(community_render_init(C))return 9;
    static const az_surface_v1 def={.size=sizeof def,.layer=AZ_LAYER_OVER_EP147,
        .views=AZ_VIEW_BIT(AZ_VIEW_DECK),.on_paint=surface_paint,.on_input=surface_input};
    for(int k=0;k<4;k++){surfaces[k]=C->surface_create(MOD_ID,&def);if(!surfaces[k])return 6;}
    if(C->frame_filter(MOD_ID,1u<<AZ_BUS_ERP_IN,frames,NULL))return 7;
    if(C->touch_filter(MOD_ID,touch,NULL))return 10;
    /* Claim last. This address is distinct from the height mod's dispatcher
     * and the official core's WaveformView paint/FPS hooks. */
    if(C->claim_hook(MOD_ID,FW_PAINT_ENTIRE,(void *)paint,(void **)&original))return 8;
    if(C->claim_hook(MOD_ID,FW_COMPONENT_AT,(void *)component_at,(void **)&original_component_at))return 11;
    STORE(&ready,1);
    return 0;
}
static const char *const firmware[]={PV_FW,NULL};
static const az_mod_v1 descriptor={.abi=AZ_MOD_ABI,.size=sizeof(az_mod_v1),
    .id=MOD_ID,.fw_tags=firmware,.init=init,.tick=tick};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void){return &descriptor;}
