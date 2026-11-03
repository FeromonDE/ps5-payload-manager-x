#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include "pldmgr.h"
#include "autoload.h"
#include "config.h"
#include "json_helpers.h"
#include "slot.h"
#include "ps5_launcher.h"
#include "profiles.h"
#include "utils.h"

#ifndef PLDMGR_FRONTEND_WAIT_MS
#define PLDMGR_FRONTEND_WAIT_MS 5000
#endif
#define MAX_SEQUENCE_TEXT 4095
#define MAX_ENTRY_TEXT 511
#define MAX_ENTRY_DELAY_MS 3600000

/* Ownership lock serializes start/stop/join; worker uses only state_mutex.
 * Never hold state_mutex during join, filesystem access, notifications or launch. */
static pthread_mutex_t lifecycle_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t state_cond = PTHREAD_COND_INITIALIZER;
static pthread_t worker_thread;
static int owned, running, cancel_requested, frontend_ready;
static long long countdown_end, entry_delay_end;
static AutoloadSnapshot status = {.state=AUTOLOAD_DISABLED, .remaining=-1, .remaining_ms=-1000,.entry_index=-1};

typedef struct { char key[MAX_ENTRY_TEXT + 1]; int delay_ms; } SequenceEntry;

/* ── Fork: profiles / picker / startup ───────────────────────────────────────
 * The worker reads its sequence from `sequence_path`. For the no-profile default
 * it stays AUTOLOAD_CONFIG_PATH (upstream's meaning), so upstream behaviour and
 * host tests are unchanged; for a profile it points at the resolved scratch file.
 * `picker_active` means "profiles exist but none enabled" (frontend shows the
 * selection); `forced_run` means run the resolved sequence now with no countdown.
 * launched_bases dedups across startup + sequence so a payload never double-launches. */
static const char *sequence_path = AUTOLOAD_CONFIG_PATH;
/* Atomic: picker_active is read by the HTTP thread (pldmgr_autoload_is_picker,
 * via /autoload_status) while the worker writes it; forced_run crosses the
 * run_profile thread and the worker. sequence_path and launched_* are touched
 * only by the worker thread (run_profile joins it first), so need no atomics. */
static _Atomic int picker_active = 0;
static _Atomic int forced_run = 0;
#define MAX_LAUNCHED 64
static char launched_bases[MAX_LAUNCHED][128];
static int launched_count = 0;
static void launched_reset(void) { launched_count = 0; }
static int launched_has(const char *base) {
    for (int i = 0; i < launched_count; i++)
        if (strcmp(launched_bases[i], base) == 0) return 1;
    return 0;
}
static void launched_add(const char *base) {
    if (launched_count < MAX_LAUNCHED) {
        strncpy(launched_bases[launched_count], base, sizeof(launched_bases[0]) - 1);
        launched_bases[launched_count][sizeof(launched_bases[0]) - 1] = '\0';
        launched_count++;
    }
}

