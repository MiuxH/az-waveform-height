/* SPDX-License-Identifier: GPL-3.0-only */
/* Message-thread-only raster layers. Own every byte; never retain a native
 * image/analysis pointer. Exact source comparisons detect in-place updates.
 * If allocation/bounds/budget validation fails, callers draw normally. */
#ifndef AZMOD_PLAYER_RENDER_CACHE_H
#define AZMOD_PLAYER_RENDER_CACHE_H
#define PV_CACHE_BUDGET (16u*1024u*1024u)
#define PV_CACHE_SOURCE_MAX (512u*1024u)
struct pv_layer {
    unsigned char *pixels,*source;
    size_t pixel_capacity,source_capacity,source_n,key_n;
    unsigned char key[768];
    int w,h,valid;
};
static struct pv_layer g_pv_title_layers[4],g_pv_status_layers[4],g_pv_overview_layers[4];
static size_t g_pv_cache_bytes;
static _Atomic unsigned long g_pv_cache_hits,g_pv_cache_misses;
static int g_pv_cache_disabled; /* tests / performance A-B; no user setting */
static int pv_layer_rect(const azmod_fb *fb,struct pv_rect r) {
    return fb && fb->px && r.x>=0 && r.y>=0 && r.w>0 && r.h>0 && r.w<=1280 && r.h<=400 &&
        r.x<=fb->w-r.w && r.y<=fb->h-r.h && fb->stride>=fb->w*4;
}
static int pv_layer_get(const azmod_fb *fb,struct pv_rect r,struct pv_layer *c,
                        const void *key,size_t n,const void *source,size_t bytes) {
    if(g_pv_cache_disabled || !pv_layer_rect(fb,r) || n>sizeof c->key || bytes>PV_CACHE_SOURCE_MAX ||
       (bytes && !source) || !c->valid || c->w!=r.w || c->h!=r.h || c->key_n!=n ||
       c->source_n!=bytes || memcmp(c->key,key,n) || (bytes && memcmp(c->source,source,bytes))) {
        __atomic_fetch_add(&g_pv_cache_misses,1,__ATOMIC_RELAXED);return 0;
    }
    for(int y=0;y<r.h;y++)memcpy(fb->px+(long)(r.y+y)*fb->stride+4*r.x,c->pixels+(size_t)y*r.w*4,(size_t)r.w*4);
    __atomic_fetch_add(&g_pv_cache_hits,1,__ATOMIC_RELAXED);return 1;
}
static int pv_layer_reserve(unsigned char **data,size_t *capacity,size_t bytes) {
    if(bytes<=*capacity)return 1;
    if(bytes-*capacity>PV_CACHE_BUDGET-g_pv_cache_bytes)return 0;
    void *next=realloc(*data,bytes);
    if(!next)return 0;
    g_pv_cache_bytes+=bytes-*capacity;*data=next;*capacity=bytes;return 1;
}
static void pv_layer_put(const azmod_fb *fb,struct pv_rect r,struct pv_layer *c,
                         const void *key,size_t n,const void *source,size_t bytes) {
    if(g_pv_cache_disabled)return;
    c->valid=0;
    if( !pv_layer_rect(fb,r) || n>sizeof c->key || bytes>PV_CACHE_SOURCE_MAX ||
       (bytes && !source) || !pv_layer_reserve(&c->pixels,&c->pixel_capacity,(size_t)r.w*r.h*4) ||
       !pv_layer_reserve(&c->source,&c->source_capacity,bytes))return;
    for(int y=0;y<r.h;y++)memcpy(c->pixels+(size_t)y*r.w*4,fb->px+(long)(r.y+y)*fb->stride+4*r.x,(size_t)r.w*4);
    memcpy(c->key,key,n);if(bytes)memcpy(c->source,source,bytes);
    c->key_n=n;c->source_n=bytes;c->w=r.w;c->h=r.h;c->valid=1;
}
static inline void pv_layers_clear(void) {
    struct pv_layer *groups[]={g_pv_title_layers,g_pv_status_layers,g_pv_overview_layers};
    for(int k=0;k<3;k++)for(int d=0;d<4;d++) {
        struct pv_layer *c=&groups[k][d];free(c->pixels);free(c->source);memset(c,0,sizeof *c);
    }
    g_pv_cache_bytes=0;
}
#endif
