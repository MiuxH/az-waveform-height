/* SPDX-License-Identifier: GPL-3.0-only */
/* CDJ-3000 screen proportions; included by menu.c after ui_draw.c. */
#include "player_view.h"
#include <math.h>
#include <time.h>
#include "player_render_cache.h"
/* Player canvases are opaque ARGB from the first write. Avoid a second
 * full-frame read/modify/write pass solely to repair alpha. */
static void pv_opaque_rect(const azmod_fb *fb,int x,int y,int w,int h,uint32_t color) {
    fill_rect(fb,x,y,w,h,color|0xff000000u);
}
static _Atomic long g_pv_wave_us_total,g_pv_readout_us_total,g_pv_overview_us_total;
static long pv_raster_us(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000000L+t.tv_nsec/1000;
}
static void pv_fit(const azmod_fb *,struct pv_rect,const char *,int,uint32_t,int);
static void pv_text(const azmod_fb *fb, struct pv_rect r, const char *s, int px, uint32_t col) {
    col|=0xff000000u;
    if (r.w <= 0 || r.h <= 0 || r.x < 0 || r.y < 0 || r.x + r.w > fb->w || r.y + r.h > fb->h) return;
    if (px < 9) px = 9;
    if (px > r.h) px = r.h;
    azmod_fb clip = {.px = fb->px + (long) r.y * fb->stride + 4 * r.x, .w = r.w, .h = r.h, .stride = fb->stride};
    if (azmod_ttf_menu && azmod_ttf_menu(&clip, 0, 0, 0, r.h, s, px, 0, 0, col)) return;
    int sc = px / 8;
    if (sc < 1) sc = 1;
    int len = (int) strlen(s);
    while (sc > 1 && len * 6 * sc > r.w) sc--;
    draw_text(&clip, 0, (r.h - 7 * sc) / 2, s, sc, col);
}
static void pv_fill(const azmod_fb *fb, struct pv_rect r, uint32_t col) {
    pv_opaque_rect(fb, r.x, r.y, r.w, r.h, col);
}
static void pv_center(const azmod_fb *fb, struct pv_rect r, const char *s, int px, uint32_t col) {
    if (px < 9) px=9;
    int width=azmod_ttf_menu_width ? azmod_ttf_menu_width(s,(int)strlen(s),px,0) : 0;
    if (width <= 0) width=(int)strlen(s)*px*3/5;
    if (width < r.w) { int pad=(r.w-width)/2; r.x+=pad; r.w-=pad; }
    pv_text(fb,r,s,px,col);
}
/* A restrained display palette, independent of a photograph's cast/reflection. */
enum { PV_BG=0x030303, PV_SURFACE=0x1b1d20, PV_BUTTON=0x292c31,
       PV_WHITE=0xf2f3f5, PV_MUTED=0xa3a7ad, PV_BLUE=0x258bff,
       PV_ORANGE=0xff981a, PV_RED=0xf2383d, PV_LINE=0x41454a };
static int pv_number_width(const char *s, int cell) {
    int width=0;
    for (const unsigned char *p=(const unsigned char *)s;*p;p++) width+=pv_digit_step(*p,cell);
    return width;
}
static int pv_number(const azmod_fb *fb, struct pv_rect r, const char *s, int px, uint32_t color) {
    color|=0xff000000u;
    if (r.w<1 || r.h<1 || r.x<0 || r.y<0 || r.x+r.w>fb->w || r.y+r.h>fb->h) return 0;
    if(community_native)return community_number(fb,r.x,r.y,r.w,r.h,s,px,color);
    if (px<9) px=9;
    if (px>r.h*4/3) px=r.h*4/3;
    int cell=(px*56+50)/100;
    while (px>9 && pv_number_width(s,cell)>r.w) { px--; cell=(px*56+50)/100; }
    int width=pv_number_width(s,cell), base=r.h-2;
    azmod_fb clip={.px=fb->px+(long)r.y*fb->stride+4*r.x,.w=r.w,.h=r.h,.stride=fb->stride};
    if (azmod_ttf_player_number && azmod_ttf_player_number(&clip,0,base,s,px,cell,color)) return width;
    int x=0;
    for (const unsigned char *p=(const unsigned char *)s;*p;p++) {
        char ch[2]={(char)*p,0}; int step=pv_digit_step(*p,cell), scale=px/8;
        if (scale>base/7) scale=base/7;
        if (scale<1) scale=1;
        draw_text(&clip,x+(step-5*scale)/2,base-7*scale,ch,scale,color);
        x+=step;
    }
    return width;
}
static int pv_text_width(const char *s, int px) {
    int width=azmod_ttf_menu_width?azmod_ttf_menu_width(s,(int)strlen(s),px,0):0;
    return width>0?width:(int)strlen(s)*px*3/5;
}
static void pv_title_text(const azmod_fb *fb, struct pv_rect r, const char *s, int px) {
    char text[192];size_t used=strnlen(s,sizeof text-1);
    memcpy(text,s,used);text[used]=0;
    if (pv_text_width(text,px)>r.w) {
        while (used>0) {
            text[--used]=0;
            if (pv_text_width(text,px)+pv_text_width("...",px)<=r.w) break;
        }
        if (used+3<sizeof text) memcpy(text+used,"...",4);
    }
    pv_text(fb,r,text,px,PV_WHITE);
}
static void pv_frame(const azmod_fb *fb, struct pv_rect r, uint32_t col) {
    pv_opaque_rect(fb, r.x, r.y, r.w, 1, col);
    pv_opaque_rect(fb, r.x, r.y + r.h - 1, r.w, 1, col);
    pv_opaque_rect(fb, r.x, r.y, 1, r.h, col);
    pv_opaque_rect(fb, r.x + r.w - 1, r.y, 1, r.h, col);
}
static uint32_t pv_mix(uint32_t a, uint32_t b, unsigned q) {
    unsigned p = 256 - q;
    return ((((a >> 16)&255)*p + ((b >> 16)&255)*q) >> 8) << 16 |
           ((((a >> 8)&255)*p + ((b >> 8)&255)*q) >> 8) << 8 |
           (((a&255)*p + (b&255)*q) >> 8);
}
/* Same RGB / layered cell selection as EP147's two drawers (2.00
 * 1f6afb8 / 1f6b808), with interpolated envelope edges at output resolution.
 * No cached native image is resized, replaced or written. */
