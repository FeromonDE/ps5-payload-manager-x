#ifndef VERSION_X_H
#define VERSION_X_H

/*
 * Fork versioning for "PS5 Payload Manager X" (bsk193/ps5-payload-manager-x).
 *
 * PLDMGRX_VERSION   - the fork's OWN semver ("1.0.0", "1.0.1-beta.1"), set at
 *                     build time from git tags via -DPLDMGRX_VERSION (see
 *                     tools/fork_version.sh + the Makefile). Untagged/dev builds
 *                     fall back to "0.0.0-dev".
 * PLDMGRX_UPSTREAM_VERSION - the upstream itsPLK release this fork is based on.
 *                     It is just upstream's MENU_VERSION, which the fork NEVER
 *                     edits, so merging upstream never conflicts on version lines.
 *
 * Display the fork version wherever the app shows a version, with
 * "based on v<PLDMGRX_UPSTREAM_VERSION>" alongside.
 */

#include "pldmgr.h" /* for MENU_VERSION (upstream's, never edited by the fork) */

#ifndef PLDMGRX_VERSION
#define PLDMGRX_VERSION "0.0.0-dev"
#endif

#define PLDMGRX_UPSTREAM_VERSION MENU_VERSION

/* The version THIS build reports and self-updates against: the fork's own semver
 * for the X build, upstream's for a stock build. Self-update always compares a
 * candidate's version against this (fork-vs-fork, never fork-vs-upstream). */
#ifdef PLDMGRX
#define PLDMGR_RUNNING_VERSION PLDMGRX_VERSION
#else
#define PLDMGR_RUNNING_VERSION MENU_VERSION
#endif

/* Human-facing version string shown in the UI (served by /version). All operands
 * are string literals, so this concatenates at compile time. */
#ifdef PLDMGRX
#define PLDMGR_VERSION_DISPLAY PLDMGRX_VERSION " (based on v" MENU_VERSION ")"
#else
#define PLDMGR_VERSION_DISPLAY MENU_VERSION
#endif

#endif /* VERSION_X_H */
