/* aeon.h -- the Aeon SDK, C99 ABI 1.0.
 *
 * Link libaeon, call aeon_init() once, aeon_tick() every frame, aeon_shutdown() at exit.
 * Off-Aeon (no AEON_SDK in the environment) every call succeeds silently and aeon_player()
 * returns a stub, so a game runs unchanged on a developer's desktop.
 * Reports (unlocks, progress, scores, presence, events) are queued and delivered in the
 * background; anything that fails for lack of a network is kept on disk and retried, including
 * on the next launch. Nothing here blocks the calling thread except aeon_init() (bounded, ~2s).
 */
#ifndef AEON_SDK_H
#define AEON_SDK_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define AEON_SDK_VERSION "1.0.0"

typedef struct AeonPlayer {
    char id[64];
    char gamertag[64];
    char lang[8];
    int  on_aeon;          /* 0 when running outside Aeon */
} AeonPlayer;

typedef struct AeonLbEntry { int rank; char gamertag[64]; int64_t score; int me; } AeonLbEntry;
typedef struct AeonLeaderboard { int count; AeonLbEntry entries[100]; int my_rank; int64_t my_score; int has_me; } AeonLeaderboard;

typedef void (*AeonLbCallback)(const AeonLeaderboard* lb, void* user);
typedef void (*AeonPurchaseCallback)(const char* sku, int completed, void* user);

int         aeon_init(void);                     /* 1 on Aeon, 0 off-Aeon (calls become no-ops), -1 on error */
void        aeon_tick(void);                     /* pump callbacks + the offline queue; call once per frame */
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

#ifdef __cplusplus
}
#endif
#endif
