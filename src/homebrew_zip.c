/* Homebrew .zip install: a .zip in the catalog is a container for a raw game /
 * homebrew FOLDER dump that the console consumes. Unlike .exfat/.ffpfsc (copied
 * verbatim), a .zip must be EXTRACTED into /data/homebrew so the console sees the
 * folder. Extraction is streamed to disk (never the whole archive in memory) and
 * every entry path is validated against zip-slip before a single byte is written. */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "miniz.h"

#include "pldmgr.h"
#include "slot.h"
#include "json_helpers.h"

/* Generous ceiling on total uncompressed output so a decompression bomb cannot
 * silently fill console storage. Legit game folders are large but well under this. */
#define HOMEBREW_ZIP_MAX_TOTAL_BYTES (40ULL * 1024 * 1024 * 1024)

/* A zip entry name is safe to join under HOMEBREW_DIR iff it is a relative path
 * that cannot escape it: not empty, not absolute, no "..", no backslash (Windows
 * separators) and no drive/volume ':'. Returns 1 if safe. */
static int zip_entry_name_safe(const char *name) {
    if (!name || !name[0]) return 0;
    if (name[0] == '/' || name[0] == '\\') return 0;          /* absolute */
    if (strstr(name, "..")) return 0;                          /* parent escape */
    for (const char *c = name; *c; c++) {
        if (*c == '\\' || *c == ':') return 0;                 /* win sep / drive */
    }
    return 1;
}

/* Create every parent directory of `path` (path itself is a file). */
static int ensure_parent_dirs(const char *path) {
    char parent[1024];
    snprintf(parent, sizeof(parent), "%s", path);
    char *slash = strrchr(parent, '/');
    if (!slash) return 0;
    *slash = '\0';
    if (!parent[0]) return 0;
    return ensure_dir_recursive(parent);
}

/* Record a zero-byte marker named after the catalog filename so the UI can show
 * the extracted .zip as installed (the .zip itself is not kept). */
static void write_zip_marker(const char *filename) {
    if (!filename || !filename[0] || strchr(filename, '/') || strstr(filename, "..")) return;
    if (ensure_dir_recursive(HOMEBREW_ZIP_MARKER_DIR) != 0) return;
    char marker[1024];
    if (snprintf(marker, sizeof(marker), "%s/%s", HOMEBREW_ZIP_MARKER_DIR, filename) >= (int)sizeof(marker))
        return;
    FILE *f = fopen(marker, "w");
    if (f) fclose(f);
}

int slot_install_homebrew_zip(const char *filename, const char *source_zip,
                              char *msg_buf, size_t msg_buf_size) {
    if (!source_zip) { snprintf(msg_buf, msg_buf_size, "No archive"); return -1; }
    if (ensure_dir_recursive(HOMEBREW_DIR) != 0) {
        snprintf(msg_buf, msg_buf_size, "Failed to create " HOMEBREW_DIR);
        return -1;
    }

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, source_zip, 0)) {
        snprintf(msg_buf, msg_buf_size, "Not a valid .zip archive");
        return -1;
    }

    mz_uint count = mz_zip_reader_get_num_files(&zip);
    unsigned long long total_bytes = 0;
    int extracted = 0, failed = 0;

    for (mz_uint i = 0; i < count; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) { failed = 1; break; }

        if (!zip_entry_name_safe(st.m_filename)) {
            /* Reject the whole install rather than silently skip a hostile entry. */
            pldmgr_log("[PLDMGR] Rejected unsafe zip entry: %s\n", st.m_filename);
            failed = 1; break;
        }

        char dest[1024];
        if (snprintf(dest, sizeof(dest), "%s/%s", HOMEBREW_DIR, st.m_filename) >= (int)sizeof(dest)) {
            failed = 1; break;
        }

        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            if (ensure_dir_recursive(dest) != 0) { failed = 1; break; }
            continue;
        }

        total_bytes += st.m_uncomp_size;
        if (total_bytes > HOMEBREW_ZIP_MAX_TOTAL_BYTES) {
            pldmgr_log("[PLDMGR] Aborting zip extract: exceeds size ceiling\n");
            failed = 1; break;
        }

        if (ensure_parent_dirs(dest) != 0) { failed = 1; break; }
        if (!mz_zip_reader_extract_to_file(&zip, i, dest, 0)) { failed = 1; break; }
        extracted++;
    }

    mz_zip_reader_end(&zip);

    if (failed) {
        snprintf(msg_buf, msg_buf_size, "Extraction failed");
        return -1;
    }
    write_zip_marker(filename);
    remove(source_zip); /* consume the downloaded archive; the extracted files are what matters */
    pldmgr_log("[PLDMGR] Homebrew zip extracted: %d files from %s -> " HOMEBREW_DIR "\n",
               extracted, filename ? filename : source_zip);
    snprintf(msg_buf, msg_buf_size, "Extracted %d file%s to homebrew",
             extracted, extracted == 1 ? "" : "s");
    return 0;
}
