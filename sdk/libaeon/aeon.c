/* libaeon -- see aeon.h. HTTP via libcurl; JSON via cJSON; one worker thread; callbacks on aeon_tick(). */
#define _GNU_SOURCE
#include "aeon.h"
#include "third_party/cJSON/cJSON.h"
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ---------------- state ---------------- */
static int g_on = 0, g_inited = 0;
static char g_api[512], g_token[4096], g_sku[128], g_queue_path[1024];
static AeonPlayer g_player;
static char g_owned[32][128]; static int g_owned_n = 0;
static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cv = PTHREAD_COND_INITIALIZER;
static pthread_t g_worker; static int g_stop = 0;

typedef enum { J_UNLOCK, J_PROGRESS, J_SUBMIT, J_PRESENCE, J_EVENT, J_LB_GET, J_PURCHASE, J_PURCHASE_POLL } JobKind;
typedef struct Job {
    JobKind kind; int durable;          /* durable jobs survive offline (queued to disk) */
    char a[160]; char b[4096]; int n; int friends; int64_t score;
    AeonLbCallback lb_cb; AeonPurchaseCallback p_cb; void* user; int tries; time_t next_at;
    struct Job* next;
} Job;
static Job* g_jobs = NULL;      /* pending, worker-owned */
typedef struct Done { int kind; AeonLeaderboard* lb; char sku[128]; int completed; AeonLbCallback lb_cb; AeonPurchaseCallback p_cb; void* user; struct Done* next; } Done;
static Done* g_done = NULL;     /* completed, delivered on tick */

/* ---------------- http ---------------- */
typedef struct { char* data; size_t len; } Buf;
/* V183: engines commonly link their own libcurl/OpenSSL into the executable (Unreal does), and those symbols win over
 * a shared library's undefined references. Load the system libcurl explicitly and call it through our own pointers;
 * RTLD_DEEPBIND keeps its TLS backend bound to the system libraries as well. */
#include <dlfcn.h>
static CURL* (*p_easy_init)(void);
static CURLcode (*p_easy_setopt)(CURL*, CURLoption, ...);
static CURLcode (*p_easy_perform)(CURL*);
static CURLcode (*p_easy_getinfo)(CURL*, CURLINFO, ...);
static void (*p_easy_cleanup)(CURL*);
static struct curl_slist* (*p_slist_append)(struct curl_slist*, const char*);
static void (*p_slist_free_all)(struct curl_slist*);
static CURLcode (*p_global_init)(long);
static void (*p_global_cleanup)(void);
static int curl_load(void) {
    static void* h;
    if (h) return 1;
    const char* names[] = { "libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so", NULL };
    for (int i = 0; names[i] && !h; i++) h = dlopen(names[i], RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND);
    if (!h) { fprintf(stderr, "[aeon] libcurl.so.4 not found -- reports will queue\n"); return 0; }
    *(void**)&p_easy_init = dlsym(h, "curl_easy_init");       *(void**)&p_easy_setopt = dlsym(h, "curl_easy_setopt");
    *(void**)&p_easy_perform = dlsym(h, "curl_easy_perform"); *(void**)&p_easy_getinfo = dlsym(h, "curl_easy_getinfo");
    *(void**)&p_easy_cleanup = dlsym(h, "curl_easy_cleanup"); *(void**)&p_slist_append = dlsym(h, "curl_slist_append");
    *(void**)&p_slist_free_all = dlsym(h, "curl_slist_free_all"); *(void**)&p_global_init = dlsym(h, "curl_global_init");
    *(void**)&p_global_cleanup = dlsym(h, "curl_global_cleanup");
    return p_easy_init && p_easy_setopt && p_easy_perform && p_easy_getinfo && p_easy_cleanup && p_slist_append && p_slist_free_all && p_global_init;
}

