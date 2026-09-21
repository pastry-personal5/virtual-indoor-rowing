#!/usr/bin/env python3
"""CLI wrapper for the Phase 2 content-origin canary."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from vir_dev import content  # noqa: E402


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("command", choices=("canary", "fixture", "release-package"))
args = parser.parse_args()
raise SystemExit(content.command(args.command))