static long long now_ms(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (long long)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}
const char *pldmgr_autoload_state_name(AutoloadState state) {
    const char *names[] = {"disabled", "waiting", "countdown", "executing", "done", "aborted", "failed"};
    return state >= AUTOLOAD_DISABLED && state <= AUTOLOAD_FAILED ? names[state] : "failed";
}
void pldmgr_autoload_get_snapshot(AutoloadSnapshot *out) {
    if (!out) return;
    pthread_mutex_lock(&state_mutex);
    *out = status;
    if (status.state == AUTOLOAD_COUNTDOWN) {
        out->remaining_ms = countdown_end - now_ms();
        if (out->remaining_ms < 0) out->remaining_ms = 0;
        out->remaining = (int)((out->remaining_ms + 999) / 1000);
    }
    if (status.state == AUTOLOAD_EXECUTING && entry_delay_end) {
        out->entry_remaining_ms = entry_delay_end - now_ms();
        if (out->entry_remaining_ms < 0) out->entry_remaining_ms = 0;
    }
    pthread_mutex_unlock(&state_mutex);
}
int pldmgr_autoload_get_remaining_seconds(void) {
    AutoloadSnapshot snapshot; pldmgr_autoload_get_snapshot(&snapshot); return snapshot.remaining;
}
long long pldmgr_autoload_get_remaining_ms(void) {
    AutoloadSnapshot snapshot; pldmgr_autoload_get_snapshot(&snapshot); return snapshot.remaining_ms;
}
void pldmgr_autoload_get_status(int *total, int *done, char *current) {
    AutoloadSnapshot snapshot; pldmgr_autoload_get_snapshot(&snapshot);
    if (total) *total=snapshot.total;
    if (done) *done=snapshot.done;
    if (current) memcpy(current, snapshot.current, sizeof(snapshot.current));
}
void pldmgr_autoload_frontend_ready(void) {
    pthread_mutex_lock(&state_mutex);
    frontend_ready=1;
    pthread_cond_broadcast(&state_cond);
    pthread_mutex_unlock(&state_mutex);
}
static int cancelled(void *context) {
    (void)context;
    pthread_mutex_lock(&state_mutex);
    int result=cancel_requested;
    pthread_mutex_unlock(&state_mutex);
    return result;
}
/* Timed condition waits allow prompt stop without changing countdown deadlines. */
static int wait_until(long long end, int waiting_frontend) {
    pthread_mutex_lock(&state_mutex);
    while (!cancel_requested && !(waiting_frontend && frontend_ready) && now_ms() < end) {
        struct timespec wake;
        clock_gettime(CLOCK_REALTIME, &wake);
        wake.tv_nsec += 100000000;
        if (wake.tv_nsec >= 1000000000) { wake.tv_sec++; wake.tv_nsec -= 1000000000; }
        pthread_cond_timedwait(&state_cond, &state_mutex, &wake);
    }
    int result=cancel_requested;
    pthread_mutex_unlock(&state_mutex);
    return result;
}
/* Parse the exact sequence once. Counters and UI list derive from these entries,
 * not a second pass with different whitespace/delay rules or a newer file. */
static int read_sequence(SequenceEntry **out, size_t *count, char list[4096]) {
    *out=NULL; *count=0; list[0]=0;
    int fd=open(sequence_path,O_RDONLY|O_NONBLOCK);
    if (fd<0) return errno==ENOENT ? 1 : -1;
    struct stat info;
    if (fstat(fd,&info) || !S_ISREG(info.st_mode) || info.st_size<0 || info.st_size>MAX_SEQUENCE_TEXT) {
        close(fd); return -1;
    }
    FILE *file=fdopen(fd,"rb");
    if (!file) { close(fd); return -1; }
    char text[MAX_SEQUENCE_TEXT+1];
    size_t size=fread(text,1,sizeof(text),file);
    int failed=ferror(file);
    if (fclose(file)) failed=1;
    if (failed || size>MAX_SEQUENCE_TEXT || memchr(text,0,size)) return -1;
    text[size]=0;
    size_t capacity=1;
    for (size_t i=0;i<size;i++) if (text[i]=='\n') capacity++;
    SequenceEntry *entries=calloc(capacity, sizeof(*entries));
    if (!entries) return -1;
    char *save=NULL; size_t pos=0;
    for (char *line=strtok_r(text,"\n",&save);line;line=strtok_r(NULL,"\n",&save)) {
        while (*line && isspace((unsigned char)*line)) line++;
        char *end=line+strlen(line);
        while (end>line && isspace((unsigned char)end[-1])) *--end=0;
        size_t length=strlen(line);
        if (!length) continue;
        if (length > MAX_ENTRY_TEXT || strpbrk(line,"\r,")) goto invalid;
        SequenceEntry *entry=&entries[*count];
        entry->delay_ms=-1;
        if (*line=='!') {
            if (length==1) goto invalid;
            long delay=0;
            for (const char *p=line+1;*p;p++) {
                if (*p<'0' || *p>'9' || delay > MAX_ENTRY_DELAY_MS/10) goto invalid;
                delay=delay*10+*p-'0';
                if (delay > MAX_ENTRY_DELAY_MS) goto invalid;
            }
            entry->delay_ms=(int)delay;
        }
        size_t needed=length+(pos?1:0);
        if (needed >= 4096-pos) goto invalid;
        if (pos) list[pos++]=',';
        memcpy(list+pos,line,length); pos+=length; list[pos]=0;
        memcpy(entry->key,line,length+1);
        (*count)++;
    }
    *out=entries; return 0;
invalid:
    free(entries); *count=0; list[0]=0; return -1;
}
static void set_current(const char *key) {
    size_t length=strlen(key);
    if (length>=sizeof(status.current)) {
        length=sizeof(status.current)-1;
        while (length && ((unsigned char)key[length]&0xc0)==0x80) length--;
    }
    memcpy(status.current,key,length); status.current[length]=0;
}
static void finish(AutoloadState state) {
    pthread_mutex_lock(&state_mutex);
    if (entry_delay_end) {
        status.entry_remaining_ms=entry_delay_end-now_ms();
        if (status.entry_remaining_ms<0) status.entry_remaining_ms=0;
    }
    entry_delay_end=0;
    status.state=cancel_requested ? AUTOLOAD_ABORTED : state;
    status.remaining=status.state==AUTOLOAD_DONE ? 0 : -1;
    status.remaining_ms=(long long)status.remaining*1000;
    snprintf(status.current,sizeof(status.current),"%s",status.state==AUTOLOAD_DONE ? "DONE" : "");
    countdown_end=0;
    running=0;
    pthread_cond_broadcast(&state_cond);
    pthread_mutex_unlock(&state_mutex);
}
/* Launch the always-run startup payloads (startup.txt) silently in the
 * background, honouring "!ms" delays and skipping duplicates. They are not shown
 * as an autoload sequence; they run before any profile/countdown. Abortable. */
