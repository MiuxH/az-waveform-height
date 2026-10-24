/* SPDX-License-Identifier: GPL-3.0-only */
/* V22 chrome, adapted to SDK drawing. Native wave/ruler/cue areas are alpha
 * holes; their renderers run in EP147, not in this framebuffer compositor. */
#include "azmod-mod.h"
#include "layout.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
struct azmod_fb {unsigned char *px;int w,h,stride;};
enum {AZF_PLAYER_VIEW_FX=1,AZF_OVERLAYS};
static const az_core_v1 *render_core;
static az_draw *render_context;
static int render_width;
static int community_native=1;
static unsigned char *render_pixels;
static rect holes[12];static int nholes;
struct label {rect r;char text[192];int px;uint32_t color;};
static struct label labels[768];static int nlabels;
static uint64_t rendered_at;
static int last_width,last_count,last_focus,last_left,last_right;
static unsigned last_elapsed;
static int az_on(int feature){return feature==AZF_OVERLAYS || render_width<1280;}
static int az_value(int feature){return az_on(feature);}
static unsigned azm_theme_key(void){return 0;}
static uint32_t rgba(uint32_t c){return (c&0xff00ff00u)|((c>>16)&255u)|((c&255u)<<16);}
static void fill_rect(const azmod_fb *f,int x,int y,int w,int h,uint32_t color) {
    long long x0=x<0?0:x,y0=y<0?0:y,x1=(long long)x+w,y1=(long long)y+h;
    if(x1>f->w)x1=f->w;
    if(y1>f->h)y1=f->h;
    uint32_t c=rgba(color);
    for(int yy=(int)y0;yy<y1;yy++) {
        uint32_t *row=(uint32_t *)(f->px+(long)yy*f->stride);
        for(int xx=(int)x0;xx<x1;xx++)row[xx]=c;
    }
}
static int intersects(rect a,rect b){return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h;}
static int label_add(const azmod_fb *f,int x,int y,int h,const char *s,int px,uint32_t color) {
    if(!render_context || px<8 || h<1 || nlabels>=768)return 1;
    long offset=f->px-render_pixels;
    rect r={(int)(offset%5120)/4+x,(int)(offset/5120)+y,f->w-x,h};
    if(!valid_rect(r,render_width,800))return 1;
    for(int i=0;i<nholes;i++)if(intersects(r,holes[i]))return 1;
    struct label *q=&labels[nlabels++];q->r=r;q->px=px>64?64:px;q->color=color&0xffffffu;
    snprintf(q->text,sizeof q->text,"%s",s);return 1;
}
static int azmod_ttf_menu(const azmod_fb *f,int x,int y,int unused,int h,const char *s,int px,int bold,int track,uint32_t col) {
    (void)unused;(void)bold;(void)track;
    int line=px*3/2;
    if(line>h)px=h*2/3;
    return label_add(f,x,y+(h-px*3/2)/2,h-(h-px*3/2)/2,s,px,col);
}
static int azmod_ttf_menu_width(const char *s,int n,int px,int track) {
    (void)n;(void)track;return render_core->text_width(s,px<8?8:px>64?64:px);
}
static int azmod_ttf_player_number(const azmod_fb *f,int x,int baseline,const char *s,int px,int cell,uint32_t color) {
    /* The SDK owns font choice; native AZ glyphs replace the old bitmap fallback.
     * Place fractional values on the same baseline with the native line box. */
    (void)cell;
    if(px*3/2>baseline)px=baseline*2/3;
    return label_add(f,x,baseline-px*3/2,px*3/2,s,px,color);
}
/* Return exactly the advance of the font we actually draw. The previous
 * bitmap cell estimate left fractions detached and cropped the last digit. */
