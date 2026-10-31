#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Copy the canonical NativeMaliABI.h into the build tree, pinned by hash.

The kernel header lives in the Mali-G52 repository and is the single source of
truth. This repository must not carry a second copy of those records, so the
copy used to compile is generated into build/abi/ (gitignored) and checked
against deps/abi.sha256.

    python3 tools/sync-abi.py                       # default: ../Mali-G52
    python3 tools/sync-abi.py --kernel /path/Mali-G52
    python3 tools/sync-abi.py --accept              # record a new hash

The hash is the cross-repository contract: if the kernel header changes, this
tool fails until a human records the new hash and re-runs the tests. --accept
refuses to record a change that also moves the ABI version or the last method
selector; that is an ABI decision for Mali-G52, not a hash bump.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PIN = ROOT / "deps" / "abi.sha256"
HEADER = "NativeMaliABI.h"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_contract(header: Path) -> tuple[str, str]:
    text = header.read_text(encoding="utf-8")
    version = re.search(r"NativeMaliAbiVersion\s*=\s*(\d+)", text)
    last = re.search(r"NativeMaliMethodLast\s*=\s*NativeMaliMethod(\w+)", text)
    if not version or not last:
        raise SystemExit(f"{header}: cannot read the ABI version/method markers")
    return version.group(1), last.group(1)


def read_pin(path: Path) -> tuple[str, str, str] | None:
    if not path.is_file():
        return None
    digest = version = last = ""
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("#"):
            if "version=" in line:
                fields = dict(
                    part.split("=", 1) for part in line.lstrip("# ").split() if "=" in part
                )
                version = fields.get("version", "")
                last = fields.get("last", "")
            continue
        if line:
            digest = line.split()[0]
    if not digest:
        return None
    return digest, version, last


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--kernel",
        default=str(ROOT.parent),
        help="Mali-G52 checkout (default: the parent directory)",
    )
    parser.add_argument(
        "--accept",
        action="store_true",
        help="record the current hash as the pin instead of failing",
    )
    args = parser.parse_args(argv)

    source = Path(args.kernel).expanduser().resolve() / HEADER
    if not source.is_file():
        print(f"FAIL {source} does not exist; set --kernel to the Mali-G52 checkout")
        return 1

    actual = sha256(source)
    new_version, new_last = parse_contract(source)
    pin = read_pin(PIN)

    if pin is None:
        if not args.accept:
            print(f"FAIL {PIN} is missing; run with --accept to create it")
            return 1
    elif pin[0] != actual:
        old_version, old_last = pin[1], pin[2]
        print(f"NativeMaliABI.h sha256 {actual}")
        if not args.accept:
            print(f"FAIL pinned sha256 is {pin[0]}")
            print("     if the kernel changed on purpose, review the diff, then")
            print("     run: python3 tools/sync-abi.py --accept")
            return 1
        if (old_version, old_last) not in (("", ""), (new_version, new_last)):
            print(
                "FAIL refusing to accept silently: the ABI version or the last "
                f"method changed ({old_version}/{old_last} -> {new_version}/{new_last})."
            )
            print("     That is an ABI decision for Mali-G52, not a hash bump.")
            return 1

    if pin is None or pin[0] != actual:
        PIN.write_text(
            "# sha256 of the canonical NativeMaliABI.h in Mali-G52\n"
            f"# version={new_version} last={new_last}\n"
            f"{actual}  {HEADER}\n",
            encoding="utf-8",
        )
        print(f"OK   recorded {actual} (version {new_version}, last {new_last})")

    target = ROOT / "build" / "abi" / HEADER
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    if sha256(target) != actual:
        print(f"FAIL copy at {target} does not match the source")
        return 1
    print(f"OK   {target} ({actual[:12]})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