static int pv_cell(const double h[7], double row, int layered) {
    return !layered ? (row < h[0] ? 0 : row < h[1] ? 1 : 2)
        : row < h[3] ? (row < h[1] ? (row < h[0] ? 0 : 1) : row < h[2] ? 2 : 3)
        : row < h[5] ? (row < h[4] ? 4 : 5) : 6;
}
static void pv_columns_draw(const azmod_fb *fb, struct pv_rect r, const struct pv_deck *d, int sx, int sw) {
    if (r.x < 0 || r.y < 0 || r.x+r.w > fb->w || r.y+r.h > fb->h) return;
    for (int x = 0; x < r.w; x++) {
        double source = sx + (double)x*sw/r.w;
        int ix = (int)floor(source);
        if (ix < 0 || ix >= d->wave.w) continue;
        const unsigned char *a = d->columns + ix*28;
        const unsigned char *b = ix+1 < d->wave.w ? a+28 : a;
        double q=source-ix, height[7];
        uint32_t colors[7];
        for (int k=0;k<7;k++) {
            height[k]=(a[k*4+3]+(b[k*4+3]-a[k*4+3])*q)*r.h/d->column_height;
            const unsigned char *ca=a+k*4, *cb=b+k*4;
            colors[k]=pv_mix(ca[0]|(uint32_t)ca[1]<<8|(uint32_t)ca[2]<<16,
                             cb[0]|(uint32_t)cb[1]<<8|(uint32_t)cb[2]<<16,(unsigned)(q*256));
        }
        /* A cell changes only where one of its seven envelope boundaries
         * crosses a sample. Resolve those few events, not two floating-point
         * cell selections and a blend for every output pixel. */
        int half=(r.h+1)/2, cuts[16]={0,half}, n=2;
        for(int k=0;k<7;k++)for(int edge=0;edge<2;edge++) {
            int cut=(int)floor(r.h*.5-(edge?.25:.75)-height[k])+1;
            if(cut<=0 || cut>=half)continue;
            int j=n;
            while(j>0 && cuts[j-1]>cut){cuts[j]=cuts[j-1];j--;}
            cuts[j]=cut;n++;
        }
        for(int k=0;k+1<n;k++) {
            int from=cuts[k],to=cuts[k+1];if(from==to)continue;
            double row=r.h*.5-from-.5;
            uint32_t color=pv_mix(colors[pv_cell(height,row-.25,d->layered)],
                                 colors[pv_cell(height,row+.25,d->layered)],128)|0xff000000u;
            unsigned char *top=fb->px+(long)(r.y+from)*fb->stride+4*(r.x+x);
            unsigned char *bottom=fb->px+(long)(r.y+r.h-1-from)*fb->stride+4*(r.x+x);
            for(int y=from;y<to;y++,top+=fb->stride,bottom-=fb->stride) {
                *(uint32_t *)top=color;*(uint32_t *)bottom=color;
            }
        }
    }
}
static void pv_analysis_sample(const struct pv_deck *d, int index, double height[3], uint32_t *color) {
    const unsigned char *p=d->analysis+(long)index*d->analysis_kind;
    if (d->analysis_kind==3) {
        /* 2EX PWV7 bands: low .4, mid .3, high .06, per beat-link's
         * documented 3-band decoder. Fixed full-scale, no track peak gain. */
        height[0]=p[2]*.4/31; height[1]=p[0]*.3/31; height[2]=p[1]*.06/31;
        *color=0;
    } else if(d->analysis_kind==1) {
        height[0]=(p[0]>>3)/31.0;height[1]=height[2]=0;*color=0x48b7ed;
    } else {
        unsigned bits=(unsigned)p[0]<<8|p[1];
        height[0]=((bits>>2)&31)/31.0; height[1]=height[2]=0;
        *color=pv_rgb_color(bits);
    }
}
static void pv_analysis_draw(const azmod_fb *fb, struct pv_rect r, const struct pv_deck *d, int sx, int sw) {
    if (r.w<1 || r.h<1 || r.x<0 || r.y<0 || r.x+r.w>fb->w || r.y+r.h>fb->h) return;
    static const uint32_t band_color[3]={0x2765ea,0xe7ad62,0xeeeeee};
    for (int x=0;x<r.w;x++) {
        double seconds=d->wave_position+(sx+(double)x*sw/r.w-d->playhead)*d->seconds_per_column;
        double at=seconds*150.0;
        if (at<0 || at>=d->analysis_n) continue;
        int i=(int)at, j=i+1<d->analysis_n?i+1:i;
        double a[3], b[3], h[3]; uint32_t ca,cb;
        pv_analysis_sample(d,i,a,&ca); pv_analysis_sample(d,j,b,&cb);
        double q=at-i;
        for (int k=0;k<3;k++) h[k]=a[k]+(b[k]-a[k])*q;
        uint32_t color=pv_mix(ca,cb,(unsigned)(q*256));
        if (d->wave_style==1) color=0x48b7ed;
        /* Preserve genuine transients when more than one exported sample falls
         * in an output column. Bound work by the supplied analysis length. */
        double samples=d->seconds_per_column*sw/r.w*150;
        int end=(int)ceil(at+samples)-1;
        if (end>=d->analysis_n) end=d->analysis_n-1;
        for (int z=i+1;z<=end && samples>1;z++) {
            double peak[3]; uint32_t cc;
            pv_analysis_sample(d,z,peak,&cc);
            if (peak[0]>h[0] && d->wave_style!=1) color=cc;
            for (int k=0;k<3;k++) if (peak[k]>h[k]) h[k]=peak[k];
        }
        int tinted=0;
        if (d->attenuation && d->attenuation_n>0) {
            int k=(int)((long long)i*d->attenuation_n/d->analysis_n);
            if (k>=d->attenuation_n) k=d->attenuation_n-1;
            for (int bnd=0;bnd<3;bnd++) h[bnd]*=d->attenuation[k]/256.0;
            if (d->tint && d->tint[k]) { color=d->tint[k]; tinted=1; }
        }
        for (int k=0;k<3;k++) {
            if (h[k]>1) h[k]=1;
            h[k]*=r.h*.5;
        }
        if (d->analysis_kind!=3) {
            /* RGB/BLUE has one constant ink per column. Mix only the edge,
             * then write solid mirrored runs; no floating-point colour blend
             * for every interior pixel. Same coverage as the general path. */
            double edge=r.h*.5-h[0];
            int first=(int)floor(edge),half=(r.h+1)/2;
            if(first<0)first=0;
            uint32_t solid=0xff000000u|color;
            if(first<half) {
                double cover=first+1-edge;
                uint32_t ink=cover<1?0xff000000u|pv_mix(0,color,(unsigned)(cover*256)):solid;
                unsigned char *top=fb->px+(long)(r.y+first)*fb->stride+4*(r.x+x);
                unsigned char *bottom=fb->px+(long)(r.y+r.h-1-first)*fb->stride+4*(r.x+x);
                *(uint32_t *)top=ink;*(uint32_t *)bottom=ink;
                for(int y=first+1;y<half;y++) {
                    top+=fb->stride;bottom-=fb->stride;
                    *(uint32_t *)top=solid;*(uint32_t *)bottom=solid;
                }
            }
            continue;
        }
        for (int y=0;y<(r.h+1)/2;y++) {
            double row=r.h*.5-y-.5, coverage=0; uint32_t ink=color;
            if (d->analysis_kind==3) {
                int low=h[0]>row-.5,mid=h[1]>row-.5,high=h[2]>row-.5;
                if(high){ink=band_color[2];coverage=h[2]-row+.5;}
                else if(low && mid){ink=0x7c5635;coverage=fmin(h[0],h[1])-row+.5;}
                else if(mid){ink=band_color[1];coverage=h[1]-row+.5;}
                else if(low){ink=band_color[0];coverage=h[0]-row+.5;}
                if(tinted)ink=pv_mix(color,0xffffff,high?90:mid?45:0);
            }
            if (coverage<=0) continue;
            if (coverage>1) coverage=1;
            uint32_t rgb=0xff000000u|pv_mix(0,ink,(unsigned)(coverage*256));
            *(uint32_t *)(fb->px+(long)(r.y+y)*fb->stride+4*(r.x+x))=rgb;
            *(uint32_t *)(fb->px+(long)(r.y+r.h-1-y)*fb->stride+4*(r.x+x))=rgb;
        }
    }
}
/* Preserve native columns, independently scale rows. A span may cross the image
 * boundary: those columns stay black; never duplicate edge samples as audio. */
static void pv_image_draw(const azmod_fb *fb, struct pv_rect r, const struct pv_image *im, int sx, int sw, int reverse, uint32_t tint) {
    if (!im->data || im->w < 1 || im->h < 1 || im->pixel < 3 || im->pixel > 4 || im->stride < im->w * im->pixel ||
        sw < 1 || r.w < 1 || r.h < 1) return;
    for (int y = 0; y < r.h; y++) {
        int iy = y * im->h / r.h;
        if (reverse) iy = im->h - 1 - iy;
        for (int x = 0; x < r.w; x++) {
            int ix = sx + x * sw / r.w;
            if (ix < 0 || ix >= im->w) continue;
            const unsigned char *p = im->data + (long) iy * im->stride + ix * im->pixel;
            uint32_t rgb = p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16;
            if (tint) {
                unsigned v = p[0] > p[1] ? p[0] : p[1];
                if (p[2] > v) v = p[2];
                rgb = (((tint >> 16) & 255) * v / 255) << 16 | (((tint >> 8) & 255) * v / 255) << 8 | (tint & 255) * v / 255;
            }
            if (r.x + x >= 0 && r.x + x < fb->w && r.y + y >= 0 && r.y + y < fb->h)
                *(uint32_t *) (fb->px + (long) (r.y + y) * fb->stride + 4 * (r.x + x)) = 0xff000000u | rgb;
        }
    }
}
/* One time-to-pixel transform for the audio, ruler and both cue families. */
static double pv_wave_time_x(const struct pv_deck *d, struct pv_rect r, int sx, int span, double seconds) {
    if (!isfinite(seconds) || !isfinite(d->wave_position) ||
        !isfinite(d->seconds_per_column) || d->seconds_per_column<=0 || span<=0) return NAN;
    return r.x+(d->playhead+(seconds-d->wave_position)/d->seconds_per_column-sx)*r.w/span;
}
static int pv_wave_marker_x(const struct pv_deck *d, struct pv_rect r, int sx, int span, double seconds, int *out) {
    double x=pv_wave_time_x(d,r,sx,span,seconds);
    if (!isfinite(x) || x<r.x || x>=r.x+r.w) return 0;
    *out=(int)lround(x);
    if (*out>=r.x+r.w) *out=r.x+r.w-1;
    return 1;
}
static void pv_wave_loop(const azmod_fb *fb, struct pv_rect r, const struct pv_deck *d,
                         int sx, int span, const struct pv_cue *cue, int y) {
    if (y<r.y || y+4>r.y+r.h) return;
    if (!cue->present || !isfinite(cue->seconds) || !isfinite(cue->loop_end) || cue->loop_end<=cue->seconds) return;
    double a=pv_wave_time_x(d,r,sx,span,cue->seconds), b=pv_wave_time_x(d,r,sx,span,cue->loop_end);
    if (!isfinite(a) || !isfinite(b) || b<r.x || a>=r.x+r.w) return;
    int left=a<r.x?r.x:(int)lround(a), right=b>=r.x+r.w?r.x+r.w-1:(int)lround(b);
    if (right>=r.x+r.w) right=r.x+r.w-1;
    if (left>=r.x+r.w) left=r.x+r.w-1;
    pv_opaque_rect(fb,left,y,right-left+1,1,cue->color);
    if (a>=r.x) pv_opaque_rect(fb,left,y,1,4,cue->color);
    if (b<r.x+r.w) pv_opaque_rect(fb,right,y,1,4,cue->color);
}
static void pv_wave_cues(const azmod_fb *fb, struct pv_rect r,
                         const struct pv_deck *d, int sx, int span, int tag, int memory_y) {
    struct { int id,x,label; } hot[8];
    int count=0;
    /* Memory cues have their own lane; they cannot cover the A-H flags. */
    for (int i=0;i<d->memory_n && i<PV_MEMORY_N;i++) {
        const struct pv_cue *c=&d->memory[i]; int x;
        if (!c->present || !pv_wave_marker_x(d,r,sx,span,c->seconds,&x)) continue;
        /* Only the triangle is drawn; no cue-coloured stem or connector. */
        for (int k=0;k<4 && memory_y+k<r.y+r.h;k++) {
            int a=x-k,b=x+k+1;
            if (a<r.x) a=r.x;
            if (b>r.x+r.w) b=r.x+r.w;
            pv_opaque_rect(fb,a,memory_y+k,b-a,1,c->color);
        }
    }
    for (int i=0;i<8;i++) {
        const struct pv_cue *c=&d->hotcues[i]; int x,at=count;
        if (!c->present || !pv_wave_marker_x(d,r,sx,span,c->seconds,&x)) continue;
        while (at>0 && hot[at-1].x>x) {hot[at]=hot[at-1];at--;}
        hot[at].id=i;hot[at].x=x;count++;
    }
    if (tag*8+7>r.w) tag=(r.w-7)/8;
    if (tag<3) return;
    for (int i=0;i<count;i++) {
        hot[i].label=hot[i].x;
        if (i && hot[i].label<hot[i-1].label+tag+1) hot[i].label=hot[i-1].label+tag+1;
    }
    for (int i=count-1;i>=0;i--) {
        int limit=i+1<count?hot[i+1].label-tag-1:r.x+r.w-tag;
        if (hot[i].label>limit) hot[i].label=limit;
    }
    for (int i=0;i<count;i++) {
        const struct pv_cue *c=&d->hotcues[hot[i].id];
        int x=hot[i].x,tx=hot[i].label;
        pv_fill(fb,(struct pv_rect){tx,r.y,tag,tag},c->color);
        char label[2]={(char)('A'+hot[i].id),0};
        pv_text(fb,(struct pv_rect){tx+2,r.y,tag-2,tag},label,tag*3/4,0x101010);
        if (tx!=x) for (int k=0;k<3;k++) {
            int left=x-k,right=x+k+1;
            if (left<r.x) left=r.x;
            if (right>r.x+r.w) right=r.x+r.w;
            pv_opaque_rect(fb,left,r.y+tag+k,right-left,1,c->color);
        }
    }
    for (int i=0;i<8;i++) pv_wave_loop(fb,r,d,sx,span,&d->hotcues[i],r.y+tag);
    for (int i=0;i<d->memory_n && i<PV_MEMORY_N;i++) pv_wave_loop(fb,r,d,sx,span,&d->memory[i],r.y+tag+4);
}
struct pv_wave_layout {
    struct pv_rect top,bottom,body;
    int tag,number_h;
};
static struct pv_wave_layout pv_wave_geometry(struct pv_rect r,int comparison) {
    struct pv_wave_layout l={0};
    int rh=r.h/20,tag=r.h*6/100;
    if (rh<6) rh=6;
    if (rh>16) rh=16;
    if (tag<12) tag=12;
    if (tag>16) tag=16;
    int marker_h=comparison?0:tag+7;
    if (marker_h>r.h/3) marker_h=r.h/3;
    if (tag>marker_h-5 && !comparison) tag=marker_h-5;
    l.tag=tag;
    /* Lower grid labels retain a reserved footer,
     * below the audio envelope. The larger touch target is retained. */
    l.number_h=!comparison && r.w>=400 && r.h>=70?20:0;
    l.top=(struct pv_rect){r.x,r.y+marker_h,r.w,rh};
    l.bottom=(struct pv_rect){r.x,r.y+r.h-rh-l.number_h,r.w,rh};
    l.body=(struct pv_rect){r.x,l.top.y+rh,r.w,l.bottom.y-l.top.y-rh};
    if (comparison) {l.body.y=l.top.y+l.top.h+1;l.body.h=l.bottom.y-l.body.y-1;}
    else {
        int pad=l.body.h/48;
        if (pad<1) pad=1;
        l.body.y+=pad;l.body.h-=2*pad;
    }
    return l;
}
static int pv_native_grid_ok(const struct pv_image *im) {
    return im->data && im->w>=1 && im->w<=2048 && im->h>=1 && im->h<=64 &&
        (im->pixel==3 || im->pixel==4) && im->stride>=im->w*im->pixel && im->stride<=im->w*im->pixel+64;
}
static uint32_t pv_grid_rgb(const struct pv_image *im,int x,int y) {
    const unsigned char *p=im->data+(long)y*im->stride+x*im->pixel;
    if (im->pixel==4 && !p[3]) return 0;
    return (uint32_t)p[2]<<16|(uint32_t)p[1]<<8|p[0];
}
/* Reproject native tick columns, instead of resizing a bitmap and losing thin
 * lines at narrow widths. Metadata arrival never selects a different ruler. */
