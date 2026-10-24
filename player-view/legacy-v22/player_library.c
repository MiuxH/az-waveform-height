/* SPDX-License-Identifier: GPL-3.0-only */
/* Own read-only connections on one worker. No file or database I/O in paint,
 * no calls into another worker's SQLite connection, no retained EP147 objects. */
#include "player_cues.c"
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include "player_grid.c"
#ifndef PV_LIBRARY_MEDIA
#define PV_LIBRARY_MEDIA "/media/usb"
#endif
static struct {
    int (*open)(const char *, void **, int, const char *);
    int (*key)(void *, const void *, int);
    int (*close)(void *);
    int (*prepare)(void *, const char *, int, void **, const char **);
    int (*bind_id)(void *, int, long long);
    int (*step)(void *);
    const unsigned char *(*text)(void *, int);
    double (*number)(void *, int);
    int (*finish)(void *);
    int (*threadsafe)(void);
} g_pvsql;
static pthread_mutex_t g_pvlib_mu = PTHREAD_MUTEX_INITIALIZER;
static struct pv_library_data g_pvlib_data[4];
static int g_pvlib_started;
static azstem_deck_snapshot_fn g_pvlib_snapshot;
static void pv_analysis_release(struct pv_analysis *wave) {
    if (wave && __atomic_sub_fetch(&wave->refs,1,__ATOMIC_ACQ_REL)==0) free(wave);
}
static struct pv_analysis *pv_analysis_read_kind(const unsigned char *bytes, size_t n, unsigned wanted) {
    if (n<12 || memcmp(bytes,"PMAI",4) || pv_be32(bytes+8)!=n) return NULL;
    size_t pos=pv_be32(bytes+4);
    if (pos<12 || pos>n) return NULL;
    const unsigned char *samples=NULL; unsigned count=0, kind=0;
    while (pos<n) {
        if (n-pos<12) return NULL;
        const unsigned char *t=bytes+pos;
        size_t h=pv_be32(t+4), len=pv_be32(t+8);
        if (h<12 || len<h || len>n-pos) return NULL;
        if (!memcmp(t,"PWV3",4) || !memcmp(t,"PWV5",4) || !memcmp(t,"PWV7",4)) {
            unsigned next=t[3]=='3'?1:t[3]=='5'?2:3;
            if ((wanted && wanted!=next) || (!wanted && kind>next)) {pos+=len;continue;}
            kind=next;
            if (h!=24 || len<24 || pv_be32(t+12)!=kind) return NULL;
            count=pv_be32(t+16);
            if (!count || count>(len-h)/kind || count*kind!=len-h) return NULL;
            samples=t+h;
        }
        pos+=len;
    }
    if (!samples) return NULL;
    struct pv_analysis *wave=malloc(sizeof *wave+(size_t)count*kind);
    if (wave) {
        wave->refs=1; wave->count=count; wave->kind=kind;
        memcpy(wave->samples,samples,(size_t)count*kind);
        wave->summary_n=count<PV_SUMMARY_N?count:PV_SUMMARY_N;
        for (unsigned x=0;x<wave->summary_n;x++) {
            struct pv_peak peak={{0,0,0},0};
            unsigned lo=(unsigned)((uint64_t)x*count/wave->summary_n);
            unsigned hi=(unsigned)((uint64_t)(x+1)*count/wave->summary_n);
            for (unsigned i=lo;i<hi;i++) {
                const unsigned char *p=samples+(size_t)i*kind;
                if (kind==1) {
                    unsigned h=(p[0]>>3)*4096u/31;
                    if(h>peak.level[0])peak.level[0]=h;
                    peak.color=0x48b7ed; /* one genuine BLUE envelope, no invented bands */
                } else if (kind==2) {
                    unsigned bits=(unsigned)p[0]<<8|p[1], h=((bits>>2)&31)*4096/31;
                    if (h>=peak.level[0]) {
                        peak.level[0]=h;
                        peak.color=pv_rgb_color(bits);
                    }
                } else {
                    unsigned h[3]={p[2]*4096u*40/3100,p[0]*4096u*30/3100,p[1]*4096u*6/3100};
                    for (int k=0;k<3;k++) {
                        if (h[k]>4096) h[k]=4096;
                        if (h[k]>peak.level[k]) peak.level[k]=h[k];
                    }
                }
            }
            wave->summary[x]=peak;
        }
    }
    return wave;
}
static struct pv_analysis *pv_analysis_read(const unsigned char *bytes,size_t n) {
    return pv_analysis_read_kind(bytes,n,0);
}
static int pv_library_sql(void) {
#define PV_SQL(field, name) *(void **) &g_pvsql.field = dlsym(RTLD_DEFAULT, name)
    PV_SQL(open, "sqlite3_open_v2");
    PV_SQL(key, "sqlite3_key");
    PV_SQL(close, "sqlite3_close");
    PV_SQL(prepare, "sqlite3_prepare_v2");
    PV_SQL(bind_id, "sqlite3_bind_int64");
    PV_SQL(step, "sqlite3_step");
    PV_SQL(text, "sqlite3_column_text");
    PV_SQL(number, "sqlite3_column_double");
    PV_SQL(finish, "sqlite3_finalize");
    PV_SQL(threadsafe, "sqlite3_threadsafe");
#undef PV_SQL
    return g_pvsql.open && g_pvsql.key && g_pvsql.close && g_pvsql.prepare && g_pvsql.bind_id &&
           g_pvsql.step && g_pvsql.text && g_pvsql.number && g_pvsql.finish &&
           g_pvsql.threadsafe && g_pvsql.threadsafe();
}
static int pv_library_path(const char *mount, const char *relative, char path[1024]) {
    while (*relative == '/') relative++;
    if (strncmp(relative, "PIONEER/USBANLZ/", 16) || strstr(relative, "..") || strchr(relative, '\\') ||
        snprintf(path, 1024, "%s/%s", mount, relative) >= 1024) return 0;
    char real[4096], root[4096];
    if (!realpath(mount, root) || !realpath(path, real)) return 0;
    size_t n = strlen(root);
    if (strncmp(real, root, n) || real[n] != '/' || strlen(real) >= 1024) return 0;
    snprintf(path, 1024, "%s", real);
    return 1;
}
static void pv_library_cues(const char *path, struct pv_library_data *data, double elapsed) {
    FILE *f = fopen(path, "rb");
    if (!f) return;
    struct stat before, after;
    if (fstat(fileno(f), &before) || before.st_size < 12 || before.st_size > 16 * 1024 * 1024) {
        fclose(f);
        return;
    }
    size_t n = (size_t) before.st_size;
    unsigned char *bytes = malloc(n);
    if (bytes && fread(bytes, 1, n, f) == n && !fstat(fileno(f), &after) &&
        before.st_size == after.st_size && before.st_mtime == after.st_mtime) {
        pv_cue_parse(bytes, n, data->cues);
        pv_countdown_parse(bytes, n, elapsed, &data->countdown);
        pv_overview_parse(bytes, n, data);
        pv_memory_read(bytes,n,data);
        struct pv_beats *grid=pv_grid_read(bytes,n);
        if(grid) {pv_beats_release(data->beats);data->beats=grid;}
        struct pv_analysis *wave=pv_analysis_read(bytes,n);
        if (wave) {
            struct pv_analysis **slot=wave->kind==3?&data->wave3:wave->kind==1?&data->wave_blue:&data->wave;
            pv_analysis_release(*slot); *slot=wave;
        }
        if(!wave || wave->kind!=1) {
            struct pv_analysis *blue=pv_analysis_read_kind(bytes,n,1);
            if(blue) {pv_analysis_release(data->wave_blue);data->wave_blue=blue;}
        }
    }
    free(bytes);
    fclose(f);
}
static int pv_library_one(const char *mount, const struct azstem_deck_snap *sn, struct pv_library_data *out) {
    char path[1024], uri[1100];
    if (snprintf(path, sizeof path, "%s/PIONEER/rekordbox/exportLibrary.db", mount) >= (int) sizeof path) return 0;
    struct stat before, after;
    if (stat(path, &before)) return 0;
    snprintf(uri, sizeof uri, "file:%s?immutable=1", path);
    /* URI metacharacters would change which file SQLite opens. */
    if (strchr(path, '?') || strchr(path, '#') || strchr(path, '%')) return 0;
    void *db = NULL, *st = NULL;
    int match = 0;
    char analysis[768] = "";
    if (g_pvsql.open(uri, &db, 0x01 | 0x40 | 0x10000, NULL) || !db) goto done;
    if (g_pvsql.key(db, ONELIBRARY_KEY_SM, sizeof ONELIBRARY_KEY_SM - 1)) goto done;
    const char *query = "SELECT c.title,a.name,c.length,k.name,c.isHotCueAutoLoadOn,c.analysisDataFilePath "
                        "FROM content c LEFT JOIN artist a ON a.artist_id=c.artist_id_artist "
                        "LEFT JOIN key k ON k.key_id=c.key_id WHERE c.content_id=?1 LIMIT 2";
    if (g_pvsql.prepare(db, query, -1, &st, NULL) || !st || g_pvsql.bind_id(st, 1, sn->content_id)) goto done;
    if (g_pvsql.step(st) != 100) goto done;
    const unsigned char *title = g_pvsql.text(st, 0), *artist = g_pvsql.text(st, 1);
    char ti[128], ar[128];
    snprintf(ti, sizeof ti, "%s", title ? (const char *) title : "");
    snprintf(ar, sizeof ar, "%s", artist ? (const char *) artist : "");
    if (strcmp(ti, sn->title) || strcmp(ar, sn->artist)) goto done;
    memset(out, 0, sizeof *out);
    out->content_id = sn->content_id;
    snprintf(out->title, sizeof out->title, "%s", ti);
    snprintf(out->artist, sizeof out->artist, "%s", ar);
    const unsigned char *key = g_pvsql.text(st, 3), *an = g_pvsql.text(st, 5);
    snprintf(out->key, sizeof out->key, "%s", key ? (const char *) key : "");
    snprintf(analysis, sizeof analysis, "%s", an ? (const char *) an : "");
    out->duration = g_pvsql.number(st, 2);
    out->auto_hotcue = g_pvsql.number(st, 4) != 0;
    match = g_pvsql.step(st) == 101;
done:
    if (st) g_pvsql.finish(st);
    if (db) g_pvsql.close(db);
    if (!match || stat(path, &after) || before.st_size != after.st_size || before.st_mtime != after.st_mtime) return 0;
    /* Extended cues first, preserving D-H and assigned colours. A legacy file
     * can fill missing slots but cannot overwrite an extended record. */
    size_t len = strlen(analysis);
    if (len > 4 && len < sizeof analysis - 1) {
        memcpy(analysis + len - 4, ".EXT", 5);
        if (pv_library_path(mount, analysis, path)) pv_library_cues(path, out, sn->pos_s);
        memcpy(analysis + len - 4, ".2EX", 5);
        if (pv_library_path(mount, analysis, path)) pv_library_cues(path, out, sn->pos_s);
        memcpy(analysis + len - 4, ".DAT", 5);
        if (pv_library_path(mount, analysis, path)) pv_library_cues(path, out, sn->pos_s);
    }
    return 1;
}
static void pv_library_scan(const struct azstem_deck_snap *sn, struct pv_library_data *out) {
    memset(out, 0, sizeof *out);
    if (sn->loaded != 1 || !sn->content_id || !sn->title[0]) return;
    DIR *dir = opendir(PV_LIBRARY_MEDIA);
    if (!dir) return;
    struct dirent *entry;
    int matches = 0, seen = 0;
    while ((entry = readdir(dir)) && seen < 32) {
        if (entry->d_name[0] == '.') continue;
        seen++;
        char mount[768];
        if (snprintf(mount, sizeof mount, "%s/%s", PV_LIBRARY_MEDIA, entry->d_name) >= (int) sizeof mount) continue;
        struct pv_library_data candidate;
        if (pv_library_one(mount, sn, &candidate)) {
            if (++matches == 1) *out = candidate;
            else {
                pv_analysis_release(candidate.wave);
                pv_analysis_release(candidate.wave3);
                pv_analysis_release(candidate.wave_blue);
                pv_beats_release(candidate.beats);
                break; /* Same title/id on two sticks: source identity is unknown. */
            }
        }
    }
    closedir(dir);
    if (matches != 1) {
        pv_analysis_release(out->wave); pv_analysis_release(out->wave3); pv_analysis_release(out->wave_blue);
        pv_beats_release(out->beats);
        memset(out, 0, sizeof *out);
    }
}
static void *pv_library_worker(void *unused) {
    (void) unused;
    if (!pv_library_sql()) return NULL;
    for (;;) {
        if (az_on(AZF_PLAYER_VIEW)) {
            for (int d = 0; d < 4; d++) {
                struct azstem_deck_snap sn = {.size = sizeof sn};
                struct pv_library_data next = {0};
                if (!g_pvlib_snapshot(d, &sn)) pv_library_scan(&sn, &next);
                pthread_mutex_lock(&g_pvlib_mu);
                struct pv_analysis *old=g_pvlib_data[d].wave;
                struct pv_analysis *old3=g_pvlib_data[d].wave3;
                struct pv_analysis *old_blue=g_pvlib_data[d].wave_blue;
                struct pv_beats *old_grid=g_pvlib_data[d].beats;
                g_pvlib_data[d] = next;
                pthread_mutex_unlock(&g_pvlib_mu);
                pv_analysis_release(old);
                pv_analysis_release(old3);
                pv_analysis_release(old_blue);
                pv_beats_release(old_grid);
            }
        }
        for (int i = 0; i < 20; i++) usleep(250000);
    }
    return NULL;
}
static void pv_library_start(azstem_deck_snapshot_fn snapshot) {
    if (g_pvlib_started || !snapshot) return;
    g_pvlib_snapshot = snapshot;
    pthread_t t;
    if (!az_thread_create(&t, NULL, pv_library_worker, NULL)) {
        g_pvlib_started = 1;
        pthread_detach(t);
    }
}
static int pv_library_copy(int deck, const struct azstem_deck_snap *sn, struct pv_library_data *out) {
    if (deck < 0 || deck >= 4 || sn->loaded != 1 || pthread_mutex_trylock(&g_pvlib_mu)) return 0;
    const struct pv_library_data *v = &g_pvlib_data[deck];
    int match = v->content_id && v->content_id == sn->content_id &&
                !strcmp(v->title, sn->title) && !strcmp(v->artist, sn->artist);
    if (match) {
        *out = *v;
        if (out->wave) __atomic_add_fetch(&out->wave->refs,1,__ATOMIC_RELAXED);
        if (out->wave3) __atomic_add_fetch(&out->wave3->refs,1,__ATOMIC_RELAXED);
        if (out->wave_blue) __atomic_add_fetch(&out->wave_blue->refs,1,__ATOMIC_RELAXED);
        if (out->beats)__atomic_add_fetch(&out->beats->refs,1,__ATOMIC_RELAXED);
    }
    pthread_mutex_unlock(&g_pvlib_mu);
    return match;
}
