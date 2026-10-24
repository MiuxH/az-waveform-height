/* SPDX-License-Identifier: GPL-3.0-only */
/* Complete exported beat times, immutable and reference counted like detail.
 * No BPM extrapolation: variable-tempo tracks retain every actual timestamp. */
static void pv_beats_release(struct pv_beats *grid) {
    if(grid && __atomic_sub_fetch(&grid->refs,1,__ATOMIC_ACQ_REL)==0)free(grid);
}
static struct pv_beats *pv_grid_read(const unsigned char *p,size_t n) {
    struct pv_cue checked[8]={{0}};
    if(pv_cue_parse(p,n,checked)<0)return NULL;
    const unsigned char *entries=NULL;unsigned count=0;
    for(size_t pos=pv_be32(p+4);pos<n;pos+=pv_be32(p+pos+8)) {
        const unsigned char *t=p+pos;
        if(memcmp(t,"PQTZ",4))continue;
        unsigned h=pv_be32(t+4),len=pv_be32(t+8),c;
        if(h!=24 || len<24)return NULL;
        c=pv_be32(t+20);
        if(c<2 || c>524288 || c!=(len-24)/8 || (len-24)%8)return NULL;
        entries=t+24;count=c;
    }
    if(!entries)return NULL;
    struct pv_beats *grid=malloc(sizeof *grid+(size_t)count*sizeof(double));
    if(!grid)return NULL;
    grid->refs=1;grid->count=count;grid->first=0;
    unsigned previous=0,beat_before=0;
    for(unsigned i=0;i<count;i++) {
        const unsigned char *e=entries+8*i;
        unsigned beat=(unsigned)e[0]<<8|e[1],ms=pv_be32(e+4);
        if(beat<1 || beat>4 || ms>=86400000u || (i && (ms<=previous || beat!=beat_before%4+1))) {
            free(grid);return NULL;
        }
        if(!i)grid->first=beat-1;
        grid->times[i]=ms/1000.0;previous=ms;beat_before=beat;
    }
    return grid;
}
static void pv_memory_read(const unsigned char *p,size_t n,struct pv_library_data *data) {
    struct pv_cue checked[8]={{0}},next[PV_MEMORY_N]={{0}};
    if(pv_cue_parse(p,n,checked)<0)return;
    int count=0,format=0;
    for(size_t pos=pv_be32(p+4);pos<n;pos+=pv_be32(p+pos+8)) {
        const unsigned char *t=p+pos;
        int ext=!memcmp(t,"PCO2",4),old=!memcmp(t,"PCOB",4);
        if(!(ext || old) || pv_be32(t+12)!=0)continue;
        if(ext && format<2) {count=0;format=2;}
        if(!ext && format==2)continue;
        if(!format)format=1;
        unsigned records=(unsigned)t[ext?16:18]<<8|t[ext?17:19];
        size_t at=pv_be32(t+4);
        for(unsigned i=0;i<records;i++) {
            const unsigned char *e=t+at;
            unsigned type=e[ext?16:28],ms=pv_be32(e+(ext?20:32)),end=pv_be32(e+(ext?24:36));
            uint32_t color=type==2?0xff981a:0xf2f3f5;
            if(ext && pv_be32(e+8)>43) {
                unsigned comment=pv_be32(e+40),len=pv_be32(e+8);
                if(len-comment>=48) {
                    const unsigned char *c=e+44+comment;
                    unsigned rgb=(unsigned)c[1]<<16|(unsigned)c[2]<<8|c[3];
                    if(rgb)color=rgb;
                }
            }
            if(!pv_be32(e+12) && (type==1 || type==2) && ms<86400000u &&
                (type!=2 || (end>ms && end<86400000u)) && count<PV_MEMORY_N)
                next[count++]=(struct pv_cue){ms/1000.0,type==2?end/1000.0:-1,color,1};
            at+=pv_be32(e+8);
        }
    }
    if(format>=data->memory_format && format) {
        memcpy(data->memory,next,sizeof next);data->memory_n=count;data->memory_format=format;
    }
}
