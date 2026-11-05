#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#include "profiles.h"
#include "pldmgr.h"
#include "config.h"
#include "json_helpers.h"
#include "utils.h"

/* ── Helpers ───────────────────────────────────────────────── */

/* Extract a JSON boolean value ("true"/"false") for key within [start, end). */
static int json_extract_bool(const char *start, const char *end,
                             const char *key, int fallback) {
    char pattern[96];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(start, pattern);
    if (!p || p >= end) return fallback;
    const char *colon = strchr(p + strlen(pattern), ':');
    if (!colon || colon >= end) return fallback;
    const char *v = colon + 1;
    while (v < end && isspace((unsigned char)*v)) v++;
    if (v + 4 <= end && strncmp(v, "true", 4) == 0) return 1;
    if (v + 5 <= end && strncmp(v, "false", 5) == 0) return 0;
    return fallback;
}

/* Sanitize an id in place: keep [A-Za-z0-9_-], replace others with '_'. */
static void sanitize_id(char *id) {
    for (char *c = id; *c; c++) {
        if (!(isalnum((unsigned char)*c) || *c == '_' || *c == '-'))
            *c = '_';
    }
    if (id[0] == '\0') strcpy(id, "profile");
}

/* Enforce "at most one enabled": keep the first enabled, disable the rest. */
static void enforce_single_enabled(ProfileEntry *arr, int count) {
    int seen = 0;
    for (int i = 0; i < count; i++) {
        if (arr[i].enabled) {
            if (seen) arr[i].enabled = 0;
            else seen = 1;
        }
    }
}

/* ── Parse ─────────────────────────────────────────────────── */

static int parse_profiles(const char *json, ProfileEntry *out, int *count) {
    int c = 0;
    memset(out, 0, sizeof(ProfileEntry) * MAX_PROFILES);

    const char *arr = strstr(json, "\"profiles\"");
    if (arr) arr = strchr(arr, '[');
    if (!arr) { *count = 0; return 0; }

    const char *p = arr + 1;
    while (c < MAX_PROFILES && (p = strchr(p, '{')) != NULL) {
        const char *brace = strchr(p, '}');
        if (!brace) break;
        /* json_extract() is a strict object parser: its [start, end) range must
         * INCLUDE the closing brace (it only accepts '}' while p < end), so end
         * is one past the brace. Passing the brace position itself made every
         * field return JSON_STRING_INVALID and silently dropped every profile. */
        const char *end = brace + 1;

        char id[64] = "", name[128] = "", list[PROFILE_LIST_MAX] = "";
        json_extract_string(p, end, "id",   id,   sizeof(id));
        json_extract_string(p, end, "name", name, sizeof(name));
        json_extract_string(p, end, "list", list, sizeof(list));
        int enabled = json_extract_bool(p, end, "enabled", 0);

        if (id[0]) {
            sanitize_id(id);
            strncpy(out[c].id,   id,   sizeof(out[c].id) - 1);
            strncpy(out[c].name, name[0] ? name : id, sizeof(out[c].name) - 1);
            strncpy(out[c].list, list, sizeof(out[c].list) - 1);
            out[c].enabled = enabled;
            c++;
        }
        p = end;
    }

    enforce_single_enabled(out, c);
    *count = c;
    return 0;
}

/* ── Serialize ─────────────────────────────────────────────── */

static int build_json(const ProfileEntry *arr, int count, char *buf, size_t size) {
    JsonListBuilder jb = { buf, size, 0, 1, 0 };
    buf[0] = '\0';

    json_append(&jb, "{\"profiles\":[\n");
    for (int i = 0; i < count; i++) {
        char id_e[128], name_e[256], list_e[PROFILE_LIST_MAX * 2];
        json_escape_checked(&jb, arr[i].id,   id_e,   sizeof(id_e));
        json_escape_checked(&jb, arr[i].name, name_e, sizeof(name_e));
        json_escape_checked(&jb, arr[i].list, list_e, sizeof(list_e));
        if (json_append(&jb,
                "  {\"id\":\"%s\",\"name\":\"%s\",\"enabled\":%s,\"list\":\"%s\"}%s\n",
                id_e, name_e, arr[i].enabled ? "true" : "false", list_e,
                (i < count - 1) ? "," : "") != 0) {
            break;
        }
    }
    json_append(&jb, "]}\n");
    return (int)jb.pos;
}

