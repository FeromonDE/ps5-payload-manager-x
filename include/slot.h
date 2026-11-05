#ifndef SLOT_H
#define SLOT_H

#include <stddef.h>

#define PAYLOAD_SLOT_SCHEMA 2
#define SLOT_KIND_REPO "repo"
#define SLOT_KIND_LOCAL "local"

typedef struct SlotMeta {
    int schema;
    char kind[16];                /* "repo" or "local" */
    char slot_key[128];           /* "repo/<id>@<srchash8>" or "local/<key>" */
    char slot_dir[512];           /* absolute directory path */
    char payload_path[512];       /* absolute path to payload ELF/BIN */
    char filename[256];           /* payload filename in slot */
    char installed_at[64];        /* ISO timestamp */

    /* kind == "repo" */
    char id[128];
    char source_url[1024];
    char source_name[256];
    char name[256];
    char version[64];
    char checksum[65];
    char last_update[64];
    char url[1024];
    char source[1024];
    char source_direct[1024];
    char category[128];
    char description[1024];
    char author[128];
    char details_url[1024];

    /* kind == "local" */
    char origin[64];              /* "web_upload" or "usb" */
    char origin_detail[1024];
} SlotMeta;

/* Serialized payload + metadata publication; source remains caller-owned.
 * new rejects existing keys, replace preserves old on failure, keep_both
 * selects a unique suffix. out_key must have at least 128 bytes. */
int slot_mode_valid(const char *mode);
int slot_commit_file(const char *base_key, const char *source, const char *mode,
                     SlotMeta *meta, int check_repo_conflict, char *out_key, size_t out_size);
/* Recover interrupted commits before migration or serving requests. */
int slot_storage_recover(void);

/* Compute 8-hex-character hash of normalized source URL */
int slot_source_hash(const char *source_url, char *out_hash8, size_t out_size);

/* Convert string to clean slug ([a-z0-9._-]) */
int slot_slug(const char *input, char *out_slug, size_t out_size);

/* Compute local key from filename (strip ext, version marker, ps4/5, slug) */
int slot_local_key(const char *filename, char *out_key, size_t out_size);

/* Format repo slot key: repo/<id>@<h> */
int slot_repo_key(const char *id, const char *source_url, char *out_slot, size_t out_size);

/* Find next available local slot key ("local/<key>", "local/<key>-2", etc.) */
int slot_find_next_local_key(const char *base_key, char *out_slot, size_t out_size);

/* Read meta.json from a slot directory */
int slot_read_meta(const char *slot_dir, SlotMeta *out_meta);

/* Write meta.json to a slot directory */
int slot_write_meta(const char *slot_dir, const SlotMeta *meta);

/* Enumerate all installed slots across repo/ and local/ */
int slot_list_all(SlotMeta **out_slots, size_t *out_count);

/* Free slots array returned by slot_list_all */
void slot_free_all(SlotMeta *slots);

/* Shared HTTP/autoload loader gate. Open rejects substituted symlink components. */
int slot_validate_payload_path(const char *path, char *out_path, size_t out_size);
int slot_open_payload(const char *path, char *out_path, size_t out_size);
/* Internal startup handoff only; accepts direct files in legacy update directories. */
int slot_open_self_update(const char *path);

/* Resolve a slot key or path to absolute payload file path */
int slot_resolve(const char *slot_or_path, char *out_path, size_t out_size);

/* Delete an entire slot directory and remove from autoload */
int slot_delete(const char *slot_key);

/* Strict version comparison (implements section 4) */
int slot_version_compare(const char *v1, const char *v2);

/* One-time migration on startup (payloads/.layout = 2) */
int slot_migrate_legacy(void);

/* Homebrew files are homebrew, not payloads. slot_install_homebrew copies the
 * file verbatim into /data/homebrew (flat), consuming `source` on success. A
 * .zip (slot_is_homebrew_zip_name) is EXTRACTED instead — slot_install_homebrew_zip
 * unpacks it into /data/homebrew with zip-slip protection and records a marker. */
int slot_is_homebrew_name(const char *path);
int slot_is_homebrew_zip_name(const char *path);
int slot_install_homebrew(const char *filename, const char *source,
                          char *msg_buf, size_t msg_buf_size);
int slot_install_homebrew_zip(const char *filename, const char *source,
                              char *msg_buf, size_t msg_buf_size);
/* {"homebrew":[...]} list of installed homebrew (files in /data/homebrew plus
 * extracted-.zip markers) for UI installed-state. */
int slot_homebrew_list_json(char *buf, size_t size);

/* Retrieve a complete valid legacy paths map and clear its file on success.
 * Returns -1 if unavailable, -2 if invalid/too large (file retained). */
int slot_pop_legacy_paths_json(char *buf, size_t buf_size);

#endif
