/* Host test: profiles save/load round-trip through the real backend code.
 * Guards the parse_profiles off-by-one that silently dropped every profile
 * (json_extract needs its [start,end) range to include the closing brace). */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "profiles.h"

/* Stubs for symbols the linked backend objects reference but this test does not
 * exercise. */
void pldmgr_log(const char *fmt, ...) { (void)fmt; }
void pldmgr_autoload_abort(void) {}

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

int main(void) {
    /* The exact shape App.saveProfiles POSTs: one object per profile, `list`
     * a comma string, plus an empty-list profile. */
    const char *js =
        "{\"profiles\":["
        "{\"id\":\"profile_1\",\"name\":\"Alpha\",\"enabled\":true,\"list\":\"a.elf,b.elf\"},"
        "{\"id\":\"profile_2\",\"name\":\"Beta\",\"enabled\":false,\"list\":\"\"}"
        "]}";

    CHECK(profiles_save_raw(js, strlen(js)) == 0, "save_raw returns 0");

    ProfileEntry arr[MAX_PROFILES];
    int count = -1;
    CHECK(profiles_load(arr, &count) == 0, "load returns 0");
    CHECK(count == 2, "two profiles survive the round-trip (not silently dropped)");

    if (count == 2) {
        CHECK(strcmp(arr[0].id, "profile_1") == 0, "profile 0 id");
        CHECK(strcmp(arr[0].name, "Alpha") == 0, "profile 0 name");
        CHECK(arr[0].enabled == 1, "profile 0 enabled");
        CHECK(strcmp(arr[0].list, "a.elf,b.elf") == 0, "profile 0 list keeps commas");
        CHECK(strcmp(arr[1].id, "profile_2") == 0, "profile 1 id");
        CHECK(arr[1].enabled == 0, "profile 1 disabled");
        CHECK(arr[1].list[0] == '\0', "profile 1 empty list");
    }

    /* "At most one enabled" is enforced: two enabled collapse to the first. */
    const char *two_enabled =
        "{\"profiles\":["
        "{\"id\":\"x\",\"name\":\"X\",\"enabled\":true,\"list\":\"a.elf\"},"
        "{\"id\":\"y\",\"name\":\"Y\",\"enabled\":true,\"list\":\"b.elf\"}"
        "]}";
    CHECK(profiles_save_raw(two_enabled, strlen(two_enabled)) == 0, "save two-enabled");
    count = -1;
    profiles_load(arr, &count);
    CHECK(count == 2, "two-enabled round-trips both rows");
    if (count == 2) {
        int enabled_total = arr[0].enabled + arr[1].enabled;
        CHECK(enabled_total == 1, "exactly one enabled after enforce_single_enabled");
        CHECK(arr[0].enabled == 1, "the first enabled wins");
    }

    if (failures == 0) printf("Profiles round-trip: save/load preserves id/name/enabled/list; single-enabled enforced\n");
    return failures ? 1 : 0;
}