static void launch_startup_payloads(void) {
    FILE *f = fopen(STARTUP_LIST_PATH, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '\0') continue;
        if (line[0] == '!') {
            int delay = atoi(line + 1);
            if (delay > 0) {
                pldmgr_log("[Autoload] Startup delay %d ms...\n", delay);
                if (wait_until(now_ms() + delay, 0)) break;
            }
            continue;
        }
        if (cancelled(NULL)) break;
        if (is_pldmgr_name(line)) continue;
        char base[128];
        pldmgr_utils_get_payload_folder_name(line, base, sizeof(base));
        if (launched_has(base)) { pldmgr_log("[Autoload] Startup: already launched %s\n", line); continue; }
        char path[512];
        if (slot_resolve(line, path, sizeof(path)) == 0 && !is_pldmgr_name(path)) {
            pldmgr_log("[Autoload] Launching (startup): %s\n", path);
            ps5_launch_elf_cancellable(path, cancelled, NULL);
            launched_add(base);
            if (wait_until(now_ms() + 500, 0)) break; /* launch spacing; abortable */
        } else {
            pldmgr_log("[Autoload] Startup payload not found: %s\n", line);
        }
    }
    fclose(f);
}

static void *autoload_worker(void *argument) {
    (void)argument;
    SequenceEntry *entries=NULL; size_t count=0;
    char list[4096]={0};
    PldmgrConfig cfg; config_read(&cfg);
    if (!cfg.autoload_enabled) { finish(AUTOLOAD_DISABLED); return NULL; }

    /* ── Fork: resolve what to run (manual profile / enabled profile / picker /
     * upstream default), then launch always-run startup payloads. The no-profile,
     * no-startup, non-forced path leaves sequence_path = AUTOLOAD_CONFIG_PATH and
     * launches nothing, so it is byte-identical to upstream. ─────────────────── */
    launched_reset();
    int forced = forced_run;
    forced_run = 0;
    if (forced) {
        /* run_profile() already wrote PROFILE_SCRATCH_PATH; run it now with no
         * countdown and no browser wait. Startup payloads already ran at boot. */
        sequence_path = PROFILE_SCRATCH_PATH;
        cfg.autoload_delay = 0;
        cfg.auto_browser_open = 0;
    } else {
        picker_active = 0;
        ProfileEntry *arr = calloc(MAX_PROFILES, sizeof(ProfileEntry));
        int pcount = 0, active = -1;
        if (arr) { profiles_load(arr, &pcount); active = profiles_active_index(arr, pcount); }
        if (active >= 0) {
            profiles_write_sequence(arr[active].list);
            sequence_path = PROFILE_SCRATCH_PATH;
            pldmgr_log("[Autoload] Enabled profile: %s\n", arr[active].name);
        } else if (pcount > 0) {
            picker_active = 1; /* set before startup so the status poll keeps the picker visible */
            pldmgr_log("[Autoload] No enabled profile - startup picker (%d profiles).\n", pcount);
        } else {
            sequence_path = AUTOLOAD_CONFIG_PATH; /* upstream default sequence */
        }
        if (arr) free(arr);

        launch_startup_payloads();

        /* Picker: do not run a sequence; stay idle so the frontend can offer the
         * selection. run_profile() takes over when the user picks one. */
        if (picker_active) { finish(AUTOLOAD_DISABLED); return NULL; }
    }

    int parsed=read_sequence(&entries,&count,list);
    if (parsed) {
        if (parsed<0) pldmgr_log("[Autoload] Invalid or unreadable sequence; no payloads launched.\n");
        finish(parsed>0 ? AUTOLOAD_DISABLED : AUTOLOAD_FAILED); return NULL;
    }
    pthread_mutex_lock(&state_mutex);
    status.delay=cfg.autoload_delay;
    status.scan_usb_payloads=cfg.scan_usb_payloads;
    memcpy(status.list,list,sizeof(list));
    for (size_t i=0;i<count;i++) if (entries[i].delay_ms<0) status.outcomes[status.total++]='p';
    status.state=AUTOLOAD_WAITING;
    status.remaining=cfg.autoload_delay;
    status.remaining_ms=(long long)cfg.autoload_delay*1000;
    pthread_mutex_unlock(&state_mutex);
    if (cfg.auto_browser_open) {
        pldmgr_log("[Autoload] Browser Mode: Waiting for frontend connection...\n");
        if (wait_until(now_ms()+PLDMGR_FRONTEND_WAIT_MS,1)) goto aborted;
    }
    if (cancelled(NULL)) goto aborted;
    pthread_mutex_lock(&state_mutex);
    status.state=AUTOLOAD_COUNTDOWN;
    countdown_end=now_ms()+(long long)cfg.autoload_delay*1000;
    long long end=countdown_end;
    pthread_mutex_unlock(&state_mutex);

    /* Preserve the existing PS-button mode, prompt/drain ordering and per-read
     * matching. The user explicitly withdrew the separate detection changes. */
    int klog_fd=-1;
    if (cfg.autoload_delay>0 && !cfg.auto_browser_open) {
        if (!pldmgr_server_is_active()) {
            char ip[64];
            if (pldmgr_get_local_ip(ip,sizeof(ip))!=0) strcpy(ip,"0.0.0.0");
            pldmgr_notify("Payload Manager Running\nhttp://%s:%d",ip,MENU_PORT);
            pldmgr_notify("Autoloading in %ds\nPress PS Button to Abort",cfg.autoload_delay);
        }
        klog_fd=open("/dev/klog",O_RDONLY|O_NONBLOCK);
        if (klog_fd>=0) {
            char flush[4096];
            while (read(klog_fd,flush,sizeof(flush))>0) {
                if (cancelled(NULL)) { close(klog_fd); goto aborted; }
            }
        }
        pldmgr_log("[Autoload] Fallback Mode: Starting %ds countdown (PS Button active)...\n",cfg.autoload_delay);
    } else if (cfg.autoload_delay>0) {
        pldmgr_log("[Autoload] Browser Mode: Starting %ds countdown...\n",cfg.autoload_delay);
    }
    while (now_ms()<end) {
        if (cancelled(NULL)) { if(klog_fd>=0)close(klog_fd); goto aborted; }
        if (klog_fd>=0) {
            char log[2048]; ssize_t n=read(klog_fd,log,sizeof(log)-1);
            if (n>0) {
                log[n]=0;
                if (strstr(log,"onPSButtonPressed")) {
                    pldmgr_log("[Autoload] ABORTED via PS Button.\n");
                    pldmgr_notify("Autoload Aborted");
                    pldmgr_autoload_abort(); close(klog_fd); goto aborted;
                }
            }
        }
        long long tick=now_ms()+100;
        if (tick>end) tick=end;
        if (wait_until(tick,0)) { if(klog_fd>=0)close(klog_fd); goto aborted; }
    }
    if (klog_fd>=0) close(klog_fd);
    if (cancelled(NULL)) goto aborted;
    pthread_mutex_lock(&state_mutex);
    status.state=AUTOLOAD_EXECUTING; status.remaining=0; status.remaining_ms=0; countdown_end=0;
    pthread_mutex_unlock(&state_mutex);
    pldmgr_log("[Autoload] Starting sequence...\n");
    int payload_index=0;
    for (size_t i=0;i<count;i++) {
        if (cancelled(NULL)) goto aborted;
        SequenceEntry *entry=&entries[i];
        if (entry->delay_ms>=0) {
            long long deadline=now_ms()+entry->delay_ms;
            pthread_mutex_lock(&state_mutex);
            status.entry_index=(int)i;
            status.entry_duration_ms=entry->delay_ms;
            status.entry_remaining_ms=entry->delay_ms;
            entry_delay_end=deadline;
            snprintf(status.current,sizeof(status.current),"DELAY");
            pthread_mutex_unlock(&state_mutex);
            if (wait_until(deadline,0)) goto aborted;
            pthread_mutex_lock(&state_mutex);
            status.entry_remaining_ms=0; entry_delay_end=0;
            pthread_mutex_unlock(&state_mutex);
            continue;
        }
        pthread_mutex_lock(&state_mutex);
        status.entry_index=(int)i;
        status.entry_duration_ms=0; status.entry_remaining_ms=0; entry_delay_end=0;
        set_current(entry->key);
        pthread_mutex_unlock(&state_mutex);
        int skipped=0, failed=0;
        char path[512];
        char base[128];
        pldmgr_utils_get_payload_folder_name(entry->key, base, sizeof(base));
        if (is_pldmgr_name(entry->key)) skipped=1;
        else if (launched_has(base)) { skipped=1; pldmgr_log("[Autoload] Skipping already-launched: %s\n", entry->key); }
        else if (slot_resolve(entry->key,path,sizeof(path))) failed=1;
        else if (is_pldmgr_name(path)) skipped=1;
        else {
            pldmgr_log("[Autoload] Launching: %s\n",path);
            if (ps5_launch_elf_cancellable(path,cancelled,NULL)) failed=1;
            else launched_add(base);
        }
        if (skipped) pldmgr_log("[Autoload] Skipping self: %s\n",entry->key);
        if (failed) pldmgr_log("[Autoload] Missing, failed or cancelled payload: %s\n",entry->key);
        pthread_mutex_lock(&state_mutex);
        status.done++;
        status.outcomes[payload_index++]=skipped ? 'k' : failed ? 'f' : 's';
        status.skipped+=skipped;
        status.failed+=failed;
        status.successful+=!skipped && !failed;
        pthread_mutex_unlock(&state_mutex);
        if (cancelled(NULL)) goto aborted;
        if (wait_until(now_ms()+500,0)) goto aborted;
    }
    free(entries); finish(AUTOLOAD_DONE);
    pldmgr_log("[Autoload] Sequence complete.\n"); return NULL;
aborted:
    free(entries); finish(AUTOLOAD_ABORTED);
    pldmgr_log("[Autoload] Sequence aborted.\n"); return NULL;
}
int pldmgr_autoload_start(void) {
    pthread_mutex_lock(&lifecycle_mutex);
    if (owned) { pthread_mutex_unlock(&lifecycle_mutex); return 0; }
    pthread_mutex_lock(&state_mutex);
    status=(AutoloadSnapshot){.state=AUTOLOAD_WAITING,.remaining=-1,.remaining_ms=-1000,.entry_index=-1};
    cancel_requested=0; frontend_ready=0; countdown_end=0; entry_delay_end=0; running=1;
    int result=pthread_create(&worker_thread,NULL,autoload_worker,NULL);
    if (result) { running=0;status.state=AUTOLOAD_FAILED; }
    else owned=1;
    pthread_mutex_unlock(&state_mutex);
    pthread_mutex_unlock(&lifecycle_mutex);
    if (result) pldmgr_log("[Autoload] Failed to create worker: %s\n",strerror(result));
    return result ? -1 : 0;
}
void pldmgr_autoload_abort(void) {
    pthread_mutex_lock(&state_mutex);
    if (running) { cancel_requested=1; pthread_cond_broadcast(&state_cond); }
    pthread_mutex_unlock(&state_mutex);
}
void pldmgr_autoload_stop(void) {
    pthread_mutex_lock(&lifecycle_mutex);
    if (owned) {
        pldmgr_autoload_abort();
        pthread_join(worker_thread,NULL);
        owned=0;
    }
    pthread_mutex_unlock(&lifecycle_mutex);
}
void pldmgr_autoload_reset(void) {
    pthread_mutex_lock(&state_mutex);
    if (!running) {
        status=(AutoloadSnapshot){.state=AUTOLOAD_DISABLED,.remaining=-1,.remaining_ms=-1000,.entry_index=-1};
        countdown_end=0; entry_delay_end=0;
    }
    pthread_mutex_unlock(&state_mutex);
}
int pldmgr_autoload_update_config_entry(const char *old_key,const char *new_key) {
    int result=config_update_list_entry(AUTOLOAD_CONFIG_PATH,old_key,new_key);
    if (result) pldmgr_log("[Autoload] Failed to update entry: %s\n",old_key ? old_key : "");
    return result;
}

