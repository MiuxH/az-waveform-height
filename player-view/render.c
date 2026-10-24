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
/* Fit the whole number together. Fraction size follows the fitted integer,
 * and space is reserved before any text is queued. No last-digit trimming. */
static void community_pair(const azmod_fb *f,struct pv_rect r,const char *whole,const char *fraction,int px,int ratio,uint32_t color) {
    if(px>64)px=64;
    int fp=8,advance=0,fw=0,gap=fraction[0]?2:0;
    for(;px>=8;px--) {
        fp=px*ratio/100;if(fp<8)fp=8;
        advance=render_core->text_width(whole,px);fw=fraction[0]?render_core->text_width(fraction,fp):0;
        if(px*3/2<=r.h && advance+gap+fw<=r.w)break;
    }
    if(px<8)return;
    community_number(f,r.x,r.y,advance,r.h,whole,px,color);
    if(fraction[0])community_number(f,r.x+advance+gap,r.y,fw,r.h,fraction,fp,color);
}
static void community_compact_status(const azmod_fb *,struct pv_layout,const struct pv_deck *,int,unsigned);
static void draw_text(const azmod_fb *f,int x,int y,const char *s,int scale,uint32_t col) {
    label_add(f,x,y,f->h-y,s,scale*8,col);
}
#include "chrome.c"

/* Narrow panes reflow the instrument columns. The PLAYER label, full clock,
 * tempo and BPM have explicit space instead of sharing shrunken V22 columns. */
static void community_compact_status(const azmod_fb *f,struct pv_layout l,const struct pv_deck *d,int deck,unsigned elapsed_mask) {
    int x=l.panel.x+4,y=l.player.y,h=l.player.h,w=l.panel.w-8,gap=4;
    int badge=48,track=34,rest=w-badge-track-gap*4;
    int tw=rest*50/100,pw=rest*23/100,bw=rest-tw-pw;
    struct pv_rect player={x,y,badge,h},tr={x+badge+gap,y,track,h};
    struct pv_rect time={tr.x+track+gap,y,tw,h},tempo={time.x+tw+gap,y,pw,h},bpm={tempo.x+pw+gap,y,bw,h};
    pv_frame(f,player,d->selected?PV_WHITE:PV_MUTED);pv_frame(f,bpm,PV_WHITE);
    int caption=16,small=9;
    pv_fit(f,(struct pv_rect){player.x+2,y,player.w-4,caption},"PLAYER",small,PV_WHITE,1);
    pv_fit(f,(struct pv_rect){tr.x,y,tr.w,caption},"TRACK",small,PV_MUTED,1);
    int elapsed=(elapsed_mask>>deck)&1u;if(!(d->duration>0))elapsed=1;
    pv_fit(f,(struct pv_rect){time.x,y,time.w,caption},elapsed?"TIME":"REMAIN",small,PV_MUTED,0);
    pv_fit(f,(struct pv_rect){tempo.x,y,tempo.w,caption},"TEMPO",small,PV_MUTED,0);
    pv_fit(f,(struct pv_rect){bpm.x+3,y,bpm.w-6,caption},"BPM",small,PV_WHITE,0);
    struct pv_rect number={player.x+4,y+caption,player.w-8,h-caption-3};
    char a[64],b[16];snprintf(a,sizeof a,"%d",deck+1);
    community_pair(f,number,a,"",h,43,PV_WHITE);
    number=(struct pv_rect){tr.x,y+caption,tr.w,h-caption-3};
    if(d->track_number>=0)snprintf(a,sizeof a,"%d",d->track_number);else snprintf(a,sizeof a,"--");
    community_pair(f,number,a,"",h/2,43,PV_WHITE);
    double seconds=elapsed?d->elapsed:d->duration-d->elapsed;
    if(d->loaded!=1 || !isfinite(seconds) || seconds<0 || seconds>=86400 || d->elapsed<0) {
        snprintf(a,sizeof a,"--:--");snprintf(b,sizeof b,".---");
    } else {
        long ms=(long)(seconds*1000);snprintf(a,sizeof a,"%02ld:%02ld",ms/60000,(ms/1000)%60);
        snprintf(b,sizeof b,".%03ld",ms%1000);
    }
    number=(struct pv_rect){time.x,y+caption,time.w,h-caption-3};community_pair(f,number,a,b,h,43,PV_WHITE);
    if(isfinite(d->tempo))snprintf(a,sizeof a,"%+.2f",d->tempo);else snprintf(a,sizeof a,"--.--");
    number=(struct pv_rect){tempo.x,y+caption,tempo.w,h-caption-3};community_pair(f,number,a,"%",h*2/3,43,PV_WHITE);
    if(d->loaded==1 && d->bpm>=20 && d->bpm<=400)snprintf(a,sizeof a,"%.1f",d->bpm);else snprintf(a,sizeof a,"---.-");
    char *dot=strchr(a,'.');snprintf(b,sizeof b,"%s",dot?dot:".-");if(dot)*dot=0;
    number=(struct pv_rect){bpm.x+3,y+caption,bpm.w-6,h-caption-3};community_pair(f,number,a,b,h,52,PV_WHITE);
}

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
        if(in->loaded!=1){out->elapsed=out->duration=-1;out->bpm=0;out->key_name[0]=0;}
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