static size_t on_write(char* p, size_t s, size_t n, void* ud) { Buf* b = ud; size_t add = s * n; char* d = realloc(b->data, b->len + add + 1); if (!d) return 0; b->data = d; memcpy(b->data + b->len, p, add); b->len += add; b->data[b->len] = 0; return add; }
/* returns HTTP status (0 = transport failure) */
static long http(const char* method, const char* path, const char* body, Buf* out) {
    CURL* h = p_easy_init(); if (!h) return 0;
    char url[1024]; snprintf(url, sizeof url, "%s%s", g_api, path);
    char auth[4200]; snprintf(auth, sizeof auth, "Authorization: Bearer %s", g_token);
    struct curl_slist* hd = p_slist_append(NULL, auth); hd = p_slist_append(hd, "Content-Type: application/json");
    hd = p_slist_append(hd, "User-Agent: libaeon/" AEON_SDK_VERSION);
    p_easy_setopt(h, CURLOPT_URL, url); p_easy_setopt(h, CURLOPT_HTTPHEADER, hd);
    p_easy_setopt(h, CURLOPT_CUSTOMREQUEST, method); p_easy_setopt(h, CURLOPT_TIMEOUT, 15L); p_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 5L);
    if (body) p_easy_setopt(h, CURLOPT_POSTFIELDS, body);
    Buf tmp = {0}; Buf* b = out ? out : &tmp; p_easy_setopt(h, CURLOPT_WRITEFUNCTION, on_write); p_easy_setopt(h, CURLOPT_WRITEDATA, b);
    long code = 0; if (p_easy_perform(h) == CURLE_OK) p_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
    p_slist_free_all(hd); p_easy_cleanup(h); if (!out) free(tmp.data);
    return code;
}

/* ---------------- offline queue (jsonl) ---------------- */
static void queue_write_all(void) {   /* rewrite the file from durable pending jobs; caller holds g_mx */
    FILE* f = fopen(g_queue_path, "w"); if (!f) return;
    for (Job* j = g_jobs; j; j = j->next) if (j->durable)
        fprintf(f, "{\"k\":%d,\"a\":\"%s\",\"b\":%s,\"n\":%d,\"s\":%lld}\n", (int)j->kind, j->a, j->b[0] ? j->b : "null", j->n, (long long)j->score);
    fclose(f);
}
static void queue_load(void) {
    FILE* f = fopen(g_queue_path, "r"); if (!f) return;
    char line[8192]; int n = 0;
    while (fgets(line, sizeof line, f)) {
        cJSON* j = cJSON_Parse(line); if (!j) continue;
        Job* jb = calloc(1, sizeof *jb); jb->kind = (JobKind)cJSON_GetNumberValue(cJSON_GetObjectItem(j, "k")); jb->durable = 1;
        const char* a = cJSON_GetStringValue(cJSON_GetObjectItem(j, "a")); if (a) snprintf(jb->a, sizeof jb->a, "%s", a);
        cJSON* b = cJSON_GetObjectItem(j, "b"); if (b && !cJSON_IsNull(b)) { char* s = cJSON_PrintUnformatted(b); if (s) { snprintf(jb->b, sizeof jb->b, "%s", s); free(s); } }
        jb->n = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(j, "n")); jb->score = (int64_t)cJSON_GetNumberValue(cJSON_GetObjectItem(j, "s"));
        cJSON_Delete(j);
        jb->next = g_jobs; g_jobs = jb; n++;
    }
    fclose(f);
    if (n) fprintf(stderr, "[aeon] %d queued report(s) from a previous session\n", n);
}

/* ---------------- worker ---------------- */
static void push_done(Done* d) { d->next = NULL; pthread_mutex_lock(&g_mx); Done** p = &g_done; while (*p) p = &(*p)->next; *p = d; pthread_mutex_unlock(&g_mx); }
static void json_escape(const char* s, char* out, size_t n) { size_t o = 0; for (; *s && o + 2 < n; s++) { if (*s == '"' || *s == '\\') out[o++] = '\\'; out[o++] = *s; } out[o] = 0; }

