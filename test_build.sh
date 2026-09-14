#!/bin/sh

set -eu

# Backward-compatible entry point. The root Makefile owns the pinned toolchain,
# complete native target graph, and test suite.
script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec make -C "$script_directory" test