static int community_number(const azmod_fb *f,int x,int y,int w,int h,const char *s,int px,uint32_t color) {
    if(w<1 || h<12)return 0;
    if(px>64)px=64;
    while(px>8 && (px*3/2>h || render_core->text_width(s,px)>w))px--;
    int advance=render_core->text_width(s,px);
    if(advance>w || px*3/2>h)return 0;
    /* All fractions use the same bottom baseline as their integer part. */
    int before=nlabels;
    label_add(f,x,y+h-px*3/2,px*3/2,s,px,color);
    if(nlabels>before)labels[nlabels-1].r.w=w;
    return advance;
}
static void draw_text(const azmod_fb *f,int x,int y,const char *s,int scale,uint32_t col) {
    label_add(f,x,y,f->h-y,s,scale*8,col);
}
#include "chrome.c"

int community_render_init(const az_core_v1 *core) {
    render_core=core;render_pixels=calloc(1280*800,4);
    /* The V22 raster caches include text pixels; SDK glyphs are drawn later,
     * so disable those caches rather than silently losing cached text. */
    g_pv_cache_disabled=1;
    return render_pixels?0:-1;
}
void community_render(az_draw *ctx,int width,int count,int focus,int left,int right,
                      unsigned elapsed,unsigned phase,const az_deck_info info[4]) {
    render_context=ctx;render_width=width;
    struct timespec clock;clock_gettime(CLOCK_MONOTONIC,&clock);
    uint64_t stamp=(uint64_t)clock.tv_sec*1000000000u+clock.tv_nsec;
    if(rendered_at && stamp-rendered_at<8000000u && last_width==width && last_count==count &&
       last_focus==focus && last_left==left && last_right==right && last_elapsed==elapsed)goto present;
    rendered_at=stamp;last_width=width;last_count=count;last_focus=focus;
    last_left=left;last_right=right;last_elapsed=elapsed;
    nholes=0;nlabels=0;
    struct pv_deck data[4];memset(data,0,sizeof data);
    for(int d=0;d<4;d++) {
        const az_deck_info *in=&info[d];struct pv_deck *out=&data[d];
        out->loaded=in->loaded;out->elapsed=in->pos_s;out->duration=in->len_s;
        out->track_bpm=NAN;
        out->bpm=in->bpm;out->phase=-1;out->tempo=NAN;
        out->pad_mode=out->tempo_range=out->master_tempo=out->quantize=out->quantize_value=-1;
        out->auto_cue=out->auto_hotcue=out->track_number=out->beat_jump=-1;
        out->selected=d==focus;
        snprintf(out->title,sizeof out->title,"%s",in->title);
        snprintf(out->artist,sizeof out->artist,"%s",in->artist);
        snprintf(out->key_name,sizeof out->key_name,"%s",in->key);
        /* Sentinel tests only presence in chrome. The community native guards
         * above the old raster readers prohibit dereferencing it. */
        out->wave.data=(const unsigned char *)1;
    }
    for(int i=0;i<count;i++) {
        struct layout l=geometry(width,count,i);
        holes[nholes++]=l.wave;holes[nholes++]=l.compare;holes[nholes++]=l.preview;
    }
    azmod_fb fb={render_pixels,width,800,5120};
    azmod_player_draw(&fb,data,count,focus,left,right,0,elapsed);
    for(int i=0;i<nholes;i++) {
        rect r=holes[i];
        if(valid_rect(r,width,800))
            for(int y=r.y;y<r.y+r.h;y++)memset(render_pixels+(long)y*5120+r.x*4,0,(size_t)r.w*4);
    }
    /* draw_image takes tightly packed RGBA. Keep the fixed 1280px stride even
     * when native FX reserves the right edge; the surface clips that edge. */
present:
    render_core->draw_image(ctx,0,0,render_pixels,1280,800);
    for(int i=0;i<nlabels;i++) {
        struct label *q=&labels[i];size_t n=strlen(q->text);
        while(n && render_core->text_width(q->text,q->px)>q->r.w) {
            do {n--;}while(n && ((unsigned char)q->text[n]&0xc0)==0x80);
            q->text[n]=0;
        }
        render_core->draw_text(ctx,q->r.x,q->r.y,q->text,q->px,q->color);
    }
    (void)phase;render_context=NULL;
}
