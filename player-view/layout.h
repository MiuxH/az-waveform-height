/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PV_COMMUNITY_LAYOUT_H
#define PV_COMMUNITY_LAYOUT_H
#include <stdint.h>
#include <math.h>
#include "player_view.h"
typedef struct {int x,y,w,h;} rect;
struct layout {rect panel,title,compare,wave,status,preview;};
static inline int inside(rect r,int x,int y) {
    return r.w>0 && r.h>0 && x>=r.x && y>=r.y && x-r.x<r.w && y-r.y<r.h;
}
static inline int valid_rect(rect r,int w,int h) {
    return r.x>=0 && r.y>=0 && r.w>0 && r.h>0 && r.x<=w-r.w && r.y<=h-r.h;
}
static inline int count_for(int choice){return choice==1?2:choice==2?4:1;}
static inline struct layout geometry(int width,int count,int index) {
    struct layout l={0};
    if(width<900 || width>1280 || (count!=1 && count!=2 && count!=4) || index<0 || index>=count)return l;
    struct pv_layout old=pv_geometry_at(width,800,count,index,96);
#define COPY_RECT(name,src) l.name=(rect){old.src.x,old.src.y,old.src.w,old.src.h}
    COPY_RECT(panel,panel);COPY_RECT(title,title);COPY_RECT(compare,compare);COPY_RECT(wave,wave);
    l.status=(rect){old.panel.x,old.player.y,old.panel.w,old.player.h};
    l.preview=(rect){old.cues.x,old.cues.y,old.cues.w,old.axis.y+old.axis.h-old.cues.y};
#undef COPY_RECT
    return l;
}
static inline int player_for(int count,int i,int focus,int left,int right) {
    return count==4?i:count==2?(i?right:left):focus;
}
/* Source and destination slices share the same x mapping. Keep native ruler
 * heights instead of vertically stretching beat ticks and cue text with audio. */
static inline int slices(rect dst,int sw,int sh,int ruler,rect src[3],rect out[3]) {
    if(sw<1 || sh<16 || ruler<1 || ruler*2>=sh || dst.w<1 || dst.h<=ruler*2)return 0;
    src[0]=(rect){0,0,sw,ruler};src[1]=(rect){0,ruler,sw,sh-2*ruler};
    src[2]=(rect){0,sh-ruler,sw,ruler};
    out[0]=(rect){dst.x,dst.y,dst.w,ruler};
    out[1]=(rect){dst.x,dst.y+ruler,dst.w,dst.h-2*ruler};
    out[2]=(rect){dst.x,dst.y+dst.h-ruler,dst.w,ruler};
    return 1;
}
/* AffineTransform is six row-major floats in this verified JUCE build. */
static inline int mapping(rect source,rect dest,float m[6]) {
    if(source.w<1 || source.h<1 || dest.w<1 || dest.h<1)return 0;
    float sx=(float)dest.w/source.w,sy=(float)dest.h/source.h;
    m[0]=sx;m[1]=0;m[2]=dest.x-source.x*sx;
    m[3]=0;m[4]=sy;m[5]=dest.y-source.y*sy;
    return 1;
}
#endif
