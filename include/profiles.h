#pragma once

#include <stddef.h>

/*
 * Autoload Profiles
 * -----------------
 * A "profile" is a named autoload sequence (the same payload/delay entry format
 * used by the legacy single autoload sequence). Profiles are stored as a single
 * JSON document at PROFILES_CONFIG_PATH, fully managed by the frontend.
 *
 * At most one profile may be "enabled" (radio semantics). The enabled profile is
 * the one that auto-runs on boot. If no profile is enabled the manager shows a
 * startup picker instead of running anything.
 *
 * The sequence itself is stored as a single comma-separated "list" string, using
 * the same encoding as the legacy AUTOLOAD_LIST (payload filename per entry, and
 * "!<ms>" for a delay entry).
 */

#define MAX_PROFILES 32
#define PROFILE_LIST_MAX 4096

typedef struct {
    char id[64];
    char name[128];
    int  enabled;
    char list[PROFILE_LIST_MAX]; /* comma-separated entries */
} ProfileEntry;

/* Load all profiles into a caller-provided array of size MAX_PROFILES.
 * Performs a one-time migration from the legacy autoload.txt if the profiles
 * file does not yet exist. Enforces the "at most one enabled" invariant.
 * Returns 0 on success; *count receives the number of profiles loaded. */
int profiles_load(ProfileEntry *out, int *count);

/* Build a normalized {"profiles":[...]} JSON document into buf (for /profiles_get). */
int profiles_list_json(char *buf, size_t size);

/* Parse a raw JSON document from the frontend and persist it (for /profiles_set).
 * Normalizes and enforces the single-enabled invariant. Returns 0 on success. */
int profiles_save_raw(const char *json, size_t len);

/* Index of the first enabled profile, or -1 if none. */
int profiles_active_index(const ProfileEntry *arr, int count);

/* Index of the profile with the given id, or -1 if not found. */
int profiles_find_by_id(const ProfileEntry *arr, int count, const char *id);

/* Write a comma-separated list string out as the resolved autoload.txt scratch
 * file (one entry per line) that the autoload worker executes. Returns 0 on ok. */
int profiles_write_sequence(const char *list);

/* Propagate a payload rename (new_filename != NULL) or delete (new_filename ==
 * NULL) across every profile's sequence, rewriting the profiles file if changed. */
void profiles_propagate_payload_change(const char *old_filename,
                                       const char *new_filename);
