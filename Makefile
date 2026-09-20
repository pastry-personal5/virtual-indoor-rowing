# Root entry points. Every target is a thin wrapper over Scripts/dev.py (see
# Scripts/vir_dev/) so builds never depend on personal shell setup. `make` alone
# prints this list.
#
# Naming: core native-build verbs are unprefixed (doctor, configure, build, test,
# format-check, clean); everything else is <area>-<action>, with the areas
# pm5-tui, unreal, release and clean.

PYTHON ?= python3
DEV := $(PYTHON) Scripts/dev.py
VIR_GO_CACHE ?= /tmp/virtual-rowing-go-build
VIR_GO_MOD_CACHE ?= /tmp/virtual-rowing-go-modcache

.DEFAULT_GOAL := help

.PHONY: help
help: ## List the available targets.
	@awk 'BEGIN {FS = ":.*## "} /^[a-zA-Z0-9_-]+:.*## / {printf "  %-32s %s\n", $$1, $$2}' $(MAKEFILE_LIST)

# --- Native build and test ---------------------------------------------------

.PHONY: doctor configure build test format format-check phase1-check development-sync-test

doctor: ## Verify the host and installed tools against Config/BuildVersions.json.
	$(DEV) doctor

configure: ## Generate the native CMake/Ninja build files (arm64 Debug, Build/native/).
	$(DEV) configure

build: ## Configure if needed, then compile all native diagnostic-client targets.
	$(DEV) build

test: ## Build, then run CTest with failure output enabled.
	$(DEV) test

format-check: ## Check C++/Objective-C++ formatting without modifying files.
	$(DEV) format-check

format: ## Apply the pinned C++/Objective-C++ formatter.
	$(DEV) format

phase1-check: ## Validate Phase 1 milestone numbering, links, and evidence statuses.
	$(PYTHON) Scripts/check_phase1_packet.py

development-sync-test: ## Run the development-sync Go test suite.
	cd Services/development-sync && GOCACHE="$(VIR_GO_CACHE)" GOMODCACHE="$(VIR_GO_MOD_CACHE)" go test ./...

# --- Optional loopback development sync -------------------------------------

.PHONY: development-sync-bootstrap development-sync-up development-sync-migrate development-sync-down development-sync-reset
development-sync-bootstrap: ## Generate the owner-only Compose bootstrap secret.
	$(PYTHON) Scripts/development_sync.py bootstrap
development-sync-up: ## Start the opt-in loopback-only development sync API.
	$(PYTHON) Scripts/development_sync.py up
development-sync-migrate: ## Apply forward-only development-sync migrations to an existing Compose volume.
	$(PYTHON) Scripts/development_sync.py migrate
development-sync-down: ## Stop the development sync API, preserving any volumes.
	$(PYTHON) Scripts/development_sync.py down
development-sync-reset: ## Destructively remove only named development-sync Compose volumes.
	$(PYTHON) Scripts/development_sync.py reset

# --- PM5 diagnostic TUI ------------------------------------------------------

.PHONY: pm5-tui pm5-tui-hil pm5-tui-journal

pm5-tui: ## Build and launch the interactive PM5 diagnostic TUI.
	$(DEV) pm5-tui

# Real PM5 required; the capture is private athlete/device evidence.
pm5-tui-hil: ## Launch the TUI with the bounded hardware-probe capture (user-driven).
	$(DEV) pm5-tui-hil

# The journal holds athlete data; keep it local.
pm5-tui-journal: ## Launch the TUI with the opt-in Keychain-sealed workout journal.
	$(DEV) pm5-tui-journal

# --- Unreal ------------------------------------------------------------------

.PHONY: unreal-native-app unreal-smoke unreal-shipping unreal-package-verify unreal-bluetooth-probe unreal-bluetooth-probe-clean content-canary content-fixture

# The first configure downloads hash-pinned protobuf and abseil sources (network).
unreal-native-app: ## Build the Release static archive (Build/native-app/) the Unreal module links.
	$(DEV) unreal-native-app

unreal-smoke: ## Build unreal-native-app, then compile the UnrealEditor Development target.
	$(DEV) unreal-smoke

unreal-shipping: ## BuildCookRun the unsigned arm64 Shipping diagnostic host.
	$(DEV) unreal-shipping

unreal-package-verify: ## Inspect the staged Shipping .app only (no sign/notarize/launch).
	$(DEV) unreal-package-verify

content-canary: ## Run the deterministic Phase 2 origin/download/fallback/rollback canary.
	$(DEV) content-canary

content-fixture: ## Generate the local deterministic content-origin fixture (not Shipping evidence).
	$(DEV) content-fixture

unreal-bluetooth-probe: ## Run the bounded CoreBluetooth/TCC diagnostic in the staged app.
	$(DEV) unreal-bluetooth-probe

# The probe never rebuilds the app, so this wipes every Unreal intermediate first to
# rule out testing a stale .app. Slower: it forces a full, uncached BuildCookRun.
unreal-bluetooth-probe-clean: doctor clean-unreal unreal-shipping unreal-package-verify unreal-bluetooth-probe ## Rebuild the Shipping app from scratch, then run the probe.

# --- Release -----------------------------------------------------------------

.PHONY: release-sign-notarize

# Never accepts credentials as arguments; reads VIR_DEVELOPER_ID_IDENTITY and
# VIR_NOTARY_KEYCHAIN_PROFILE from the environment.
release-sign-notarize: ## Protected sign/notarize procedure for the credential owner.
	$(DEV) release-sign-notarize

# --- Clean -------------------------------------------------------------------

.PHONY: clean clean-unreal clean-logs clean-metrics clean-diagnostics

clean: ## Remove native build output (Build/native/ and Build/native-app/).
	$(DEV) clean

clean-unreal: ## Remove Unreal intermediates: Saved/, Intermediate/, Binaries/, DerivedDataCache/, Build/unreal-shipping/.
	$(DEV) clean-unreal

# The two below delete only generated PM5 TUI files by exact pattern.
clean-logs: ## Purge generated PM5 TUI logs (keeps Logs/.gitkeep).
	rm -f Logs/pm5-tui/*.log Logs/pm5-tui/*.log.*

clean-metrics: ## Purge generated PM5 TUI metrics JSONL.
	rm -f Metrics/pm5-tui/*.jsonl

clean-diagnostics: clean-logs clean-metrics ## Purge both PM5 TUI logs and metrics.
	@echo "All logs and metrics files have been purged."

# --- Deprecated names --------------------------------------------------------
# Old target names still work and forward to the new ones with a notice, so older
# docs and muscle memory keep running. Remove once nothing refers to them.

define DEPRECATED_ALIAS
.PHONY: $(1)
$(1):
	@echo "note: 'make $(1)' is deprecated; use 'make $(2)'" >&2
	@$$(MAKE) --no-print-directory $(2)
endef

$(eval $(call DEPRECATED_ALIAS,native-app,unreal-native-app))
$(eval $(call DEPRECATED_ALIAS,hil-pm5,pm5-tui-hil))
$(eval $(call DEPRECATED_ALIAS,toolchain-bluetooth-probe,unreal-bluetooth-probe))
$(eval $(call DEPRECATED_ALIAS,toolchain-bluetooth-probe-clean,unreal-bluetooth-probe-clean))
$(eval $(call DEPRECATED_ALIAS,clean-all,clean-diagnostics))