/* ── Fork: profiles / picker / startup ─────────────────────────────────── */

int pldmgr_autoload_is_picker(void) { return picker_active; }

void pldmgr_autoload_get_list(char *buf, size_t size) {
    if (!buf || !size) return;
    AutoloadSnapshot s;
    pldmgr_autoload_get_snapshot(&s);
    strncpy(buf, s.list, size - 1);
    buf[size - 1] = '\0';
}

int pldmgr_autoload_run_profile(const char *id) {
    if (!id || !id[0]) return -1;
    ProfileEntry *arr = calloc(MAX_PROFILES, sizeof(ProfileEntry));
    if (!arr) return -1;
    int count = 0;
    profiles_load(arr, &count);
    int idx = profiles_find_by_id(arr, count, id);
    if (idx < 0) { free(arr); return -1; }
    int rc = profiles_write_sequence(arr[idx].list);
    free(arr);
    if (rc != 0) return -1;
    /* Stop the finished/idle boot worker, then run the resolved sequence now
     * (forced => no countdown). stop() joins; start() spawns a fresh worker. */
    pldmgr_autoload_stop();
    picker_active = 0;
    forced_run = 1;
    rc = pldmgr_autoload_start();
    if (rc != 0) forced_run = 0;
    return rc;
}

/* ── Fork: always-run startup payloads (startup.txt) ───────────────────── */