static void pv_native_grid_draw(const azmod_fb *fb,struct pv_rect top,struct pv_rect bottom,
                                const struct pv_image *im,int sx,int span) {
    if (!pv_native_grid_ok(im) || span<1 || top.h<1 || bottom.h<1) return;
    for (int col=0;col<im->w;col++) {
        double at=top.x+(double)(col-sx)*top.w/span;
        if (at<top.x-.5 || at>=top.x+top.w-.5) continue;
        int x=(int)lround(at);
        for (int y=0;y<top.h;y++) {
            uint32_t rgb=pv_grid_rgb(im,col,y*im->h/top.h);
            if (rgb) pv_opaque_rect(fb,x,top.y+y,1,1,rgb);
        }
        for (int y=0;y<bottom.h;y++) {
            uint32_t rgb=pv_grid_rgb(im,col,im->h-1-y*im->h/bottom.h);
            if (rgb) pv_opaque_rect(fb,x,bottom.y+y,1,1,rgb);
        }
    }
}
/* Optional numeric labels attach to a real native red tick, never to the
 * differently anchored exported timeline. Missing anchors mean no label. */
static int pv_native_bar_x(const struct pv_deck *d,struct pv_rect r,int sx,int span,
                           double seconds,double tolerance,int *out) {
    if (!pv_native_grid_ok(&d->grid) || !(tolerance>0)) return 0;
    double expected=d->playhead+(seconds-d->wave_position)/d->seconds_per_column;
    double distance=tolerance;int best=-1;
    for (int col=0;col<d->grid.w;col++) {
        if (fabs(col-expected)>distance) continue;
        int red=0;
        for (int y=0;y<d->grid.h && !red;y++) {
            uint32_t c=pv_grid_rgb(&d->grid,col,y);
            red=(c>>16&255)>128 && (c>>8&255)<80 && (c&255)<80;
        }
        if (red) {best=col;distance=fabs(col-expected);}
    }
    if (best<0) return 0;
    double x=r.x+(double)(best-sx)*r.w/span;
    if (x<r.x || x>=r.x+r.w) return 0;
    *out=(int)lround(x);
    if (*out>=r.x+r.w) *out=r.x+r.w-1;
    return 1;
}
static void pv_inspect_grid_tick(const azmod_fb *fb,struct pv_rect top,struct pv_rect bottom,int x,int bar) {
    /* Off-screen inspection has no corresponding native raster. Use only
     * complete exported beat times with the captured AZ red/grey tick style. */
    uint32_t color=bar?0xff0000:0xa6a6a6;
    pv_opaque_rect(fb,x,top.y,1,top.h,color);
    pv_opaque_rect(fb,x,bottom.y,1,bottom.h,color);
}
static void pv_wave_draw_impl(const azmod_fb *fb, struct pv_rect r, const struct pv_deck *d, int comparison) {
    if (community_native) return;
    pv_fill(fb,r,0x000000);
    if (d->loaded==0 || r.w<1 || r.h<8) return;
    struct pv_deck view;
    if (!comparison && d->preview_active) {
        if (!d->analysis || d->analysis_n<1 || !isfinite(d->duration) || d->duration<=0 || d->preview_q<0 || d->preview_q>65535) {
            pv_fit(fb,(struct pv_rect){r.x+8,r.y+r.h/3,r.w-16,36},"DETAIL UNAVAILABLE AT VIEW POINT",r.w/40,PV_MUTED,1);
            return;
        }
        view=*d;view.wave_position=d->duration*d->preview_q/65535.0;d=&view;
    }
    if (!d->wave.data) {
        if (!comparison) pv_text(fb,(struct pv_rect){r.x+8,r.y+r.h/3,r.w-16,r.h/3},"NO WAVEFORM DATA",r.w/38,0x888888);
        return;
    }
    struct pv_wave_layout geometry=pv_wave_geometry(r,comparison);
    int tag=geometry.tag;
    struct pv_rect top=geometry.top,bottom=geometry.bottom,body=geometry.body;
    if (body.h<1 || tag<1) return;
    int known=d->playhead>=0 && d->playhead<d->wave.w,span=d->crop_w;
    if (span<1) return;
    int sx=known?d->playhead-span*(d->cursor_center?50:27)/100:d->crop_x;
    const double *times=d->beat_times;int nt=d->beat_n,first=d->beat_first;
    int exact_grid=known && times && nt>=2 && nt<=524288 && isfinite(d->seconds_per_column) && d->seconds_per_column>0;
    int inspect_grid=d->preview_active && !comparison;
    if (!inspect_grid) pv_native_grid_draw(fb,top,bottom,&d->grid,sx,span);
    if (d->analysis && d->analysis_n>0 &&
        ((d->wave_style!=1 && d->analysis_kind==2) || (d->preview_active && d->analysis_kind==1) ||
         (d->analysis_kind==3 && (d->preview_active || !d->columns))) &&
        isfinite(d->seconds_per_column) && d->seconds_per_column>0 && isfinite(d->wave_position) && known)
        pv_analysis_draw(fb,body,d,sx,span);
    else if (d->columns && d->column_height>=2 && d->column_height<=256)
        pv_columns_draw(fb,body,d,sx,span);
    else pv_image_draw(fb,body,&d->wave,sx,span,0,0);
    if (exact_grid) {
        double start=d->wave_position+(sx-d->playhead)*d->seconds_per_column;
        int lo=0,hi=nt;
        while (lo<hi) {int mid=lo+(hi-lo)/2;if(times[mid]<start)lo=mid+1;else hi=mid;}
        int label_end=r.x-1;
        for (int i=lo;i<nt;i++) {
            double at=pv_wave_time_x(d,r,sx,span,times[i]);int x;
            if (at>=r.x+r.w) break;
            if (!pv_wave_marker_x(d,r,sx,span,times[i],&x)) continue;
            int bar=(first+i)%4==0;
            if (inspect_grid) pv_inspect_grid_tick(fb,top,bottom,x,bar);
            if (bar && geometry.number_h && first+i>0 && (first+i)%16==0) {
                if (!inspect_grid) {
                    double tolerance=20;
                    double duration=i>=4?times[i]-times[i-4]:i+4<nt?times[i+4]-times[i]:0;
                    if (duration<=0) continue;
                    double half=duration/d->seconds_per_column*.45;
                    if (half<tolerance) tolerance=half;
                    if (!pv_native_bar_x(d,r,sx,span,times[i],tolerance,&x)) continue;
                }
                if (x<=label_end || x>=r.x+r.w-110) continue;
                char num[16];snprintf(num,sizeof num,"%d",first+i);
                int tx=x-10;
                if (tx<r.x) tx=r.x;
                if (tx>r.x+r.w-30) tx=r.x+r.w-30;
                pv_text(fb,(struct pv_rect){tx,bottom.y+bottom.h,30,geometry.number_h},num,11,PV_WHITE);
                label_end=tx+34;
            }
        }
    }
    if (known && !comparison) {
        {
            struct pv_rect markers=r;markers.h=top.y-r.y;
            pv_wave_cues(fb,markers,d,sx,span,tag,r.y+tag+3);
        }
        int px=pv_cursor_for(r,d);
        pv_opaque_rect(fb,px,top.y,2,r.y+r.h-top.y,d->preview_active?PV_BLUE:0xffffff);
        for (int k=0;k<7 && k<r.h;k++)
            pv_opaque_rect(fb,px-k,r.y+r.h-7+k,2*k+1,1,PV_ORANGE);
    }
}
static void pv_wave_draw(const azmod_fb *fb,struct pv_rect r,const struct pv_deck *d,int comparison) {
    long started=pv_raster_us();pv_wave_draw_impl(fb,r,d,comparison);
    __atomic_fetch_add(&g_pv_wave_us_total,pv_raster_us()-started,__ATOMIC_RELAXED);
}
static void pv_phase_lane(const azmod_fb *fb, struct pv_rect r, const struct pv_deck *d, int y, int toward, uint32_t color) {
    int left=r.x+r.w*21/100,right=r.x+r.w*4/5;
    pv_opaque_rect(fb, left, y, right-left, 2, color);
    if (!d || d->loaded == 0 || !isfinite(d->phase) || d->phase < 0) return;
    /* Only native position drives these marks. No wall-clock animation: a
     * paused player's marks retain their position while other decks can move. */
    int center = pv_cursor_for(r,d), beat_h = r.h / 5, bar_h = r.h * 2 / 5;
    if (beat_h < 3) beat_h = 3;
    if (bar_h < 5) bar_h = 5;
    for (int k = -12; k <= 16; k++) {
        int x = center + (int) lround((k - d->phase) * r.w / 16.0);
        if (x < left+2 || x >= right-3) continue;
        if (k % 4 == 0) pv_opaque_rect(fb, x - 1, y + 1 - bar_h / 2, 3, bar_h, 0xe62828);
        else pv_opaque_rect(fb, x, toward > 0 ? y : y + 2 - beat_h, 2, beat_h, 0xffffff);
    }
}
static void pv_phase_draw(const azmod_fb *fb, struct pv_rect r, const struct pv_deck *a, const struct pv_deck *b) {
    pv_fill(fb, r, 0x000000);
    if (a->master) {
        /* Latest reference: fixed orange master line, live own blue beat lane.
         * There is no fabricated second player's phase on the master. */
        int left=r.x+r.w*21/100,right=r.x+r.w*4/5;
        pv_opaque_rect(fb,left,r.y+r.h/3,right-left,2,0xff8c00);
        pv_phase_lane(fb,r,a,r.y+r.h*2/3,-1,0x239fff);
    } else {
        struct pv_deck reference=*b;reference.cursor_center=a->cursor_center;
        pv_phase_lane(fb, r, &reference, r.y + r.h / 3, 1, 0xff8c00);
        pv_phase_lane(fb, r, a, r.y + r.h * 2 / 3, -1, 0x239fff);
    }
    pv_opaque_rect(fb, pv_cursor_for(r,a), r.y + r.h / 8, 1, r.h * 3 / 4, 0xffffff);
}
static void pv_master_badge(const azmod_fb *fb, struct pv_rect r) {
    int h = r.h * 2 / 5, w = r.w * 2 / 5;
    if (h > 25) h = 25;
    if (w > 200) w = 200;
    struct pv_rect box = {r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, w, h};
    pv_fill(fb, box, 0xffb34b);
    pv_text(fb, (struct pv_rect) {box.x + 4, box.y, box.w - 8, box.h}, "MASTER PLAYER", box.h * 3 / 4, 0x171717);
}
static void pv_cues_draw(const azmod_fb *fb, struct pv_rect r, const struct pv_deck *d) {
    if (community_native) return;
    if (d->loaded == 0 || !isfinite(d->duration) || d->duration <= 0 || r.h < 3) return;
    int height = r.h > 22 ? 22 : r.h, width = height;
    struct { int id, x, label; } tags[8];
    int count=0;
    if (width*8+7 > r.w) width=(r.w-7)/8;
    if (width<3) return;
    for (int i=0;i<8;i++) {
        const struct pv_cue *c=&d->hotcues[i];
        if (!c->present || !isfinite(c->seconds) || c->seconds<0 || c->seconds>d->duration) continue;
        int x=r.x+(int)lround(c->seconds*(r.w-1)/d->duration), at=count;
        while (at>0 && tags[at-1].x>x) { tags[at]=tags[at-1]; at--; }
        tags[at].id=i; tags[at].x=x; count++;
    }
    for (int i=0;i<count;i++) {
        tags[i].label=tags[i].x;
        if (i && tags[i].label<tags[i-1].label+width+1) tags[i].label=tags[i-1].label+width+1;
    }
    for (int i=count-1;i>=0;i--) {
        int limit=i+1<count ? tags[i+1].label-width-1 : r.x+r.w-width;
        if (tags[i].label>limit) tags[i].label=limit;
    }
    for (int i=0;i<count;i++) {
        uint32_t color=d->hotcues[tags[i].id].color;
        struct pv_rect box={tags[i].label,r.y,width,height};
        pv_fill(fb,box,color);
        char label[2]={(char)('A'+tags[i].id),0};
        pv_text(fb,(struct pv_rect){box.x+2,box.y,box.w-2,box.h},label,height*3/4,0x101010);
        /* Overview labels also have no coloured leader entering audio. */
    }
}
static void pv_status_badge(const azmod_fb *fb, struct pv_rect r, const char *label, const char *value, int active) {
    uint32_t col = active > 0 ? 0xff8c00 : active == 0 ? 0x888888
                                                       : 0x555555;
    pv_frame(fb, r, col);
    if (r.w < 70 && !strcmp(label, "QUANTIZE")) label = "QNT";
    int label_px = (r.w - 4) * 8 / ((int)strlen(label) * 5);
    if (label_px > r.h / 3) label_px = r.h / 3;
    pv_text(fb, (struct pv_rect) {r.x + 2, r.y, r.w - 4, r.h / 2}, label, label_px, col);
    pv_text(fb, (struct pv_rect) {r.x + 2, r.y + r.h / 2, r.w - 4, r.h / 2}, value, r.h / 2, col);
}
static void pv_bars_label(char text[40],double distance) {
    if(!isfinite(distance) || distance<=.001 || distance>=1000000)return;
    int beats=(int)ceil(distance-.001);
    if(beats>0)snprintf(text,40,"%02d.%d Bars",(beats-1)/4,(beats-1)%4+1);
}
static void pv_counter_text(const azmod_fb *fb,struct pv_rect r,const char *label,uint32_t color) {
    char digits[28];size_t n=strcspn(label," ");if(n>=sizeof digits)n=sizeof digits-1;
    memcpy(digits,label,n);digits[n]=0;
    int advance=pv_number(fb,(struct pv_rect){r.x,r.y,r.w*72/100,r.h},digits,r.h,color);
    struct pv_rect unit={r.x+advance+2,r.y+r.h/3,r.w-advance-2,r.h*2/3};
    pv_text(fb,unit,"Bars",r.h*55/100,color);
}
static void pv_countdown_label(char text[40], const struct pv_deck *d) {
    snprintf(text,40,"--.- Bars");
    if(d->mark_set) {
        double a,b;
        if(d->loaded==1 && d->mark_q>=0 && d->mark_q<=65535 && isfinite(d->duration) &&
           d->duration>0 && pv_beat_at(d,d->elapsed,&a) && pv_beat_at(d,d->duration*d->mark_q/65535.0,&b))
            pv_bars_label(text,b-a);
        return;
    }
    if(d->beat_times && d->beat_n>=2) {
        double point=d->preview_active?d->duration*d->preview_q/65535.0:-1,a,b;
        if(!d->preview_active)for(int i=0;i<d->memory_n && i<PV_MEMORY_N;i++)
            if(d->memory[i].present && d->memory[i].seconds>d->elapsed && (point<0 || d->memory[i].seconds<point))point=d->memory[i].seconds;
        if(point>=0 && pv_beat_at(d,d->elapsed,&a) && pv_beat_at(d,point,&b)) {pv_bars_label(text,b-a);return;}
        if(d->preview_active)return;
    }
    const struct pv_countdown *c=&d->countdown;
    if(d->loaded==0 || !isfinite(d->elapsed) || c->n<2 || c->n>64 || !isfinite(c->target_beat) ||
       d->elapsed<c->times[0] || d->elapsed>=c->next_cue)return;
    for(int i=0;i+1<c->n;i++) {
        if(d->elapsed<c->times[i] || d->elapsed>=c->times[i+1] || c->times[i+1]<=c->times[i])continue;
        double beat=c->first+i+(d->elapsed-c->times[i])/(c->times[i+1]-c->times[i]);
        pv_bars_label(text,c->target_beat-beat);return;
    }
}
static unsigned pv_ov_height(const unsigned char *p) { return p[4] | (unsigned) p[5] << 8; }
static uint32_t pv_sample_rgb(const struct pv_image *im, int x, int y) {
    const unsigned char *p=im->data+(long)y*im->stride+x*im->pixel;
    return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16;
}
static void pv_overview_image(const azmod_fb *fb, struct pv_rect r, const struct pv_image *im) {
    if (!im->data || im->w<1 || im->h<1 || im->pixel<3 || im->pixel>4 || im->stride<im->w*im->pixel ||
        r.w<1 || r.h<1 || r.x<0 || r.y<0 || r.x+r.w>fb->w || r.y+r.h>fb->h) return;
    for (int y=0;y<r.h;y++) {
        double fy=(double)y*(im->h-1)/(r.h>1?r.h-1:1);
        int y0=(int)fy, y1=y0+1<im->h?y0+1:y0;
        for (int x=0;x<r.w;x++) {
            double fx=(double)x*(im->w-1)/(r.w>1?r.w-1:1);
            int x0=(int)fx, x1=x0+1<im->w?x0+1:x0;
            unsigned qx=(unsigned)((fx-x0)*256), qy=(unsigned)((fy-y0)*256);
            uint32_t top=pv_mix(pv_sample_rgb(im,x0,y0),pv_sample_rgb(im,x1,y0),qx);
            uint32_t bot=pv_mix(pv_sample_rgb(im,x0,y1),pv_sample_rgb(im,x1,y1),qx);
            *(uint32_t *)(fb->px+(long)(r.y+y)*fb->stride+4*(r.x+x))=0xff000000u|pv_mix(top,bot,qy);
        }
    }
}
static void pv_summary_draw(const azmod_fb *fb, struct pv_rect r, const struct pv_deck *d) {
    static const uint32_t colors[]={0x2765ea,0xe7ad62,0xf1f0e7};
    for (int x=0;x<r.w;x++) {
        int first=x*d->summary_n/r.w, last=(x+1)*d->summary_n/r.w;
        if (last<=first) last=first+1;
        if (last>d->summary_n) last=d->summary_n;
        double height[3]={0,0,0}; uint32_t rgb=d->summary[first].color;
        for (int i=first;i<last;i++) {
            const struct pv_peak *p=&d->summary[i];
            if (p->level[0]>=height[0]) rgb=p->color;
            for (int k=0;k<3;k++) if (p->level[k]>height[k]) height[k]=p->level[k];
        }
        if(r.w>d->summary_n) {
            double at=(double)x*(d->summary_n-1)/(r.w>1?r.w-1:1);
            int a=(int)at,b=a+1<d->summary_n?a+1:a;double q=at-a;
            for(int k=0;k<3;k++)height[k]=d->summary[a].level[k]+(d->summary[b].level[k]-d->summary[a].level[k])*q;
            rgb=pv_mix(d->summary[a].color,d->summary[b].color,(unsigned)(q*256));
        }
        unsigned gain=256; int tinted=0;
        if (d->attenuation && d->attenuation_n>0) {
            int i=(int)((long)x*d->attenuation_n/r.w);
            gain=d->attenuation[i];
            if (d->tint && d->tint[i]) { rgb=d->tint[i]; tinted=1; }
        }
        if (d->wave_style==1 && !tinted) rgb=0x48b7ed;
        int bands=d->analysis_kind==3?3:1;
        for (int k=0;k<bands;k++) {
            double h=(double)height[k]*gain*r.h/(4096.0*256);
            if (h>r.h) h=r.h;
            int whole=(int)h;
            uint32_t color=bands==3 && !tinted?colors[k]:rgb;
            if (whole) pv_opaque_rect(fb,r.x+x,r.y+r.h-whole,1,whole,color);
            if (whole<r.h && h>whole) pv_opaque_rect(fb,r.x+x,r.y+r.h-whole-1,1,1,
                pv_mix(PV_BG,color,(unsigned)((h-whole)*256)));
        }
    }
}
static void pv_overview_audio(const azmod_fb *fb, struct pv_rect r, const struct pv_deck *d) {
    pv_fill(fb, r, 0x080808);
    int summary=(d->wave_style!=1 || d->analysis_kind==1) && d->loaded!=0 && d->summary && d->summary_n>0 && d->summary_n<=PV_SUMMARY_N;
    int cached = d->loaded != 0 && d->overview_image.data && !summary;
    if (summary) pv_summary_draw(fb,r,d);
    if (cached) pv_overview_image(fb, r, &d->overview_image);
    if (!summary && !cached && (d->loaded == 0 || !d->overview || d->overview_n < 1)) {
        pv_text(fb, r, d->loaded == 0 ? "" : "NO OVERVIEW DATA", r.h / 3, 0x555555);
        return;
    }
    if (!summary && !cached) {
        unsigned peak = 255;
        for (int i = 0; i < d->overview_n; i++)
            for (int band = 0; band < 2; band++) {
                unsigned v = pv_ov_height(d->overview + 12 * i + 6 * band);
                if (v > peak) peak = v;
            }
        for (int x = 0; x < r.w; x++) {
            int i0 = x * d->overview_n / r.w, i1 = (x + 1) * d->overview_n / r.w;
            if (i1 <= i0) i1 = i0 + 1;
            for (int band = 0; band < 2; band++) {
                const unsigned char *v = d->overview + 12 * i0 + 6 * band;
                for (int i = i0 + 1; i < i1; i++) {
                    const unsigned char *p = d->overview + 12 * i + 6 * band;
                    if (pv_ov_height(p) > pv_ov_height(v)) v = p;
                }
                double at = (double)x*(d->overview_n-1)/(r.w > 1 ? r.w-1 : 1);
                int left=(int)at, right=left+1 < d->overview_n ? left+1 : left;
                double q=at-left;
                const unsigned char *av=d->overview+12*left+6*band, *bv=d->overview+12*right+6*band;
                double amplitude=r.w > d->overview_n ? pv_ov_height(av)+(pv_ov_height(bv)-pv_ov_height(av))*q : pv_ov_height(v);
                double height=amplitude*r.h/peak;
                int h=(int)height;
                if (h > r.h) h=r.h;
                uint32_t color=(uint32_t)v[0]<<16|(uint32_t)v[1]<<8|v[2];
                if (r.w > d->overview_n) color=pv_mix((uint32_t)av[0]<<16|(uint32_t)av[1]<<8|av[2],
                    (uint32_t)bv[0]<<16|(uint32_t)bv[1]<<8|bv[2],(unsigned)(q*256));
                if (h) pv_opaque_rect(fb,r.x+x,r.y+r.h-h,1,h,color);
                if (h < r.h && height > h) pv_opaque_rect(fb,r.x+x,r.y+r.h-h-1,1,1,pv_mix(0x080808,color,(unsigned)((height-h)*256)));
            }
        }
    }
}
static void pv_overview_marks(const azmod_fb *fb,struct pv_rect r,const struct pv_deck *d) {
    pv_opaque_rect(fb, r.x, r.y + r.h - 1, r.w, 1, 0xcccccc);
    if (d->elapsed >= 0 && d->duration > 0) {
        double q = d->elapsed / d->duration;
        if (q < 0) q = 0;
        if (q > 1) q = 1;
        int px = r.x + (int) (q * (r.w - 1));
        pv_opaque_rect(fb, px, r.y - 2, 2, r.h + 2, 0xffffff);
        pv_opaque_rect(fb, r.x, r.y + r.h - 2, px - r.x, 2, 0xff8c00);
        for (int k=0;k<5;k++) pv_opaque_rect(fb,px-k,r.y+r.h-5+k,2*k+1,1,PV_ORANGE);
    }
    if (d->duration>0 && isfinite(d->duration)) {
        for(int i=0;i<d->memory_n && i<PV_MEMORY_N;i++) {
            const struct pv_cue *c=&d->memory[i];
            if(!c->present || !(c->seconds>=0 && c->seconds<=d->duration))continue;
            int x=r.x+(int)(c->seconds*(r.w-1)/d->duration);
            for(int k=0;k<4;k++) {
                int left=x-k,right=x+k+1;if(left<r.x)left=r.x;if(right>r.x+r.w)right=r.x+r.w;
                pv_opaque_rect(fb,left,r.y+k,right-left,1,c->color);
            }
            if(c->loop_end>c->seconds && c->loop_end<=d->duration) {
                int end=r.x+(int)(c->loop_end*(r.w-1)/d->duration);
                pv_opaque_rect(fb,x,r.y+r.h-5,end-x+1,1,c->color);
            }
        }
        for (int i=0;i<8;i++) {
            const struct pv_cue *c=&d->hotcues[i];
            if (!c->present || !isfinite(c->seconds) || !isfinite(c->loop_end) || c->seconds<0 ||
                c->loop_end<=c->seconds || c->loop_end>d->duration) continue;
            int x0=r.x+(int)(c->seconds*(r.w-1)/d->duration), x1=r.x+(int)(c->loop_end*(r.w-1)/d->duration);
            pv_opaque_rect(fb,x0,r.y+r.h-4,x1-x0+1,1,c->color);
            pv_opaque_rect(fb,x0,r.y+r.h-7,1,4,c->color); pv_opaque_rect(fb,x1,r.y+r.h-7,1,4,c->color);
        }
        double cue=d->countdown.next_cue;
        if (cue>0 && cue<d->duration) {
            int x=r.x+(int)(cue*(r.w-1)/d->duration);
            for (int k=0;k<4;k++) pv_opaque_rect(fb,x-k,r.y+k,2*k+1,1,PV_WHITE);
        }
    }
}
static void pv_overview_draw(const azmod_fb *fb,struct pv_rect r,const struct pv_deck *d) {
    pv_overview_audio(fb,r,d);
    if(d->loaded==0 || (!d->summary_n && !d->overview_n && !d->overview_image.data))return;
    pv_overview_marks(fb,r,d);
}
static void pv_overview_cached_impl(const azmod_fb *fb,struct pv_rect r,const struct pv_deck *d,int deck) {
    /* Live stem envelopes/tints are mutable engine-owned curves, not immutable
     * analysis. Keep their direct draw path rather than freezing a cached view. */
    if(d->attenuation || d->tint){pv_overview_draw(fb,r,d);return;}
    struct { struct pv_rect r; unsigned theme; int loaded,style,analysis_kind,kind,n,stride,pixel; } key;
    memset(&key,0,sizeof key);key.r=r;key.theme=azm_theme_key();key.loaded=d->loaded;key.style=d->wave_style;key.analysis_kind=d->analysis_kind;
    const void *source=NULL;size_t bytes=0;
    if((d->wave_style!=1 || d->analysis_kind==1) && d->loaded!=0 && d->summary && d->summary_n>0 && d->summary_n<=PV_SUMMARY_N) {
        key.kind=1;key.n=d->summary_n;source=d->summary;bytes=(size_t)d->summary_n*sizeof *d->summary;
    } else if(d->loaded!=0 && d->overview_image.data) {
        const struct pv_image *im=&d->overview_image;
        if(im->w<1 || im->w>2048 || im->h<1 || im->h>256 || im->pixel<3 || im->pixel>4 || im->stride<im->w*im->pixel || im->stride>im->w*im->pixel+64) {
            pv_overview_draw(fb,r,d);return;
        }
        key.kind=2;key.n=im->w;key.stride=im->stride;key.pixel=im->pixel;
        source=im->data;bytes=(size_t)im->stride*im->h;
    } else if(d->loaded!=0 && d->overview && d->overview_n>0 && d->overview_n<=2048) {
        key.kind=3;key.n=d->overview_n;source=d->overview;bytes=(size_t)d->overview_n*12;
    }
    if(!pv_layer_get(fb,r,&g_pv_overview_layers[deck],&key,sizeof key,source,bytes)) {
        pv_overview_audio(fb,r,d);
        pv_layer_put(fb,r,&g_pv_overview_layers[deck],&key,sizeof key,source,bytes);
    }
    if(d->loaded!=0 && key.kind)pv_overview_marks(fb,r,d);
}
static void pv_overview_cached(const azmod_fb *fb,struct pv_rect r,const struct pv_deck *d,int deck) {
    long started=pv_raster_us();pv_overview_cached_impl(fb,r,d,deck);
    __atomic_fetch_add(&g_pv_overview_us_total,pv_raster_us()-started,__ATOMIC_RELAXED);
}
static void pv_fit(const azmod_fb *fb, struct pv_rect r, const char *s, int px, uint32_t color, int centered) {
    while (px>9 && pv_text_width(s,px)>r.w-4) px--;
    if (centered) pv_center(fb,r,s,px,color);
    else pv_text(fb,r,s,px,color);
}
static void pv_title(const azmod_fb *fb, struct pv_rect r, const struct pv_deck *d, int deck) {
    pv_fill(fb,r,PV_SURFACE);
    int pad=r.h/10, source=r.h*4/5, art=r.h*7/10;
    if (source>r.w/10) source=r.w/10;
    if (art>r.w*9/100) art=r.w*9/100;
    struct pv_rect src={r.x,r.y,source,r.h};
    pv_fill(fb,src,d->source_usb?0x36536b:PV_BUTTON);
    char value[80]; snprintf(value,sizeof value,"%d",deck+1);
    pv_fit(fb,(struct pv_rect){src.x+3,src.y+1,source/3,r.h/5},value,r.h/6,PV_WHITE,0);
    if (d->source_usb) {
        int ux=src.x+source*2/5, uy=r.y+r.h/3, uw=source/5, uh=r.h/3;
        pv_frame(fb,(struct pv_rect){ux,uy,uw,uh},PV_WHITE);
        pv_frame(fb,(struct pv_rect){ux+uw/4,uy-uh/4,uw/2,uh/4+1},PV_WHITE);
        pv_opaque_rect(fb,ux+uw/3,uy+uh/3,uw/3,uh/3,PV_WHITE);
    }
    pv_fit(fb,(struct pv_rect){src.x+2,src.y+r.h*3/4,source-4,r.h/4},d->source_usb?"USB":"SRC",r.h/5,PV_WHITE,1);
    struct pv_rect cover={src.x+source+pad,r.y+(r.h-art)/2,art,art};
    pv_fill(fb,cover,0x101113); pv_frame(fb,cover,PV_LINE);
    /* Existing neutral artwork fallback; never pretend to have a track cover. */
    int nx=cover.x+art/3, ny=cover.y+art/4;
    pv_opaque_rect(fb,nx,ny,2,art/2,PV_MUTED); pv_opaque_rect(fb,nx,ny,art/3,2,PV_MUTED);
    pv_opaque_rect(fb,nx-art/7,ny+art/2,art/7+2,art/10+1,PV_MUTED);
    int title_x=cover.x+cover.w+pad, text_end=r.x+r.w*690/1000;
    int note=r.h/6;
    if (note<6) note=6;
    nx=title_x+2; ny=r.y+r.h/5;
    int stroke=note/8; if (stroke<1) stroke=1;
    pv_opaque_rect(fb,nx+note/2,ny,stroke,note,PV_WHITE);
    pv_opaque_rect(fb,nx+note/2,ny,note/2,stroke,PV_WHITE);
    pv_opaque_rect(fb,nx+note-stroke,ny,stroke,note/3,PV_WHITE);
    pv_opaque_rect(fb,nx+1,ny+note-stroke*2,note/2,stroke*3,PV_WHITE);
    struct pv_rect title={title_x+note+5,r.y+r.h/12,text_end-title_x-note-8,r.h/2};
    int title_px=r.h*37/100; if (title_px<12) title_px=12;
    if (title_px>r.w/28) title_px=r.w/28;
    pv_title_text(fb,title,d->loaded==0?"NOT LOADED":d->title[0]?d->title:"USB PLAYER",title_px);
    struct pv_rect meta={title_x+note+5,r.y+r.h*57/100,text_end-title_x-note-8,r.h/3};
    char length[16]="--:--", bpm[16]="---.-";
    if (d->loaded!=0 && isfinite(d->duration) && d->duration>0 && d->duration<86400)
        snprintf(length,sizeof length,"%02d:%02d",(int)d->duration/60,(int)d->duration%60);
    if (d->loaded!=0 && d->track_bpm>=20 && d->track_bpm<=400)
        snprintf(bpm,sizeof bpm,"%.1f",d->track_bpm);
    snprintf(value,sizeof value,"%s   %s   %s",length,bpm,d->key_name);
    pv_fit(fb,meta,value,r.h*27/100,PV_WHITE,0);
}
static void pv_title_cached(const azmod_fb *fb,struct pv_rect r,const struct pv_deck *d,int deck) {
    struct {struct pv_rect r;unsigned theme;int loaded,usb,deck;double duration,bpm;char title[160],key[32];} key;
    memset(&key,0,sizeof key);key.r=r;key.theme=azm_theme_key();key.loaded=d->loaded;key.usb=d->source_usb;key.deck=deck;
    key.duration=d->duration;key.bpm=d->track_bpm;memcpy(key.title,d->title,sizeof key.title);memcpy(key.key,d->key_name,sizeof key.key);
    if(pv_layer_get(fb,r,&g_pv_title_layers[deck],&key,sizeof key,NULL,0))return;
    pv_title(fb,r,d,deck);pv_layer_put(fb,r,&g_pv_title_layers[deck],&key,sizeof key,NULL,0);
}
static void pv_readout_time(const azmod_fb *fb,struct pv_layout l,const struct pv_deck *d,int deck,unsigned elapsed_mask) {
    int small=l.player.h*15/100;if(small<9)small=9;
    int caption=l.player.h*22/100;char value[64];
    int elapsed=(elapsed_mask>>deck)&1u; if (d->duration<=0) elapsed=1;
    pv_fit(fb,(struct pv_rect){l.time.x,l.time.y,l.time.w,caption},elapsed?"TIME":"REMAIN",small,PV_MUTED,0);
    double seconds=elapsed?d->elapsed:d->duration-d->elapsed;
    long ms=-1;
    if (!isfinite(seconds) || seconds<0 || d->elapsed<0 || seconds>=86400) snprintf(value,sizeof value,"--:--");
    else { ms=(long)(seconds*1000); snprintf(value,sizeof value,"%02ld:%02ld",ms/60000,(ms/1000)%60); }
    struct pv_rect time={l.time.x,l.time.y+caption,l.time.w*78/100,l.time.h-caption-2};
    int px=l.time.h*104/100;
    int advance=pv_number(fb,time,value,px,PV_WHITE);
    char fraction[12]; if (ms<0) snprintf(fraction,sizeof fraction,".---"); else snprintf(fraction,sizeof fraction,".%03ld",ms%1000);
    pv_number(fb,(struct pv_rect){time.x+advance+2,time.y,l.time.w-advance-2,time.h},fraction,px*43/100,PV_WHITE);
}
static void pv_readouts(const azmod_fb *fb, struct pv_layout l, const struct pv_deck *d, int deck, unsigned elapsed_mask) {
    int small=l.player.h*15/100; if (small<9) small=9;
    int caption=l.player.h*22/100;
    char value[64];
    pv_frame(fb,l.player,d->selected?PV_WHITE:PV_MUTED);
    pv_fit(fb,(struct pv_rect){l.player.x+2,l.player.y,l.player.w-4,caption},"PLAYER",small,PV_WHITE,1);
    snprintf(value,sizeof value,"%d",deck+1);
    struct pv_rect number={l.player.x+l.player.w/4,l.player.y+caption,l.player.w/2,l.player.h-caption-2};
    pv_number(fb,number,value,l.player.h*9/10,PV_WHITE);
    struct pv_rect track={l.lcd.x+l.lcd.w*115/1000,l.player.y,l.lcd.w*80/1000,l.player.h};
    pv_fit(fb,(struct pv_rect){track.x,track.y,track.w,caption},"TRACK",small,PV_WHITE,1);
    if (d->track_number>=0) snprintf(value,sizeof value,"%d",d->track_number);
    else snprintf(value,sizeof value,"--");
    int cell=l.player.h*37/100, track_width=pv_number_width(value,cell);
    if (track_width>track.w) track_width=track.w;
    pv_number(fb,(struct pv_rect){track.x+(track.w-track_width)/2,track.y+caption,track_width,track.h-caption-2},
        value,l.player.h*62/100,PV_WHITE);
    for (int hot=0;hot<2;hot++) {
        struct pv_rect badge=pv_auto_button(l,hot);
        int active=hot?d->auto_hotcue:d->auto_cue;
        uint32_t color=active>0?(hot?PV_RED:PV_ORANGE):PV_MUTED;
        pv_frame(fb,badge,active>=0?color:PV_LINE);
        pv_fit(fb,(struct pv_rect){badge.x+2,badge.y,badge.w-4,badge.h},hot?"A.HOT CUE":"AUTO CUE",small,color,1);
    }
    pv_readout_time(fb,l,d,deck,elapsed_mask);
    int advance;
    struct pv_rect tempo_label={l.tempo.x,l.tempo.y,l.tempo.w/2,caption};
    pv_fit(fb,tempo_label,"TEMPO",small,PV_MUTED,0);
    struct pv_rect range={l.tempo.x+l.tempo.w/2,l.tempo.y+1,l.tempo.w/2,caption-2};
    pv_fill(fb,range,d->tempo_range>=0?PV_ORANGE:PV_SURFACE);
    static const char *ranges[]={"6","10","16","WIDE"};
    if (d->tempo_range>=0 && d->tempo_range<3) {
        int size=range.h/3, gx=range.x+3, gy=range.y+range.h/3;
        pv_opaque_rect(fb,gx,gy,size,1,PV_BG); pv_opaque_rect(fb,gx+size/2,gy-size/2,1,size,PV_BG);
        pv_opaque_rect(fb,gx,gy+size,size,1,PV_BG); range.x+=size+5; range.w-=size+5;
    }
    pv_fit(fb,range,d->tempo_range>=0 && d->tempo_range<4?ranges[d->tempo_range]:"--",small,PV_BG,1);
    if (isfinite(d->tempo)) snprintf(value,sizeof value,"%+.2f",d->tempo); else snprintf(value,sizeof value,"--.--");
    struct pv_rect tempo={l.tempo.x,l.tempo.y+caption,l.tempo.w*87/100,l.tempo.h-caption-2};
    advance=pv_number(fb,tempo,value,l.tempo.h*65/100,PV_WHITE);
    pv_number(fb,(struct pv_rect){tempo.x+advance,tempo.y,l.tempo.w-advance,tempo.h},"%",small*3/2,PV_WHITE);
    uint32_t bpm_color=d->master?PV_ORANGE:PV_WHITE;
    pv_frame(fb,l.bpm,bpm_color);
    pv_fit(fb,(struct pv_rect){l.bpm.x+4,l.bpm.y,l.bpm.w/3,caption},"BPM",small,PV_WHITE,0);
    if (d->master) {
        struct pv_rect badge={l.bpm.x+l.bpm.w/2,l.bpm.y+1,l.bpm.w/2-1,caption*3/4};
        pv_fill(fb,badge,PV_ORANGE); pv_fit(fb,badge,"MASTER",small*3/4,PV_BG,1);
    }
    if (d->bpm>=20 && d->bpm<=400) snprintf(value,sizeof value,"%.1f",d->bpm); else snprintf(value,sizeof value,"---.-");
    char *dot=strchr(value,'.'); char decimal[4]=".-";
    if (dot) { snprintf(decimal,sizeof decimal,"%s",dot); *dot=0; }
    struct pv_rect bpm={l.bpm.x+4,l.bpm.y+caption,l.bpm.w*73/100,l.bpm.h-caption-3};
    int bpm_px=l.bpm.h*95/100;
    advance=pv_number(fb,bpm,value,bpm_px,bpm_color);
    pv_number(fb,(struct pv_rect){bpm.x+advance,bpm.y,l.bpm.w-advance-8,bpm.h},decimal,bpm_px*52/100,bpm_color);
    struct pv_rect mt={l.key.x,l.cues.y,l.key.w,l.cues.h};
    pv_frame(fb,mt,d->master_tempo>0?PV_ORANGE:PV_LINE);
    pv_fit(fb,mt,"MT",small,d->master_tempo>0?PV_ORANGE:PV_MUTED,1);
    pv_fit(fb,(struct pv_rect){l.key.x,l.key.y,l.key.w,l.key.h/3},"ORIG KEY",small,PV_MUTED,1);
    pv_fit(fb,(struct pv_rect){l.key.x,l.key.y+l.key.h/3,l.key.w,l.key.h*2/3},d->key_name[0]?d->key_name:"--",small*2,PV_WHITE,1);
    struct pv_rect quant=pv_quant_button(l), jump=pv_jump_button(l);
    pv_status_badge(fb,quant,"QUANTIZE",pv_quant_label(d->quantize_value),d->quantize);
    uint32_t jc=d->beat_jump>=6?PV_WHITE:PV_MUTED;
    pv_frame(fb,jump,jc);
    pv_fit(fb,(struct pv_rect){jump.x+2,jump.y,jump.w-4,jump.h/3},jump.w<60?"JUMP":"BEAT JUMP",small*3/4,jc,1);
    pv_fit(fb,(struct pv_rect){jump.x+8,jump.y+jump.h/3,jump.w-16,jump.h*2/3},pv_jump_label(d->beat_jump),jump.h/2,jc,1);
    pv_text(fb,(struct pv_rect){jump.x+2,jump.y+jump.h/2,8,jump.h/2},"-",small,jc);
    pv_text(fb,(struct pv_rect){jump.x+jump.w-10,jump.y+jump.h/2,8,jump.h/2},"+",small,jc);
}
static void pv_readouts_cached_impl(const azmod_fb *fb,struct pv_layout l,const struct pv_deck *d,int deck,unsigned elapsed_mask) {
    struct {struct pv_layout l;unsigned theme;int deck,selected,master,range,mt,quant,qvalue,auto_cue,auto_hot,track,jump;double bpm,tempo;char key[32];} key;
    memset(&key,0,sizeof key);key.l=l;key.theme=azm_theme_key();key.deck=deck;key.selected=d->selected;key.master=d->master;
    key.range=d->tempo_range;key.mt=d->master_tempo;key.quant=d->quantize;key.qvalue=d->quantize_value;
    key.auto_cue=d->auto_cue;key.auto_hot=d->auto_hotcue;key.track=d->track_number;key.jump=d->beat_jump;
    key.bpm=d->bpm;key.tempo=d->tempo;memcpy(key.key,d->key_name,sizeof key.key);
    struct pv_rect r={l.panel.x,l.player.y,l.panel.w,l.panel.y+l.panel.h-l.player.y};
    if(pv_layer_get(fb,r,&g_pv_status_layers[deck],&key,sizeof key,NULL,0)) {
        pv_fill(fb,l.time,PV_BG);pv_readout_time(fb,l,d,deck,elapsed_mask);return;
    }
    pv_readouts(fb,l,d,deck,elapsed_mask);
    pv_layer_put(fb,r,&g_pv_status_layers[deck],&key,sizeof key,NULL,0);
}
static void pv_readouts_cached(const azmod_fb *fb,struct pv_layout l,const struct pv_deck *d,int deck,unsigned elapsed_mask) {
    long started=pv_raster_us();pv_readouts_cached_impl(fb,l,d,deck,elapsed_mask);
    __atomic_fetch_add(&g_pv_readout_us_total,pv_raster_us()-started,__ATOMIC_RELAXED);
}
#include "player_view_sheet.c"
void azmod_player_draw(const azmod_fb *fb, const struct pv_deck decks[4], int count,
                       int focus, int left, int right, unsigned phase_mask, unsigned elapsed_mask) {
    if (!fb || !fb->px || fb->w < 640 || fb->h < 400 || (count != 1 && count != 2 && count != 4) || focus < 0 || focus > 3 || left < 0 || left > 3 || right < 0 || right > 3) return;
    pv_opaque_rect(fb, 0, 0, fb->w, fb->h, 0x000000);
    pv_text(fb, (struct pv_rect) {20, 5, 180, 38}, "WAVEFORM", 22, 0xffffff);
    pv_text(fb, (struct pv_rect) {22, 44, 174, 16}, "COMMUNITY TEST 0.1.0", 10, 0x999999);
    for (int i = 0; i < 3; i++) {
        struct pv_rect r = {210 + i * 56, 18, 52, 32};
        int n = pv_count(i);
        pv_fill(fb, r, n == count ? 0x087cbb : 0x181818);
        pv_frame(fb, r, n == count ? 0x27b7ff : 0x555555);
        char t[8];
        snprintf(t, sizeof t, "%d", n);
        pv_text(fb, (struct pv_rect) {r.x + 16, r.y, r.w - 16, r.h}, t, 20, 0xffffff);
    }
    struct pv_rect fx_button = pv_fx_button();
    int fx = az_on(AZF_PLAYER_VIEW_FX);
    pv_fill(fb, fx_button, fx ? 0x087cbb : 0x181818);
    pv_frame(fb, fx_button, fx ? 0x27b7ff : 0x555555);
    pv_text(fb, (struct pv_rect) {fx_button.x + 6, fx_button.y, fx_button.w - 12, fx_button.h}, "BEAT FX", 13, 0xffffff);
    for (int i = 0; i < count; i++) {
        int deck = pv_deck_at(count, i, focus, left, right);
        if (deck < 0 || deck > 3) continue;
        const struct pv_deck *d = &decks[deck];
        int stacked_cursor=0;
        int ref = pv_reference(deck, -1, left, right);
        for (int k = 0; k < 4; k++)
            if (decks[k].master) ref = pv_reference(deck, k, left, right);
        struct pv_layout l = pv_geometry_at(fb->w, fb->h, count, i, az_value(AZF_OVERLAYS) ? 96 : 70);
        l = pv_layout_state(l,d->pad_mode,d->pad_stems,(d->stems&0x80u)!=0,d->panel_hidden);
        pv_fill(fb,l.panel,PV_BG);
        pv_frame(fb,l.lcd,PV_LINE);
        pv_title_cached(fb, l.title, d, deck);
        static const char *tab_top[] = {"BEAT", "KEY", "BEAT"};
        static const char *tab_bottom[] = {"LOOP", "SHIFT", "JUMP"};
        for (int tab = 0; tab < 3; tab++) {
            struct pv_rect button = pv_pad_tab(l, tab);
            int enabled = 0; /* Community port: audio-control sheets await SDK bindings. */
            int selected = d->sheet==tab+PV_SHEET_LOOP ||
                (l.pads.h>0 && pv_pad_choice(d->pad_mode)==tab && !d->panel_hidden);
            pv_fill(fb, button, selected ? PV_BLUE : PV_BUTTON);
            pv_frame(fb, button, selected ? PV_BLUE : PV_LINE);
            struct pv_rect text = {button.x + 3, button.y, button.w - 6, button.h / 2};
            int px=l.title.h*22/100;
            pv_center(fb, text, tab_top[tab], px, enabled ? 0xffffff : 0x777777);
            text.y += text.h;
            pv_center(fb, text, tab_bottom[tab], px, enabled ? 0xffffff : 0x777777);
        }
        if (d->loaded != 0) {
            if (phase_mask & (1u << deck)) pv_phase_draw(fb, l.compare, d, &decks[ref]);
            else if (d->master) pv_master_badge(fb, l.compare);
            else if (decks[ref].loaded==1 && decks[ref].wave.data) {
                struct pv_deck compare=decks[ref];
                compare.cursor_center=d->cursor_center;compare.preview_active=0;
                if (d->seconds_per_column>0 && compare.seconds_per_column>0)
                    compare.crop_w=(int)lround(d->crop_w*d->seconds_per_column/compare.seconds_per_column);
                pv_wave_draw(fb,l.compare,&compare,1);
                /* Leave a native-style counter gutter without changing the
                 * waveform's time scale or the shared playhead x coordinate. */
                pv_fill(fb,(struct pv_rect){l.compare.x,l.compare.y,l.compare.w/5,l.compare.h},0);
                stacked_cursor=d->wave.data && d->playhead>=0 && d->playhead<d->wave.w;
            } else pv_fit(fb,l.compare,"NO REFERENCE",l.compare.h/3,PV_MUTED,1);
        }
        char b[40];
        snprintf(b, sizeof b, "%d", ref + 1);
        int chip = l.compare.h / 3;
        if (chip < 12) chip = 12;
        struct pv_rect cr = {l.lcd.x + l.lcd.w * 80 / 1000, l.compare.y, chip, chip};
        if (d->loaded != 0 && !d->master && decks[ref].loaded != 0) pv_fill(fb, cr, 0xff8c00);
        cr.x += 2;
        cr.w -= 2;
        if (d->loaded != 0 && !d->master && decks[ref].loaded != 0) pv_text(fb, cr, b, chip * 3 / 4, 0x000000);
        for (int pad = 0; pad < 8 && l.pads.h > 0; pad++) {
            struct pv_rect button = pv_pad_button(l, pad);
            int enabled = (deck == left || deck == right) && d->pad_mode >= 1 && d->pad_mode <= 8 && d->loaded != 0 && !d->pad_drums;
            uint32_t accent = d->pad_stems ? 0x25c9f0 : d->pad_mode == 8 ? 0xcc7799 : 0xffa537;
            if ((d->pad_mode == 1 || d->pad_mode == 2) && d->hotcues[pad].present)
                accent = d->hotcues[pad].color;
            int held = (d->pads_down >> pad) & 1;
            pv_fill(fb, button, enabled && held ? accent : PV_SURFACE);
            if (!held) for (int yy=1;yy<button.h-1;yy++)
                for (int xx=1;xx<button.w-1;xx++)
                    if (((xx/2)+(yy/2))%2) pv_opaque_rect(fb,button.x+xx,button.y+yy,1,1,0x23262a);
            pv_frame(fb, button, enabled ? 0xdddddd : 0x454545);
            int px = button.w / 5;
            if (px > button.h / 2) px = button.h / 2;
            pv_center(fb, (struct pv_rect){button.x + 3, button.y, button.w - 6, button.h},
                pv_pad_label(d->pad_mode, d->pad_stems, pad), px,
                enabled && held ? 0x111111 : enabled ? 0xffffff : 0x666666);
        }
        if (l.pads.h>0) {
            struct pv_rect close=pv_panel_close(l);
            pv_frame(fb,close,PV_LINE); pv_fit(fb,close,"x",close.h*3/4,PV_MUTED,1);
            if ((d->pad_mode==3 || d->pad_mode==4 || d->pad_mode==5 || d->pad_mode==6) && !d->pad_stems) {
                int page=d->pad_mode==4 || d->pad_mode==6;
                for (int k=0;k<2;k++) {
                    struct pv_rect pr=pv_page_button(l,k);
                    pv_fill(fb,pr,page==k?PV_MUTED:PV_SURFACE); pv_frame(fb,pr,PV_MUTED);
                    pv_fit(fb,pr,k?"2":"1",pr.h*3/4,page==k?PV_BG:PV_WHITE,1);
                }
            }
        }
        pv_wave_draw(fb, l.wave, d, 0);
        if (stacked_cursor && !d->preview_active) {
            int px=pv_cursor_for(l.wave,d);
            pv_opaque_rect(fb,px,l.compare.y,2,l.wave.y+l.wave.h-7-l.compare.y,0xffffff);
        }
        if (d->preview_active) {
            pv_sheet_button(fb,pv_return_button(l),"BACK TO PLAY",1,1);
            int sec=isfinite(d->duration) && d->duration>0 && d->duration<86400?(int)(d->duration*d->preview_q/65535.0):-1;
            if (sec>=0) snprintf(b,sizeof b,"VIEW %02d:%02d",sec/60,sec%60);
            else snprintf(b,sizeof b,"VIEW --:--");
            pv_fit(fb,(struct pv_rect){l.wave.x+8,l.wave.y+20,l.wave.w-16,30},b,18,PV_BLUE,0);
        }
        if (d->enlarged) pv_sheet_button(fb,pv_back_button(l),"BACK TO MULTI",1,0);
        pv_opaque_rect(fb, l.panel.x + l.panel.w / 50, l.time.y - 6, l.panel.w * 24 / 25, 1, 0x333333);
        pv_readouts_cached(fb, l, d, deck, elapsed_mask);
        struct pv_rect bars = {l.lcd.x + l.lcd.w / 100, l.compare.y + l.compare.h / 3, l.lcd.w * 14 / 100, l.compare.h / 3};
        pv_countdown_label(b, d);
        pv_counter_text(fb,bars,b,0xffb34b);
        bars.y += bars.h;
        if (!d->master) pv_countdown_label(b, &decks[ref]);
        else snprintf(b, sizeof b, "--.- Bars");
        pv_counter_text(fb,bars,b,0x53c5ff);
        pv_cues_draw(fb, l.cues, d);
        if (d->stems & 0x80u) {
            static const char *names[] = {"VO", "IN", "BA", "DR"};
            static const uint32_t colors[] = {0x20d940, 0xe84945, 0xcc34b6, 0x3388ee};
            for (int k = 0; k < 4; k++) {
                struct pv_rect stem = {l.stems.x + k * l.stems.w / 4, l.stems.y, l.stems.w / 4 - 3, l.stems.h};
                int present = d->stems & (1u << (8 + k)), lit = present && (d->stems & (1u << k));
                pv_fill(fb, stem, lit ? colors[k] : 0x222222);
                pv_frame(fb, stem, present ? colors[k] : 0x444444);
                pv_text(fb, (struct pv_rect) {stem.x + 4, stem.y, stem.w - 8, stem.h}, names[k], stem.h * 2 / 3, lit ? 0x000000 : 0xaaaaaa);
            }
        }
        pv_overview_cached(fb, l.overview, d,deck);
        if(d->mark_set && d->mark_q>=0 && d->mark_q<=65535) {
            int mx=l.overview.x+(int)((long)d->mark_q*(l.overview.w-1)/65535);
            int tip=l.axis.y-2,base=l.axis.y+4;
            for(int yy=tip;yy<=base;yy++) {
                int half=yy-tip,x0=mx-half,x1=mx+half;
                if(x0<l.overview.x)x0=l.overview.x;
                if(x1>=l.overview.x+l.overview.w)x1=l.overview.x+l.overview.w-1;
                pv_opaque_rect(fb,x0,yy,x1-x0+1,1,PV_WHITE);
            }
        }
        if (d->preview_active && d->preview_q>=0 && d->preview_q<=65535)
            pv_opaque_rect(fb,l.overview.x+(int)((long)d->preview_q*(l.overview.w-1)/65535),l.cues.y,2,l.overview.y+l.overview.h-l.cues.y,PV_BLUE);
        if (d->duration > 0 && d->duration < 24 * 3600 && isfinite(d->duration)) {
            int interval = d->duration > 12 * 60 ? 120 : 60;
            for (int seconds = interval; seconds < d->duration; seconds += interval) {
                double q = seconds / d->duration;
                int px = l.axis.x + (int) (l.axis.w * q);
                pv_opaque_rect(fb, px, l.axis.y, 1, l.axis.h / 4, 0x999999);
                snprintf(b, sizeof b, "%d:%02d", seconds / 60, seconds % 60);
                int tw = l.lcd.w * 90 / 1000, font = l.axis.h * 3 / 4;
                if (font > l.lcd.w / 42) font = l.lcd.w / 42;
                if (px + tw + 2 > l.axis.x + l.axis.w) px = l.axis.x + l.axis.w - tw - 2;
                pv_text(fb, (struct pv_rect) {px + 2, l.axis.y, tw, l.axis.h}, b, font, 0x999999);
            }
        }
    }
    for (int d=0;d<4;d++) if (decks[d].sheet) {
        pv_sheet_draw(fb,&decks[d],d,left,right,az_value(AZF_OVERLAYS)?96:70);break;
    }

}
/* Native SOURCE/BROWSE and Track Info retain their layout and actions. Only
 * their existing per-deck footer surfaces acquire these compact CDJ readouts. */