/* returns 1 done (success or permanent failure), 0 retry later */
static int run_job(Job* j) {
    char path[512], body[4600], esc[400]; Buf out = {0}; long code = 0;
    switch (j->kind) {
    case J_UNLOCK:   snprintf(path, sizeof path, "/sdk/achievements/%s/unlock", j->a); code = http("POST", path, "{}", &out); break;
    case J_PROGRESS: snprintf(path, sizeof path, "/sdk/achievements/%s/progress", j->a); snprintf(body, sizeof body, "{\"value\":%d,\"max\":%lld}", j->n, (long long)j->score); code = http("POST", path, body, &out); break;
    case J_SUBMIT:   snprintf(path, sizeof path, "/sdk/leaderboards/%s/submit", j->a); snprintf(body, sizeof body, "{\"score\":%lld,\"metadata\":%s}", (long long)j->score, j->b[0] ? j->b : "null"); code = http("POST", path, body, &out); break;
    case J_PRESENCE: json_escape(j->a, esc, sizeof esc); snprintf(body, sizeof body, "{\"status\":\"%s\"}", esc); code = http("PUT", "/sdk/presence", body, &out); break;
    case J_EVENT:    json_escape(j->a, esc, sizeof esc); snprintf(body, sizeof body, "[{\"name\":\"%s\",\"payload\":%s}]", esc, j->b[0] ? j->b : "null"); code = http("POST", "/sdk/events", body, &out); break;
    case J_LB_GET: {
        snprintf(path, sizeof path, "/sdk/leaderboards/%s?n=%d&friends=%d", j->a, j->n > 0 ? j->n : 25, j->friends ? 1 : 0);
        code = http("GET", path, NULL, &out);
        Done* d = calloc(1, sizeof *d); d->kind = J_LB_GET; d->lb_cb = j->lb_cb; d->user = j->user; d->lb = calloc(1, sizeof *d->lb);
        if (code == 200 && out.data) { cJSON* r = cJSON_Parse(out.data); if (r) {
            cJSON* e; cJSON_ArrayForEach(e, cJSON_GetObjectItem(r, "entries")) { if (d->lb->count >= 100) break; AeonLbEntry* x = &d->lb->entries[d->lb->count++];
                x->rank = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(e, "rank")); x->score = (int64_t)cJSON_GetNumberValue(cJSON_GetObjectItem(e, "score"));
                x->me = cJSON_IsTrue(cJSON_GetObjectItem(e, "me")); const char* g = cJSON_GetStringValue(cJSON_GetObjectItem(e, "gamertag")); if (g) snprintf(x->gamertag, sizeof x->gamertag, "%s", g); }
            cJSON* mr = cJSON_GetObjectItem(r, "my_rank"); if (mr && !cJSON_IsNull(mr)) { d->lb->has_me = 1; d->lb->my_rank = (int)cJSON_GetNumberValue(mr); d->lb->my_score = (int64_t)cJSON_GetNumberValue(cJSON_GetObjectItem(r, "my_score")); }
            cJSON_Delete(r); } }
        push_done(d); free(out.data); return 1; }
    case J_PURCHASE: {
        json_escape(j->a, esc, sizeof esc); snprintf(body, sizeof body, "{\"sku\":\"%s\"}", esc); code = http("POST", "/sdk/purchase", body, &out);
        if (code == 200 && out.data) { cJSON* r = cJSON_Parse(out.data); const char* st = r ? cJSON_GetStringValue(cJSON_GetObjectItem(r, "status")) : NULL; const char* id = r ? cJSON_GetStringValue(cJSON_GetObjectItem(r, "id")) : NULL;
            if (st && strcmp(st, "completed") == 0) { Done* d = calloc(1, sizeof *d); d->kind = J_PURCHASE; d->p_cb = j->p_cb; d->user = j->user; d->completed = 1; snprintf(d->sku, sizeof d->sku, "%s", j->a); push_done(d); }
            else if (id) { Job* p = calloc(1, sizeof *p); p->kind = J_PURCHASE_POLL; snprintf(p->a, sizeof p->a, "%s", id); snprintf(p->b, sizeof p->b, "%s", j->a); p->p_cb = j->p_cb; p->user = j->user; p->next_at = time(NULL) + 2;
                pthread_mutex_lock(&g_mx); p->next = g_jobs; g_jobs = p; pthread_mutex_unlock(&g_mx); }
            if (r) cJSON_Delete(r); }
        else { Done* d = calloc(1, sizeof *d); d->kind = J_PURCHASE; d->p_cb = j->p_cb; d->user = j->user; d->completed = 0; snprintf(d->sku, sizeof d->sku, "%s", j->a); push_done(d); }
        free(out.data); return 1; }
    case J_PURCHASE_POLL: {
        snprintf(path, sizeof path, "/sdk/purchase/%s", j->a); code = http("GET", path, NULL, &out);
        const char* st = NULL; cJSON* r = (code == 200 && out.data) ? cJSON_Parse(out.data) : NULL; if (r) st = cJSON_GetStringValue(cJSON_GetObjectItem(r, "status"));
        int finished = st && strcmp(st, "pending") != 0;
        if (finished || j->tries > 300) { Done* d = calloc(1, sizeof *d); d->kind = J_PURCHASE; d->p_cb = j->p_cb; d->user = j->user; d->completed = st && strcmp(st, "completed") == 0; snprintf(d->sku, sizeof d->sku, "%s", j->b); push_done(d); if (r) cJSON_Delete(r); free(out.data); return 1; }
        if (r) cJSON_Delete(r); free(out.data); j->tries++; j->next_at = time(NULL) + 2; return 0; }
    }
    free(out.data);
    if (code == 0) { j->tries++; j->next_at = time(NULL) + (j->tries < 5 ? 2 * j->tries : 30); return 0; }   /* no network: retry */
    if (code >= 500) { j->tries++; if (j->tries > 5) return 1; j->next_at = time(NULL) + 5 * j->tries; return 0; }
    return 1;   /* 2xx done; 4xx permanent (bad id etc.) */
}

