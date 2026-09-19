#!/usr/bin/env python3
"""Narrow development-sync stack helpers; never reads or writes app journals."""
from __future__ import annotations
import argparse
import pathlib
import secrets
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
STACK = ROOT / "Services" / "development-sync"
ENV = STACK / ".env"

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("bootstrap", "up", "down", "reset"))
    command = parser.parse_args().command
    if command == "bootstrap":
        ENV.write_text(
            "VIR_BOOTSTRAP_SECRET=" + secrets.token_urlsafe(48) + "\n"
            "VIR_POSTGRES_PASSWORD=" + secrets.token_urlsafe(32) + "\n"
            "VIR_MINIO_PASSWORD=" + secrets.token_urlsafe(32) + "\n",
            encoding="utf-8",
        )
        ENV.chmod(0o600)
        print(f"wrote owner-only {ENV.relative_to(ROOT)}")
        return
    compose = ["docker", "compose", "--env-file", str(ENV), "-f", str(STACK / "compose.yaml")]
    if command == "up": subprocess.run(compose + ["up", "--build", "-d"], check=True)
    elif command == "down": subprocess.run(compose + ["down"], check=True)
    else: subprocess.run(compose + ["down", "--volumes"], check=True)

if __name__ == "__main__": main()
