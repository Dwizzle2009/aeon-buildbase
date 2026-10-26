/* aeon.h -- the Aeon SDK, C99 ABI 1.x.
 *
 * Link libaeon, call aeon_init() once, aeon_tick() every frame, aeon_shutdown() at exit.
 * Off-Aeon (no AEON_SDK in the environment) every call succeeds silently and aeon_player()
 * returns a stub, so a game runs unchanged on a developer's desktop.
 * Reports (unlocks, progress, scores, presence, events) are queued and delivered in the
 * background; anything that fails for lack of a network is kept on disk and retried, including
 * on the next launch. Nothing here blocks the calling thread except aeon_init() (bounded, ~2s).
 *
 * 1.1 adds the player's colours for in-game display, console events (the Guide opening and
 * closing, suspend and resume) and the display facts the console launched the game with.
 * The 1.0 ABI is kept: AeonPlayer only grew at the end, and every 1.0 symbol is unchanged.
 */
#ifndef AEON_SDK_H
#define AEON_SDK_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define AEON_SDK_VERSION "1.1.0"

typedef struct AeonPlayer {
    char id[64];
    char gamertag[64];
    char lang[8];          /* the console's UI language: "en", "es", "fr" */
    int  on_aeon;          /* 0 when running outside Aeon */
    /* 1.1 -- appended, so a 1.0 binary keeps reading the fields above */
    char avatar_color[8];  /* "#rrggbb" -- the disc the console draws next to the gamertag */
    char banner_color[8];  /* "#rrggbb" -- the profile's banner */
    char avatar_url[256];  /* "" until the console has picture avatars; draw a disc in avatar_color with the gamertag's first letter */
    int  sandbox;          /* 1 when the session is a developer token (the sandbox player) */
} AeonPlayer;

typedef struct AeonLbEntry { int rank; char gamertag[64]; int64_t score; int me; } AeonLbEntry;
typedef struct AeonLeaderboard { int count; AeonLbEntry entries[100]; int my_rank; int64_t my_score; int has_me; } AeonLeaderboard;

typedef void (*AeonLbCallback)(const AeonLeaderboard* lb, void* user);
typedef void (*AeonPurchaseCallback)(const char* sku, int completed, void* user);

/* 1.1: console events. The console writes them while the game runs; libaeon turns them into
 * events on aeon_tick(), so they arrive on the thread that ticks (the game thread in every engine).
 * Either register a callback or poll -- with a callback set, aeon_poll_event() returns nothing. */
typedef enum AeonEventKind {
    AEON_EV_NONE = 0,
    AEON_EV_GUIDE_OPENED = 1,   /* the Guide now covers the game: pause, mute, stop the clock */
    AEON_EV_GUIDE_CLOSED = 2,   /* back to the game */
    AEON_EV_SUSPEND = 3,        /* the console went to rest. The process was frozen, so this arrives together with RESUME */
    AEON_EV_RESUME = 4          /* the console woke: reset timers, re-check anything time-based */
} AeonEventKind;
typedef struct AeonEvent { int kind; int value; char text[64]; } AeonEvent;   /* value/text: reserved per kind, 0/"" today */
typedef void (*AeonEventCallback)(const AeonEvent* ev, void* user);

/* 1.1: what the console launched the game on. Fixed for the run; a game picks a frame-rate policy from it. */
typedef struct AeonDisplay {
    int  refresh_hz;      /* the output's refresh rate, rounded (60, 120, 144); 0 off-Aeon */
    int  vrr;             /* 1 when variable refresh is live, so an uncapped frame rate stays smooth */
    char output[32];      /* the output mode, "2560x1440"; "" off-Aeon */
} AeonDisplay;

int         aeon_init(void);                     /* 1 on Aeon, 0 off-Aeon (calls become no-ops), -1 on error */
void        aeon_tick(void);                     /* pump callbacks, events and the offline queue; call once per frame */
void        aeon_shutdown(void);
const char* aeon_sdk_version(void);
const AeonPlayer* aeon_player(void);

void aeon_achievement_unlock(const char* id);
void aeon_achievement_progress(const char* id, int value, int max);
void aeon_leaderboard_submit(const char* category, int64_t score, const char* metadata_json /* or NULL */);
void aeon_leaderboard_get(const char* category, int n, int friends, AeonLbCallback cb, void* user);
int  aeon_owns(const char* sku);                 /* 1/0 from cached entitlements; refreshed in the background */
void aeon_presence(const char* status);
void aeon_purchase(const char* sku, AeonPurchaseCallback cb, void* user);
void aeon_event(const char* name, const char* payload_json /* or NULL */);

/* 1.1 */
void aeon_set_event_callback(AeonEventCallback cb, void* user);   /* NULL to go back to polling */
int  aeon_poll_event(AeonEvent* out);                             /* 1 and *out filled, 0 when nothing is queued */
int  aeon_guide_open(void);                                       /* 1 while the Guide covers the game; 0 off-Aeon */
const AeonDisplay* aeon_display(void);                            /* never NULL; zeros off-Aeon */

#ifdef __cplusplus
}
#endif
#endif