static void* worker_main(void* ud) {
    (void)ud;
    for (;;) {
        pthread_mutex_lock(&g_mx);
        while (!g_stop) {
            time_t now = time(NULL); Job* pick = NULL; Job** pp = &g_jobs;
            for (Job** p = &g_jobs; *p; p = &(*p)->next) if ((*p)->next_at <= now) { pick = *p; pp = p; break; }
            if (pick) { *pp = pick->next; pick->next = NULL; pthread_mutex_unlock(&g_mx);
                int done = run_job(pick);
                pthread_mutex_lock(&g_mx);
                if (!done) { pick->next = g_jobs; g_jobs = pick; }
                if (pick->durable) queue_write_all();
                if (done) free(pick);
                pthread_mutex_unlock(&g_mx); pthread_mutex_lock(&g_mx); continue; }
            struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += 1; pthread_cond_timedwait(&g_cv, &g_mx, &ts);
        }
        pthread_mutex_unlock(&g_mx); return NULL;
    }
}
static void enqueue(Job* j) { if (!g_on) { if (j->kind == J_LB_GET) { Done* d = calloc(1, sizeof *d); d->kind = J_LB_GET; d->lb_cb = j->lb_cb; d->user = j->user; d->lb = calloc(1, sizeof *d->lb); push_done(d); } else if (j->kind == J_PURCHASE) { Done* d = calloc(1, sizeof *d); d->kind = J_PURCHASE; d->p_cb = j->p_cb; d->user = j->user; snprintf(d->sku, sizeof d->sku, "%s", j->a); push_done(d); } free(j); return; }
    pthread_mutex_lock(&g_mx); j->next = g_jobs; g_jobs = j; if (j->durable) queue_write_all(); pthread_cond_signal(&g_cv); pthread_mutex_unlock(&g_mx); }

/* ---------------- public ---------------- */
const char* aeon_sdk_version(void) { return AEON_SDK_VERSION; }
const AeonPlayer* aeon_player(void) { return &g_player; }

