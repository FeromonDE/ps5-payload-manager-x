#!/usr/bin/env bash
#
# Fork version helper for PS5 Payload Manager X.
#
#   tools/fork_version.sh upstream     prints upstream's version (MENU_VERSION),
#                                      read from the untouched version file.
#   tools/fork_version.sh dev          prints the version for a non-release build
#                                      (git describe of x-v* tags; 0.0.0-dev if none).
#   tools/fork_version.sh parse <tag>  validates an x-v tag and prints
#                                      "<version> <release|prerelease>".
#
set -euo pipefail
cd "$(dirname "$0")/.."

PREFIX='x-v'
UPSTREAM_VERSION_FILE='include/pldmgr.h'
UPSTREAM_VERSION_MACRO='MENU_VERSION'

# x-v1.2.3  or  x-v1.2.3-alpha.4 / -beta.4 / -rc.4   (the dot before N is required)
TAG_RE="^${PREFIX}(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-(alpha|beta|rc)\.(0|[1-9][0-9]*))?$"

case "${1:-}" in
  upstream)
    UP=$(sed -n "s/^#define ${UPSTREAM_VERSION_MACRO} \"\(.*\)\".*/\1/p" "$UPSTREAM_VERSION_FILE" | tr -d '\r')
    [ -n "$UP" ] || { echo "upstream version not found in $UPSTREAM_VERSION_FILE" >&2; exit 1; }
    echo "$UP" ;;
  dev)
    DESC=$(git describe --tags --match "${PREFIX}*" 2>/dev/null || true)
    if [ -n "$DESC" ]; then echo "${DESC#$PREFIX}"; else echo "0.0.0-dev"; fi ;;
  parse)
    TAG="${2:-}"
    [[ "$TAG" =~ $TAG_RE ]] || { echo "Invalid tag '$TAG': use ${PREFIX}1.2.3 or ${PREFIX}1.2.3-beta.1" >&2; exit 1; }
    VER="${TAG#$PREFIX}"
    if [[ "$VER" == *-* ]]; then echo "$VER prerelease"; else echo "$VER release"; fi ;;
  *)
    echo "usage: $0 upstream | dev | parse <tag>" >&2; exit 1 ;;
esac
