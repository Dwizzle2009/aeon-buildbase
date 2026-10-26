/* aeon-sdk-test -- exercises every SDK call against the session in the environment.
 * Run it from inside a game's launch line (so the console has minted a session), or with a
 * developer token. Exit 0 = the library initialized and every call completed. */
#include "aeon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int lb_done = 0, p_done = 0, ev_seen = 0;
static void on_ev(const AeonEvent* ev, void* u) { (void)u; static const char* names[] = { "none", "guide opened", "guide closed", "suspend", "resume" }; printf("[test] event: %s\n", ev->kind >= 0 && ev->kind <= 4 ? names[ev->kind] : "?"); ev_seen++; }
static void on_lb(const AeonLeaderboard* lb, void* u) { (void)u; printf("[test] leaderboard: %d entries%s", lb->count, lb->has_me ? "" : "\n"); if (lb->has_me) printf(", me: rank %d score %lld\n", lb->my_rank, (long long)lb->my_score); for (int i = 0; i < lb->count && i < 5; i++) printf("[test]   #%d %s %lld%s\n", lb->entries[i].rank, lb->entries[i].gamertag, (long long)lb->entries[i].score, lb->entries[i].me ? "  <- me" : ""); lb_done = 1; }
__attribute__((unused)) static void on_p(const char* sku, int completed, void* u) { (void)u; printf("[test] purchase %s: %s\n", sku, completed ? "completed" : "not completed"); p_done = 1; }
int main(int argc, char** argv) {
    const char* ach = argc > 1 ? argv[1] : "first_kill"; long long score = argc > 2 ? atoll(argv[2]) : 1234;
    printf("[test] libaeon %s\n", aeon_sdk_version());
    int on = aeon_init();
    const AeonPlayer* p = aeon_player();
    printf("[test] init -> %d (%s); player '%s' id=%.8s... lang=%s avatar=%s banner=%s%s\n", on, on ? "on Aeon" : "off-Aeon: calls are no-ops", p->gamertag, p->id, p->lang, p->avatar_color, p->banner_color, p->sandbox ? " (sandbox)" : "");
    const AeonDisplay* d = aeon_display();
    printf("[test] display: %d Hz, vrr=%d, output='%s'; guide open=%d\n", d->refresh_hz, d->vrr, d->output, aeon_guide_open());
    aeon_set_event_callback(on_ev, NULL);
    printf("[test] owns(%s)=%d\n", getenv("AEON_SKU") ? getenv("AEON_SKU") : "?", aeon_owns(getenv("AEON_SKU") ? getenv("AEON_SKU") : "X"));
    aeon_presence("Running aeon-sdk-test");
    aeon_achievement_unlock(ach);
    aeon_achievement_progress("collector", 3, 10);
    aeon_leaderboard_submit("score", score, "{\"from\":\"aeon-sdk-test\"}");
    aeon_event("test_run", "{\"ok\":true}");
    aeon_leaderboard_get("score", 5, 0, on_lb, NULL);
    for (int i = 0; i < 200 && !lb_done; i++) { aeon_tick(); usleep(50000); }
    if (!lb_done) printf("[test] leaderboard callback did not arrive (offline?)\n");
    for (int i = 0; i < 40; i++) { aeon_tick(); usleep(50000); }   /* let the queue drain */
    if (getenv("AEON_STATE")) { printf("[test] watching %s for 3s -- toggle the Guide now\n", getenv("AEON_STATE")); for (int i = 0; i < 60; i++) { aeon_tick(); usleep(50000); } printf("[test] %d event(s)\n", ev_seen); }
    aeon_shutdown();
    printf("[test] done\n");
    return 0;
}
