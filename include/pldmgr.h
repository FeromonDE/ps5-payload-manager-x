#pragma once

/* ── Build identity ────────────────────────────────────────────
 * Define PLDMGRX (e.g. `make ... PLDMGRX=1`) to build "Payload Manager X": a
 * second instance that installs and runs next to a stock Payload Manager without
 * colliding — its own HTTP port, home-screen app id/name, data dir
 * (/data/pldmgrx), self-update signature and process name. Without the flag the
 * build is identical to stock. */
#ifdef PLDMGRX
  /* Port chosen at build time (make ... PLDMGRX_PORT=8084|8184). 8084 = drop-in
   * replacement for the official manager; 8184 = runs alongside it. */
  #ifdef MENU_PORT_OVERRIDE
    #define MENU_PORT MENU_PORT_OVERRIDE
  #else
    #define MENU_PORT 8084
  #endif
  #define PLDMGR_DATA_ROOT "/data/pldmgrx"
  /* PS5 title ids are 4 uppercase letters + 5 digits; PLDM00002 keeps the proven
   * prefix with a distinct number so the X app tile is separate from stock. */
  #define PLDMGR_TITLE_ID  "PLDM00002"
  #define PLDMGR_APP_NAME  "Payload Manager X"
  /* Distinct from stock so the two never mistake each other for "self": the X
   * build embeds/scans a "PLDMGRX_VER:" signature and names its thread
   * "pldmgrx.elf", so self-update won't launch stock pldmgr and they won't
   * SIGKILL each other. */
  #define PLDMGR_SELF_MARKER "PLDMGRX_VER:"
  #define PLDMGR_PROC_NAME   "pldmgrx.elf"
  #define PLDMGR_SELF_ID     "pldmgrx"
#else
  #define MENU_PORT        8084
  #define PLDMGR_DATA_ROOT "/data/pldmgr"
  #define PLDMGR_TITLE_ID  "PLDM00001"
  #define PLDMGR_APP_NAME  "Payload Manager"
  #define PLDMGR_SELF_MARKER "PLDMGR_VER:"
  #define PLDMGR_PROC_NAME   "pldmgr.elf"
  #define PLDMGR_SELF_ID     "pldmgr"
#endif

/* Network Settings */
#define ELFLDR_PORT 9021

/* Routes */
#define ROUTE_INDEX "/"
#define ROUTE_INDEX_HTML "/index.html"
#define ROUTE_LIST_PAYLOADS "/list_payloads"
#define ROUTE_UPLOAD "/manage:upload"
#define ROUTE_CHECK "/manage:check"
#define ROUTE_DELETE "/manage:delete"
#define ROUTE_LOAD_PAYLOAD "/loadpayload:"
#define ROUTE_SHUTDOWN "/shutdown"
#define ROUTE_LOG "/log"
#define ROUTE_VERSION "/version"
#define ROUTE_GETIP "/getip"
#define ROUTE_GET_CONFIG "/get_config"
#define ROUTE_SET_CONFIG "/set_config"
#define ROUTE_ABORT "/abort"
#define ROUTE_AUTOLOAD_STATUS "/autoload_status"
#define ROUTE_AUTOLOAD_CLEAR "/autoload_clear"
#define ROUTE_REPO_ICONS "/repository_icons"
#define ROUTE_REPO_CACHE_CLEAR "/repository_cache_clear"
#define ROUTE_REPO_LIST "/repository_payloads"
#define ROUTE_REPO_REFRESH "/repository_refresh"
#define ROUTE_REPO_INSTALL "/repository_install"
#define ROUTE_UPDATE_CHECK "/update_check"
#define ROUTE_SOURCES_LIST "/sources_list"
#define ROUTE_SOURCES_SET "/sources_set"
#define ROUTE_SOURCES_ADD "/sources_add"
#define ROUTE_SOURCES_REMOVE "/sources_remove"
#define ROUTE_USB_MOVE_CHECK "/usb_move_check"
#define ROUTE_USB_MOVE_PERFORM "/usb_move_perform"
#define ROUTE_CACHE_MANIFEST "/cache.appcache"

#define ROUTE_PROCESSES_LIST "/processes_list"
#define ROUTE_PROCESS_KILL "/process_kill"
#define ROUTE_HISTORY_LIST "/history_list"

/* MENU_VERSION is upstream's and is NEVER edited by the fork, so upstream merges
 * never conflict here. The fork's own version lives in include/version_x.h
 * (PLDMGRX_VERSION) and is shown as "based on vX". */
#define MENU_VERSION "0.6.3"
#define AUTOLOAD_CONFIG_PATH PLDMGR_DATA_ROOT "/autoload.txt"
#define FAVORITES_CONFIG_PATH PLDMGR_DATA_ROOT "/favorites.txt"
#define PLDMGR_CONFIG_PATH PLDMGR_DATA_ROOT "/pldmgr_config.txt"
#define REPOSITORY_CACHE_PATH PLDMGR_DATA_ROOT "/repository_cache.json"
#define PAYLOADS_STORAGE_DIR PLDMGR_DATA_ROOT "/payloads"
#define REPOSITORY_SOURCE_URL                                                  \
  "https://bsk193.github.io/console-homebrew-hub/payloads_v2.json"
#define LEGACY_REPOSITORY_SOURCE_URL                                           \
  "https://bsk193.github.io/console-homebrew-hub/payloads.json"
#define REPOSITORY_REFRESH_INTERVAL_SEC 10800

/* Logging (implementation in log_server.c) */
void pldmgr_log(const char *fmt, ...);
int pldmgr_server_is_active();

#include "autoload.h"
#include "notification.h"
#include "utils.h"

/* Paths */
#define BASE_DATA_DIR PLDMGR_DATA_ROOT
#define SOURCES_CONFIG_PATH PLDMGR_DATA_ROOT "/sources.json"
#define MAX_SOURCES 50

/* Scan Locations (Internal + 8 USB ports). The internal dir follows the build's
 * data root; USB payload dirs stay shared with stock. */
static const char *SCAN_DIRS[] = {
    PLDMGR_DATA_ROOT,   "/mnt/usb0/pldmgr", "/mnt/usb1/pldmgr",
    "/mnt/usb2/pldmgr", "/mnt/usb3/pldmgr", "/mnt/usb4/pldmgr",
    "/mnt/usb5/pldmgr", "/mnt/usb6/pldmgr", "/mnt/usb7/pldmgr"};
#define SCAN_DIRS_COUNT 9

/* Messages */
#define MSG_OK "OK"
