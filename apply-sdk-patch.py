#!/usr/bin/env python3

import argparse
import subprocess
from pathlib import Path


def run_git(sdk: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["git", "-C", str(sdk), *args],
        capture_output=True,
        text=True,
        check=False,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Apply AntiCheat's CS2 SDK compatibility patch.")
    parser.add_argument("sdk", type=Path, help="Path to the alliedmodders/s2sdk checkout")
    args = parser.parse_args()

    sdk = args.sdk.resolve()
    header = sdk / "public" / "engine2" / "isource2server.h"
    if not header.is_file():
        raise SystemExit(f"CS2 SDK header was not found: {header}")

    patch = Path(__file__).resolve().parent / "patches" / "s2sdk-isource2server-pr128.patch"
    forward_check = run_git(sdk, "apply", "--check", str(patch))
    if forward_check.returncode == 0:
        applied = run_git(sdk, "apply", str(patch))
        if applied.returncode != 0:
            raise SystemExit(f"Could not apply the CS2 SDK patch: {applied.stderr.strip()}")
        print("Applied the CS2 SDK ISource2Server compatibility patch.")
        return

    reverse_check = run_git(sdk, "apply", "--reverse", "--check", str(patch))
    if reverse_check.returncode == 0:
        print("The CS2 SDK ISource2Server compatibility patch is already applied.")
        return

    details = forward_check.stderr.strip() or reverse_check.stderr.strip()
    raise SystemExit(
        "The CS2 SDK checkout does not match the expected base for the compatibility patch. "
        f"Use the pinned SDK revision. {details}"
    )


if __name__ == "__main__":
    main()
