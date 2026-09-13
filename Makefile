.PHONY: doctor configure build test format-check pm5-tui unreal-smoke hil-pm5 clean

# Verify the host machine and installed tools against the pinned M1 baseline.
doctor:
	python3 Scripts/dev.py doctor

# Generate the native CMake/Ninja build files for the arm64 Debug configuration.
configure:
	python3 Scripts/dev.py configure

# Configure if needed, then compile all native diagnostic-client targets.
build:
	python3 Scripts/dev.py build

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

# Launch the PM5 diagnostic UI for a user-driven hardware-in-the-loop session.
hil-pm5:
	python3 Scripts/dev.py hil-pm5

# Remove generated native build output.
clean:
	python3 Scripts/dev.py clean