void azmod_player_tile_draw(const azmod_fb *fb, const struct pv_deck *d, int index, unsigned elapsed_mask) {
    if (!fb || !fb->px || fb->w < 100 || fb->h < 45 || index < 0 || index > 3) return;
    pv_opaque_rect(fb, 0, 0, fb->w, fb->h, 0x000000);
    pv_opaque_rect(fb, 0, 0, fb->w, 1, d->selected ? 0xeeeeee : 0x444444);
    int gap = fb->w / 60 + 2, pw = fb->w / 12;
    struct pv_rect num = {gap, 4, pw, fb->h - 8};
    pv_frame(fb, num, 0x999999);
    char text[160];
    snprintf(text, sizeof text, "%d", index + 1);
    pv_text(fb, (struct pv_rect) {num.x + pw / 4, num.y, pw * 3 / 4, num.h}, text, fb->h / 3, 0xffffff);
    int x = gap * 2 + pw, tw = fb->w * 56 / 100;
    pv_text(fb, (struct pv_rect) {x, 2, fb->w - x - gap, fb->h / 4}, d->loaded == 0 ? "NOT LOADED" : d->title, fb->h / 6, 0xffffff);
    int elapsed = (elapsed_mask >> index) & 1u;
    if (d->duration <= 0) elapsed = 1;
    double seconds = elapsed ? d->elapsed : d->duration - d->elapsed;
    if (!isfinite(seconds) || seconds < 0 || d->elapsed < 0) snprintf(text, sizeof text, "%s --:--", elapsed ? "TIME" : "REMAIN");
    else snprintf(text, sizeof text, "%s %02d:%02d", elapsed ? "TIME" : "REMAIN", (int) seconds / 60, (int) seconds % 60);
    pv_text(fb, (struct pv_rect) {x, fb->h / 4, tw, fb->h / 3}, text, fb->h / 5, 0xffffff);
    if (d->bpm >= 20 && d->bpm <= 400) snprintf(text, sizeof text, "%.1f", d->bpm);
    else snprintf(text, sizeof text, "---.-");
    pv_text(fb, (struct pv_rect) {fb->w * 76 / 100, fb->h / 4, fb->w * 23 / 100, fb->h / 3}, text, fb->h / 4, d->master ? 0xff8c00 : 0xffffff);
    pv_overview_draw(fb, (struct pv_rect) {x, fb->h * 2 / 3, fb->w - x - gap, fb->h / 3 - 3}, d);

}