int pldmgr_startup_is_enabled(const char *filename) {
    if (!filename) return 0;
    FILE *f = fopen(STARTUP_LIST_PATH, "r");
    if (!f) return 0;
    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (strcmp(line, filename) == 0) { found = 1; break; }
    }
    fclose(f);
    return found;
}

void pldmgr_startup_set(const char *filename, int enabled) {
    if (!filename || !filename[0]) return;
    /* Read existing entries (excluding the target). */
    char entries[MAX_LAUNCHED][256];
    int n = 0;
    FILE *f = fopen(STARTUP_LIST_PATH, "r");
    if (f) {
        char line[256];
        while (n < MAX_LAUNCHED && fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (line[0] == '\0') continue;
            if (strcmp(line, filename) == 0) continue; /* drop target; re-added below if enabling */
            strncpy(entries[n], line, sizeof(entries[0]) - 1);
            entries[n][sizeof(entries[0]) - 1] = '\0';
            n++;
        }
        fclose(f);
    }
    ensure_dir_recursive(BASE_DATA_DIR);
    f = fopen(STARTUP_LIST_PATH, "w");
    if (!f) return;
    for (int i = 0; i < n; i++) fprintf(f, "%s\n", entries[i]);
    if (enabled) fprintf(f, "%s\n", filename);
    fclose(f);
    pldmgr_log("[Startup] %s %s\n", filename, enabled ? "enabled" : "disabled");
}

int pldmgr_startup_list_json(char *buf, size_t size) {
    JsonListBuilder jb = { buf, size, 0, 1, 0 };
    buf[0] = '\0';
    json_append(&jb, "{\"startup\":[");
    FILE *f = fopen(STARTUP_LIST_PATH, "r");
    if (f) {
        char line[256];
        int first = 1;
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (line[0] == '\0') continue;
            char esc[512];
            json_escape_checked(&jb, line, esc, sizeof(esc));
            json_append(&jb, "%s\"%s\"", first ? "" : ",", esc);
            first = 0;
        }
        fclose(f);
    }
    json_append(&jb, "]}");
    return 0;
}
