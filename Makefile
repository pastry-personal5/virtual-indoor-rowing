.PHONY: doctor configure build native-app test format-check pm5-tui unreal-smoke unreal-shipping unreal-package-verify toolchain-bluetooth-probe toolchain-bluetooth-probe-clean release-sign-notarize hil-pm5 pm5-tui-journal clean clean-unreal clean-logs clean-metrics clean-all

# Verify the host machine and installed tools against the pinned M1 baseline.
doctor:
	python3 Scripts/dev.py doctor

# Generate the native CMake/Ninja build files for the arm64 Debug configuration.
configure:
	python3 Scripts/dev.py configure

# Configure if needed, then compile all native diagnostic-client targets.
build:
	python3 Scripts/dev.py build

# Build the Release, static-protobuf archive (Build/native-app/) that the Unreal
# VirtualRowing module links. The first configure downloads hash-pinned protobuf and
# abseil sources, so it needs network access. `make unreal-smoke` runs this first.
native-app:
	python3 Scripts/dev.py native-app

# Build the native targets, then run CTest with failure output enabled.
test:
	python3 Scripts/dev.py test

# Check C++ and Objective-C++ source formatting without modifying files.
format-check:
	python3 Scripts/dev.py format-check

# Build and launch the interactive PM5 diagnostic terminal UI.
pm5-tui:
	python3 Scripts/dev.py pm5-tui

# Compile the Unreal Editor development target using the approved UE installation.
unreal-smoke:
	python3 Scripts/dev.py unreal-smoke

# Build, cook, stage, and package the unsigned arm64 Shipping diagnostic host.
unreal-shipping:
	python3 Scripts/dev.py unreal-shipping

# Inspect only the staged package; this does not sign, notarize, or launch it.
unreal-package-verify:
	python3 Scripts/dev.py unreal-package-verify

# Explicitly invoke the bounded CoreBluetooth/TCC diagnostic in the staged app.
toolchain-bluetooth-probe:
	python3 Scripts/dev.py toolchain-bluetooth-probe

# Wipe every Unreal intermediate/output, rebuild+archive+verify from scratch, then run
# the probe — eliminates any chance of testing a stale .app left over from a prior
# source edit (see docs/phase-0: the probe never rebuilds the app itself). Slower than
# `toolchain-bluetooth-probe` alone since it forces a full, uncached BuildCookRun.
toolchain-bluetooth-probe-clean: doctor clean-unreal unreal-shipping unreal-package-verify toolchain-bluetooth-probe

# Protected credential-owner release procedure. Never accepts credentials as arguments.
release-sign-notarize:
	python3 Scripts/dev.py release-sign-notarize

# Launch the PM5 diagnostic UI for a user-driven hardware-in-the-loop session.
hil-pm5:
	python3 Scripts/dev.py hil-pm5

# Launch the PM5 diagnostic UI with the opt-in Keychain-sealed workout journal
# (Phase 1 Milestone 4). The journal holds athlete data; keep it local.
pm5-tui-journal:
	python3 Scripts/dev.py pm5-tui-journal

# Remove generated native build output (Build/native/ and Build/native-app/).
clean:
	python3 Scripts/dev.py clean

# Remove generated Unreal intermediate/output directories: Saved/, Intermediate/,
# Binaries/, DerivedDataCache/, and Build/unreal-shipping/.
clean-unreal:
	python3 Scripts/dev.py clean-unreal

# Purge only generated PM5 TUI logs; preserve Logs/.gitkeep and unrelated files.
clean-logs:
	rm -f Logs/pm5-tui/*.log Logs/pm5-tui/*.log.*

# Purge only generated PM5 TUI metrics; never search/delete broad directories.
clean-metrics:
	rm -f Metrics/pm5-tui/*.jsonl

# Clean both logs and metrics
clean-all: clean-logs clean-metrics
	@echo "All logs and metrics files have been purged."
