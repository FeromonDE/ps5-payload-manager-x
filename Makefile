# Payload Manager - Native PS5 ELF Daemon Makefile

# Tools
PYTHON := python3
HOST_CC ?= clang
CC     := /opt/ps5-payload-sdk/bin/prospero-clang
STRIP  := /opt/ps5-payload-sdk/bin/prospero-strip

# Paths
SDK      := /opt/ps5-payload-sdk
TARGET   := $(SDK)/target
INCLUDES := -Iinclude -I$(TARGET)/include
LIBS     := $(TARGET)/lib/libcurl.a \
            $(TARGET)/lib/libmbedtls.a \
            $(TARGET)/lib/libmbedx509.a \
            $(TARGET)/lib/libmbedcrypto.a \
            $(TARGET)/lib/libmicrohttpd.a \
            -L$(TARGET)/lib -lpthread \
            -lSceNetCtl -lSceUserService -lSceSystemService \
            -lSceAppInstUtil -lSceHttp2 -lSceSsl -lSceNet

# Source Files
SRCS := src/main.c src/http_server.c src/config.c src/log_server.c \
        src/sha256.c src/json_helpers.c src/repository.c src/sources.c \
        src/payload_mgr.c src/process_mgr.c src/profiles.c \
        src/ps5_launcher.c src/notification.c src/utils.c src/autoload.c \
        src/app_installer.c src/history_mgr.c src/slot.c
OBJS := $(SRCS:.c=.o)
ELF  := pldmgr.elf

# Assets
FRONTEND_DIST := frontend/dist/index.html
ASSET_HEADER  := include/assets_index_html.h
CA_HEADER     := include/assets_cacert_pem.h
MANIFEST_HEADER := include/assets_cache_appcache.h
FRONTEND_MANIFEST := frontend/dist/cache.appcache
FAVICON_SVG_HEADER := include/assets_favicon_svg.h
ICON_PNG_HEADER := include/assets_icon_png.h




# Compiler Flags
CFLAGS := -Os -Wall -ffunction-sections -fdata-sections $(INCLUDES)
LDFLAGS := -Wl,--gc-sections

# Fork (Payload Manager X) version — our own semver, independent of upstream's
# MENU_VERSION. From git tags via tools/fork_version.sh (git describe of x-v*
# tags; 0.0.0-dev when untagged). Override with `make X_VERSION=1.2.3` (the
# release workflow passes the exact tagged version).
X_VERSION ?= $(shell bash tools/fork_version.sh dev 2>/dev/null)
ifneq ($(X_VERSION),)
CFLAGS += -DPLDMGRX_VERSION=\"$(X_VERSION)\"
endif

# "Payload Manager X" build: `make ... PLDMGRX=1 [PLDMGRX_PORT=8084|8184]`.
# Uses the X identity (name "Payload Manager X", app id PLDM00002, /data/pldmgrx,
# "PLDMGRX_VER:" self signature). Only the HTTP port varies:
#   PLDMGRX_PORT=8084 -> pldmgrx_8084.elf : drop-in replacement for official
#   PLDMGRX_PORT=8184 -> pldmgrx_8184.elf : runs alongside the official manager
PLDMGRX_PORT ?= 8084
PARAM_X :=
ifdef PLDMGRX
CFLAGS += -DPLDMGRX -DMENU_PORT_OVERRIDE=$(PLDMGRX_PORT)
ELF := pldmgrx_$(PLDMGRX_PORT).elf
PARAM_X := assets/param_x.json
# X->X self-update scans ELFs for this "PLDMGRX_VER:<version>" signature; if
# --gc-sections ever drops it again, fail the build instead of shipping a broken
# self-update (regression guard for the x-v1.1.0 bug).
SIG_CHECK = strings "$(ELF)" | grep -qF "PLDMGRX_VER:$(X_VERSION)" || { echo "ERROR: self-update signature 'PLDMGRX_VER:$(X_VERSION)' missing from $(ELF) (gc-sections dropped it?)"; exit 1; }
else
SIG_CHECK = :
endif

all: $(ELF)

# Generate the X app metadata with the chosen port baked into its deeplink.
assets/param_x.json: assets/param_x.json.in
	@echo "Generating $@ for port $(PLDMGRX_PORT)..."
	sed 's/__PORT__/$(PLDMGRX_PORT)/g' $< > $@