static int save_profiles(const ProfileEntry *arr, int count) {
    size_t cap = 262144; /* 256 KB is ample for MAX_PROFILES */
    char *buf = malloc(cap);
    if (!buf) return -1;
    ensure_dir_recursive(BASE_DATA_DIR);
    int len = build_json(arr, count, buf, cap);
    int rc = write_file_text(PROFILES_CONFIG_PATH, buf, (size_t)len);
    free(buf);
    return rc;
}

/* ── Public API ────────────────────────────────────────────── */

int profiles_load(ProfileEntry *out, int *count) {
    char *json = NULL;
    size_t size = 0;

    if (read_file_text(PROFILES_CONFIG_PATH, &json, &size) == 0 && json && size > 0) {
        parse_profiles(json, out, count);
        free(json);
        return 0;
    }
    if (json) free(json);

    /* No profiles file yet: start empty, with no filesystem side-effect. The
     * default/no-profile autoload sequence lives in autoload.txt (upstream's
     * meaning); always-run startup payloads live in the separate startup list. */
    memset(out, 0, sizeof(ProfileEntry) * MAX_PROFILES);
    *count = 0;
    return 0;
}

int profiles_list_json(char *buf, size_t size) {
    ProfileEntry *arr = calloc(MAX_PROFILES, sizeof(ProfileEntry));
    if (!arr) { snprintf(buf, size, "{\"profiles\":[]}\n"); return -1; }
    int count = 0;
    profiles_load(arr, &count);
    build_json(arr, count, buf, size);
    free(arr);
    return 0;
}

int profiles_save_raw(const char *json, size_t len) {
    (void)len;
    if (!json) return -1;
    ProfileEntry *arr = calloc(MAX_PROFILES, sizeof(ProfileEntry));
    if (!arr) return -1;
    int count = 0;
    parse_profiles(json, arr, &count);
    int rc = save_profiles(arr, count);
    free(arr);
    return rc;
}

int profiles_active_index(const ProfileEntry *arr, int count) {
    for (int i = 0; i < count; i++)
        if (arr[i].enabled) return i;
    return -1;
}

int profiles_find_by_id(const ProfileEntry *arr, int count, const char *id) {
    if (!id) return -1;
    for (int i = 0; i < count; i++)
        if (strcmp(arr[i].id, id) == 0) return i;
    return -1;
}

int profiles_write_sequence(const char *list) {
    ensure_dir_recursive(BASE_DATA_DIR);
    FILE *f = fopen(PROFILE_SCRATCH_PATH, "w");
    if (!f) return -1;

    /* Copy so strtok can mutate. */
    char *copy = strdup(list ? list : "");
    if (!copy) { fclose(f); return -1; }
    char *tok = strtok(copy, ",");
    while (tok) {
        /* Trim leading/trailing spaces. */
        while (*tok == ' ') tok++;
        char *e = tok + strlen(tok);
        while (e > tok && (e[-1] == ' ' || e[-1] == '\r' || e[-1] == '\n')) *--e = '\0';
        if (*tok) fprintf(f, "%s\n", tok);
        tok = strtok(NULL, ",");
    }
    free(copy);
    fclose(f);
    return 0;
}

void profiles_propagate_payload_change(const char *old_filename,
                                       const char *new_filename) {
    ProfileEntry *arr = calloc(MAX_PROFILES, sizeof(ProfileEntry));
    if (!arr) return;
    int count = 0;
    profiles_load(arr, &count);

    int modified = 0;
    for (int i = 0; i < count; i++) {
        char rebuilt[PROFILE_LIST_MAX] = "";
        char *copy = strdup(arr[i].list);
        if (!copy) continue;
        int first = 1, changed = 0;
        char *tok = strtok(copy, ",");
        while (tok) {
            const char *emit = tok;
            if (strcmp(tok, old_filename) == 0) {
                changed = 1;
                if (!new_filename) { emit = NULL; }       /* delete entry */
                else emit = new_filename;                 /* rename entry */
            }
            if (emit) {
                if (!first) strncat(rebuilt, ",", sizeof(rebuilt) - strlen(rebuilt) - 1);
                strncat(rebuilt, emit, sizeof(rebuilt) - strlen(rebuilt) - 1);
                first = 0;
            }
            tok = strtok(NULL, ",");
        }
        free(copy);
        if (changed) {
            strncpy(arr[i].list, rebuilt, sizeof(arr[i].list) - 1);
            arr[i].list[sizeof(arr[i].list) - 1] = '\0';
            modified = 1;
        }
    }

    if (modified) {
        save_profiles(arr, count);
        pldmgr_log("[Profiles] Propagated payload change for %s -> %s\n",
                   old_filename, new_filename ? new_filename : "(deleted)");
    }
    free(arr);
}
