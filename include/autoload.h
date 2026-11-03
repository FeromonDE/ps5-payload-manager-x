#pragma once
#include <stddef.h>

typedef enum {
    AUTOLOAD_DISABLED, AUTOLOAD_WAITING, AUTOLOAD_COUNTDOWN, AUTOLOAD_EXECUTING,
    AUTOLOAD_DONE, AUTOLOAD_ABORTED, AUTOLOAD_FAILED
} AutoloadState;

typedef struct {
    AutoloadState state;
    int remaining, total, done, successful, failed, skipped;
    int delay, scan_usb_payloads;
    long long remaining_ms;
    int entry_index, entry_duration_ms;
    long long entry_remaining_ms;
    char current[128];
    char list[4096];
    char outcomes[4096]; /* Per payload: p=pending, s=success, f=failed, k=skipped. */
} AutoloadSnapshot;

/* One run per start/stop lifecycle. Repeated start is idempotent, even after
 * completion/reset. Stop cooperatively cancels and joins the owned worker.
 * Never call stop from the worker or a callback executing inside it. */
int pldmgr_autoload_start(void);
void pldmgr_autoload_stop(void);
void pldmgr_autoload_abort(void);
/* Ignore reset while waiting/counting/executing; clear terminal display only. */
void pldmgr_autoload_reset(void);
/* Pure readers. Signal readiness separately after authorizing frontend access. */
void pldmgr_autoload_frontend_ready(void);
void pldmgr_autoload_get_snapshot(AutoloadSnapshot *out);
const char *pldmgr_autoload_state_name(AutoloadState state);
int pldmgr_autoload_get_remaining_seconds(void);
long long pldmgr_autoload_get_remaining_ms(void);
void pldmgr_autoload_get_status(int *total, int *done, char *current);
int pldmgr_autoload_update_config_entry(const char *old_filename, const char *new_filename);

/* ── Fork: autoload profiles ────────────────────────────────────────────────
 * A profile is a named autoload sequence (profiles.json). At most one may be
 * "enabled" and auto-runs on boot; if profiles exist but none is enabled the
 * manager boots into "picker" mode so the frontend shows a startup selection
 * instead of auto-running anything. Built on upstream's worker: the resolved
 * profile sequence is executed through the same countdown/launch machinery. */

/* 1 when booted into picker mode (profiles present, none enabled). Cleared once
 * a profile is run or the selection is dismissed. */
int pldmgr_autoload_is_picker(void);

/* Run a specific profile immediately (no startup countdown), regardless of which
 * profile is enabled. Used when the user picks one from the startup selection.
 * Returns 0 on success, -1 if the profile id is unknown. */
int pldmgr_autoload_run_profile(const char *id);

/* Comma-separated list of payloads for the overlay's current phase. */
void pldmgr_autoload_get_list(char *buf, size_t size);

/* ── Fork: always-run startup payloads (startup.txt) ────────────────────────
 * Launched silently on every boot, independent of the active/selected profile. */
int pldmgr_startup_is_enabled(const char *filename);
void pldmgr_startup_set(const char *filename, int enabled);
int pldmgr_startup_list_json(char *buf, size_t size);
