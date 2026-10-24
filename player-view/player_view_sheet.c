/* SPDX-License-Identifier: GPL-3.0-only */
/* Large, deck-labelled touch surface. Shared geometry is also used by X input. */
static const char *pv_sheet_name(int kind) {
    return kind==PV_SHEET_LOOP?"BEAT LOOP":kind==PV_SHEET_KEY?"KEY SHIFT":
           kind==PV_SHEET_JUMP?"BEAT JUMP / LOOP MOVE":kind==PV_SHEET_SLIP?"SLIP LOOP (AZ)":
           kind==PV_SHEET_INFO?"TRACK INFO":"";
}
static void pv_sheet_button(const azmod_fb *fb,struct pv_rect r,const char *label,int enabled,int lit) {
    pv_fill(fb,r,lit==2?PV_ORANGE:lit?PV_BLUE:PV_BUTTON); pv_frame(fb,r,enabled?PV_MUTED:PV_LINE);
    pv_fit(fb,(struct pv_rect){r.x+5,r.y,r.w-10,r.h},label,r.h*34/100,enabled?(lit==2?PV_BG:PV_WHITE):0x777777,1);
}
static void pv_info_lines(const azmod_fb *fb,struct pv_rect r,const char *text,int px) {
    size_t at=0;
    for (int row=0;text[at] && row<4;row++) {
        char part[80]; size_t n=strlen(text+at); if (n>64) n=64;
        if (text[at+n]) { size_t end=n; while(end>24 && text[at+end]!=' ') end--; if(end>24) n=end; }
        memcpy(part,text+at,n);part[n]=0;
        pv_title_text(fb,(struct pv_rect){r.x,r.y+row*34,r.w,32},part,px);
        at+=n; while(text[at]==' ') at++;
    }
}
static void pv_sheet_draw(const azmod_fb *fb,const struct pv_deck *d,int deck,int left,int right,int header) {
    if(d->sheet<PV_SHEET_LOOP || d->sheet>PV_SHEET_SLIP)return;
    struct pv_rect r=pv_sheet_rect(fb->w,fb->h,header);
    pv_fill(fb,r,PV_SURFACE);pv_frame(fb,r,PV_BLUE);
    char text[240];snprintf(text,sizeof text,"PLAYER %d  |  %s",deck+1,pv_sheet_name(d->sheet));
    pv_fit(fb,(struct pv_rect){r.x+16,r.y+8,r.w-140,42},text,24,PV_WHITE,0);
    pv_sheet_button(fb,pv_sheet_close(r),"CLOSE",1,0);
    int enabled=deck==left || deck==right;
    char time[32]="--:--.---",bpm_now[24]="---.-",prefix[100];
    if (enabled) snprintf(prefix,sizeof prefix,"Hardware side: %s",deck==left?"LEFT":"RIGHT");
    else snprintf(prefix,sizeof prefix,"Select PLAYER %d on hardware for audio controls",deck+1);
    if(d->loaded==1 && isfinite(d->elapsed) && isfinite(d->duration) && d->elapsed>=0 && d->duration>=d->elapsed && d->duration<86400) {
        long ms=(long)((d->duration-d->elapsed)*1000);
        snprintf(time,sizeof time,"%02ld:%02ld.%03ld",ms/60000,(ms/1000)%60,ms%1000);
    }
    if(d->loaded==1 && d->bpm>=20 && d->bpm<=400)snprintf(bpm_now,sizeof bpm_now,"%.1f",d->bpm);
    snprintf(text,sizeof text,"%s  |  REMAIN %s  |  BPM %s",prefix,time,bpm_now);
    pv_fit(fb,(struct pv_rect){r.x+16,r.y+60,r.w-32,36},text,18,enabled?PV_MUTED:PV_ORANGE,0);
    if (d->sheet==PV_SHEET_INFO) {
        pv_info_lines(fb,(struct pv_rect){r.x+20,r.y+118,r.w-40,136},d->title,26);
        pv_info_lines(fb,(struct pv_rect){r.x+20,r.y+258,r.w-40,136},d->artist,21);
        char length[32]="--",bpm[24]="--";
        if (isfinite(d->duration) && d->duration>0)snprintf(length,sizeof length,"%.1f s",d->duration);
        if (isfinite(d->track_bpm) && d->track_bpm>=20 && d->track_bpm<=400)snprintf(bpm,sizeof bpm,"%.1f",d->track_bpm);
        snprintf(text,sizeof text,"Length: %s    Original BPM: %s    Original key: %s",
                 length,bpm,d->key_name[0]?d->key_name:"--");
        pv_fit(fb,(struct pv_rect){r.x+20,r.y+404,r.w-40,40},text,20,PV_WHITE,0);
        pv_fit(fb,(struct pv_rect){r.x+20,r.y+448,r.w-40,40},"Use the shared native INFO control for additional track details.",18,PV_MUTED,0);
        return;
    }
    if (d->sheet!=PV_SHEET_TOOLS) {
        int mode=d->pad_mode;
        int ready=d->sheet==PV_SHEET_LOOP?(mode==3 || mode==4):d->sheet==PV_SHEET_KEY?mode==8:
            d->sheet==PV_SHEET_SLIP?mode==7:(mode==5 || mode==6);
        for (int pad=0;pad<8;pad++) {
            int native_pad=d->sheet==PV_SHEET_KEY?(int[]){5,1,4,0,-1,-1,-1,-1}[pad]:pad;
            const char *label=native_pad>=0?pv_pad_label(mode,d->pad_stems,native_pad):"";
            if (d->sheet==PV_SHEET_KEY && pad==0) label="-1 SEMITONE";
            if (d->sheet==PV_SHEET_KEY && pad==1) label="+1 SEMITONE";
            if (d->sheet==PV_SHEET_KEY && pad==2) label="RESET KEY";
            if (d->sheet==PV_SHEET_KEY && pad==3) label="KEY SYNC";
            if (native_pad<0) continue;
            pv_sheet_button(fb,pv_sheet_cell(r,pad/4,pad%4,4),ready?label:"CHANGING MODE",enabled && d->loaded==1 && ready && !d->pad_stems && !d->pad_drums,
                            native_pad>=0 && ((d->pads_down>>native_pad)&1u));
        }
        if (d->sheet==PV_SHEET_LOOP || d->sheet==PV_SHEET_JUMP) {
            int page=mode==4 || mode==6;
            pv_sheet_button(fb,pv_sheet_cell(r,2,0,4),"PAGE 1",enabled && ready && !d->pad_stems,!page);
            pv_sheet_button(fb,pv_sheet_cell(r,2,1,4),"PAGE 2",enabled && ready && !d->pad_stems,page);
            pv_sheet_button(fb,pv_sheet_cell(r,2,2,4),"RELOOP / EXIT",enabled,0);
        }
        if(d->sheet==PV_SHEET_LOOP || d->sheet==PV_SHEET_JUMP) {
            pv_sheet_button(fb,pv_sheet_cell(r,3,0,4),"4 BEAT / HALF",enabled,0);
            pv_sheet_button(fb,pv_sheet_cell(r,3,1,4),"8 BEAT / DOUBLE",enabled,0);
            pv_sheet_button(fb,pv_sheet_cell(r,3,3,4),"CLOSE",1,0);
        } else {
            pv_sheet_button(fb,pv_sheet_cell(r,3,1,2),"CLOSE",1,0);
        }
        const char *note=d->pad_drums?"AZ Drum Roll currently owns these pads. Slip Loop touch actions are unavailable.":
            d->sheet==PV_SHEET_KEY?"Key Shift changes the audio. The live shifted key is unavailable.":
            d->pad_stems?"Stems currently own these pads. Change the hardware pad page first.":
            "Lengths and loop movement follow the native AZ pad mode and beatgrid.";
        pv_fit(fb,(struct pv_rect){r.x+16,r.y+422,r.w-32,42},note,18,PV_MUTED,0);
        return;
    }
}