# These tests execute the HTTP handler on the host with PS5 services stubbed.
# See DEVELOPMENT.md for the isolated container option and native build gate.
.PHONY: test-host test-frontend test
test-host:
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/run_json_escape_tests.py
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/run_json_decode_tests.py
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/run_json_backend_tests.py
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/run_json_size_tests.py
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/run_allocation_tests.py
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/run_integrity_tests.py
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/run_repository_icons_tests.py
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/run_self_update_tests.py
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/run_storage_tests.py
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/run_autoload_tests.py
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/run_launcher_tests.py
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/run_process_tests.py
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/run_log_stream_tests.py
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/run_config_tests.py
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/run_http_tests.py --autoload-only
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/run_http_tests.py --logs-only
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/run_http_tests.py --config-only
	HOST_CC="$(HOST_CC)" $(PYTHON) tests/run_http_tests.py

test-frontend:
	cd frontend && npm run lint && npm test

test: test-host test-frontend

# Build the React frontend
.PHONY: frontend-build
frontend-build:
	@echo "Building frontend..."
	git submodule update --init --recursive
	cd frontend && npm install && npm run build
	@XVER="$(X_VERSION)"; [ -n "$$XVER" ] || XVER=$$(bash tools/fork_version.sh dev 2>/dev/null); \
	UPVER=$$(grep '#define MENU_VERSION' include/pldmgr.h | awk '{print $$3}' | tr -d '"'); \
	COMMIT=$$(git rev-parse --short HEAD 2>/dev/null || echo "unknown"); \
	git diff --quiet || COMMIT="DEV"; \
	DATE=$$(date -u +"%Y-%m-%d %H:%M:%S UTC"); \
	TITLE="PS5 Payload Manager X v$$XVER (based on v$$UPVER) by PLK and bsk193 ($$COMMIT, built at $$DATE)"; \
	echo "Updating title in index.html to: $$TITLE"; \
	TMP=$$(mktemp "$${TMPDIR:-/tmp}/pldmgr.XXXXXX"); \
	sed "s|\[\[TITLE_PLACEHOLDER\]\]|$$TITLE|g" frontend/dist/index.html > $$TMP; \
	mv $$TMP frontend/dist/index.html; \
	echo "Updating build date in cache.appcache to: $$DATE"; \
	TMP=$$(mktemp "$${TMPDIR:-/tmp}/pldmgr.XXXXXX"); \
	sed "s|\[\[BUILD_DATE\]\]|$$DATE|g" frontend/dist/cache.appcache > $$TMP; \
	mv $$TMP frontend/dist/cache.appcache



$(ASSET_HEADER): $(FRONTEND_DIST)
	@echo "Generating asset header..."
	$(PYTHON) tools/gen_assets.py $(FRONTEND_DIST) $(ASSET_HEADER) index_html

$(MANIFEST_HEADER): $(FRONTEND_DIST)
	@echo "Generating Manifest asset header..."
	$(PYTHON) tools/gen_assets.py $(FRONTEND_MANIFEST) $(MANIFEST_HEADER) cache_appcache

$(FAVICON_SVG_HEADER): $(FRONTEND_DIST)
	@echo "Generating Favicon SVG asset header..."
	$(PYTHON) tools/gen_assets.py frontend/dist/favicon.svg $(FAVICON_SVG_HEADER) favicon_svg

$(ICON_PNG_HEADER): $(FRONTEND_DIST)
	@echo "Generating Icon PNG asset header..."
	$(PYTHON) tools/gen_assets.py frontend/dist/icon.png $(ICON_PNG_HEADER) icon_png

$(CA_HEADER):
	@echo "Downloading CA bundle..."
	wget -O include/cacert.pem https://curl.se/ca/cacert.pem
	$(PYTHON) tools/gen_assets.py include/cacert.pem $(CA_HEADER) cacert_pem
	rm include/cacert.pem

$(FRONTEND_DIST):
	@echo "ERROR: frontend/dist/index.html not found!"
	@echo "Please run 'make frontend-build' locally on your host machine first."
	@exit 1

$(ELF): $(ASSET_HEADER) $(MANIFEST_HEADER) $(CA_HEADER) $(FAVICON_SVG_HEADER) $(ICON_PNG_HEADER) $(PARAM_X) $(SRCS)
	@echo "Building $(ELF)..."
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(ELF) $(SRCS) $(LIBS)
	@$(SIG_CHECK)
	@echo "Stripping $(ELF)..."
	$(STRIP) $(ELF)

clean:
	rm -f $(ELF) $(ASSET_HEADER) $(MANIFEST_HEADER) $(CA_HEADER) $(FAVICON_SVG_HEADER) $(ICON_PNG_HEADER) assets/param_x.json src/*.o





dist-clean: clean
	rm -rf frontend/dist

.PHONY: all clean frontend-build dist-clean