int aeon_init(void) {
    if (g_inited) return g_on;
    g_inited = 1; memset(&g_player, 0, sizeof g_player); snprintf(g_player.gamertag, sizeof g_player.gamertag, "Player"); snprintf(g_player.lang, sizeof g_player.lang, "en");
    const char* on = getenv("AEON_SDK"), *api = getenv("AEON_API_URL"), *tok = getenv("AEON_SESSION"), *sku = getenv("AEON_SKU");
    if (!on || strcmp(on, "1") != 0 || !api || !tok || !sku) { g_on = 0; return 0; }
    snprintf(g_api, sizeof g_api, "%s", api); snprintf(g_token, sizeof g_token, "%s", tok); snprintf(g_sku, sizeof g_sku, "%s", sku);
    const char* lang = getenv("AEON_LANG"); if (lang) snprintf(g_player.lang, sizeof g_player.lang, "%s", lang);
    const char* pid = getenv("AEON_PLAYER_ID"); if (pid) snprintf(g_player.id, sizeof g_player.id, "%s", pid);
    if (!curl_load()) { g_on = 0; return 0; }
    p_global_init(CURL_GLOBAL_DEFAULT);
    const char* st = getenv("XDG_STATE_HOME"); const char* home = getenv("HOME"); char dir[900];
    if (st) snprintf(dir, sizeof dir, "%s/aeon/%s", st, sku); else snprintf(dir, sizeof dir, "%s/.local/state/aeon/%s", home ? home : "/tmp", sku);
    char mk[1024]; snprintf(mk, sizeof mk, "mkdir -p '%s'", dir); if (system(mk) != 0) {} snprintf(g_queue_path, sizeof g_queue_path, "%s/queue.jsonl", dir);
    g_on = 1;
    /* who's playing (bounded) and what they own */
    Buf out = {0}; if (http("GET", "/sdk/me", NULL, &out) == 200 && out.data) { cJSON* r = cJSON_Parse(out.data); if (r) {
        const char* g = cJSON_GetStringValue(cJSON_GetObjectItem(r, "gamertag")); if (g) snprintf(g_player.gamertag, sizeof g_player.gamertag, "%s", g);
        const char* id = cJSON_GetStringValue(cJSON_GetObjectItem(r, "player_id")); if (id) snprintf(g_player.id, sizeof g_player.id, "%s", id);
        cJSON_Delete(r); } } free(out.data);
    Buf o2 = {0}; if (http("GET", "/sdk/entitlements", NULL, &o2) == 200 && o2.data) { cJSON* r = cJSON_Parse(o2.data); if (r) { cJSON* e; cJSON_ArrayForEach(e, cJSON_GetObjectItem(r, "owned")) { if (g_owned_n < 32 && cJSON_IsString(e)) snprintf(g_owned[g_owned_n++], 128, "%s", e->valuestring); } cJSON_Delete(r); } } free(o2.data);
    g_player.on_aeon = 1;
    queue_load();
    pthread_create(&g_worker, NULL, worker_main, NULL);
    return 1;
}
void aeon_tick(void) {
    Done* list; pthread_mutex_lock(&g_mx); list = g_done; g_done = NULL; pthread_mutex_unlock(&g_mx);
    while (list) { Done* d = list; list = d->next;
        if (d->kind == J_LB_GET && d->lb_cb) d->lb_cb(d->lb, d->user);
        if (d->kind == J_PURCHASE && d->p_cb) d->p_cb(d->sku, d->completed, d->user);
        free(d->lb); free(d); }
}
void aeon_shutdown(void) {
    if (!g_inited) return;
    if (g_on) { pthread_mutex_lock(&g_mx); g_stop = 1; pthread_cond_broadcast(&g_cv); queue_write_all(); pthread_mutex_unlock(&g_mx); pthread_join(g_worker, NULL); if (p_global_cleanup) p_global_cleanup(); }
    g_inited = 0; g_on = 0;
}
void aeon_achievement_unlock(const char* id) { if (!id) return; Job* j = calloc(1, sizeof *j); j->kind = J_UNLOCK; j->durable = 1; snprintf(j->a, sizeof j->a, "%s", id); enqueue(j); }
void aeon_achievement_progress(const char* id, int value, int max) { if (!id) return; Job* j = calloc(1, sizeof *j); j->kind = J_PROGRESS; j->durable = 1; snprintf(j->a, sizeof j->a, "%s", id); j->n = value; j->score = max; enqueue(j); }
void aeon_leaderboard_submit(const char* category, int64_t score, const char* meta) { if (!category) return; Job* j = calloc(1, sizeof *j); j->kind = J_SUBMIT; j->durable = 1; snprintf(j->a, sizeof j->a, "%s", category); j->score = score; if (meta) snprintf(j->b, sizeof j->b, "%s", meta); enqueue(j); }
void aeon_leaderboard_get(const char* category, int n, int friends, AeonLbCallback cb, void* user) { if (!category) return; Job* j = calloc(1, sizeof *j); j->kind = J_LB_GET; snprintf(j->a, sizeof j->a, "%s", category); j->n = n; j->friends = friends; j->lb_cb = cb; j->user = user; enqueue(j); }
int  aeon_owns(const char* sku) { if (!sku) return 0; if (!g_on) return 1; /* off-Aeon: everything is owned, so a dev build shows all content */ for (int i = 0; i < g_owned_n; i++) if (strcmp(g_owned[i], sku) == 0) return 1; return 0; }
void aeon_presence(const char* status) { if (!status) return; Job* j = calloc(1, sizeof *j); j->kind = J_PRESENCE; snprintf(j->a, sizeof j->a, "%s", status); enqueue(j); }
void aeon_purchase(const char* sku, AeonPurchaseCallback cb, void* user) { if (!sku) return; Job* j = calloc(1, sizeof *j); j->kind = J_PURCHASE; snprintf(j->a, sizeof j->a, "%s", sku); j->p_cb = cb; j->user = user; enqueue(j); }
void aeon_event(const char* name, const char* payload) { if (!name) return; Job* j = calloc(1, sizeof *j); j->kind = J_EVENT; j->durable = 1; snprintf(j->a, sizeof j->a, "%s", name); if (payload) snprintf(j->b, sizeof j->b, "%s", payload); enqueue(j); }
