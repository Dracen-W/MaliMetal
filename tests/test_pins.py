#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Offline checks on the pin records.

These do not need a checkout and do not touch the network. They exist because a
pin is only real when every document agrees with it: the machine-readable file,
the prose, the attribution and the licence.

Run via tests/run.sh, or directly.
"""

from __future__ import annotations

import hashlib
import re
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CANONICAL_GPL2 = "edaef632cbb643e4e7a221717a6c441a4c1a7c918e6e4d56debc3d8739b233f6"

failures: list[str] = []


def check(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def main() -> int:
    pins = tomllib.loads(read("deps/deps.toml"))
    check(pins.get("version") == 1, "deps/deps.toml: version is not 1")

    by_id = {entry["id"]: entry for entry in pins.get("dependency", [])}
    for expected in ("mesa", "metal2vulkan"):
        check(expected in by_id, f"deps/deps.toml: missing {expected}")

    deps_md = read("docs/DEPENDENCIES.md")
    readme = read("README.md")
    notice = read("NOTICE")

    for ident, entry in by_id.items():
        revision = entry["revision"]
        check(
            revision in deps_md,
            f"{ident}: revision {revision[:12]} is not in docs/DEPENDENCIES.md",
        )
        check(
            len(entry.get("file", [])) > 0,
            f"{ident}: no file hashes recorded",
        )
        for item in entry.get("file", []):
            check(
                re.fullmatch(r"[0-9a-f]{64}", item["sha256"]) is not None,
                f"{ident}: {item['path']} has a malformed sha256",
            )

    mesa = by_id.get("mesa", {})
    mesa_revision = mesa.get("revision", "")
    check(
        "gitlab.freedesktop.org/mesa/mesa" in mesa.get("repository", ""),
        "mesa: repository is not the official GitLab",
    )
    check(
        mesa_revision[:8] in readme,
        "README.md does not mention the pinned Mesa revision",
    )
    check(
        mesa_revision in notice,
        "NOTICE does not mention the pinned Mesa revision",
    )
    check(
        mesa_revision[:8] in read("docs/SEAM.md"),
        "docs/SEAM.md does not mention the pinned Mesa revision",
    )

    m2v = by_id.get("metal2vulkan", {})
    check(
        m2v.get("revision", "") in notice,
        "NOTICE does not mention the pinned metal2vulkan revision",
    )
    check(
        "GPL-2.0-or-later OR LGPL-3.0-or-later" in notice,
        "NOTICE does not state metal2vulkan's real licence",
    )
    check(
        "MIT" not in m2v.get("licence", ""),
        "metal2vulkan licence recorded as MIT; it is GPL-2.0-or-later OR LGPL-3.0-or-later",
    )

    licence = (ROOT / "LICENSE").read_bytes()
    licence_hash = hashlib.sha256(licence).hexdigest()
    check(
        licence_hash == CANONICAL_GPL2,
        f"LICENSE is not the canonical GPL-2.0 text (sha256 {licence_hash})",
    )
    check(
        licence_hash in notice,
        "NOTICE does not record the LICENSE hash",
    )

    # The rejected provisional pin may only appear where it is documented as
    # history, so a stray reference is caught.
    rejected = "e24dc5bd1e7fe6101bdc866fb16a15a8fcae1aae"
    allowed = {"docs/DECISIONS.md", "docs/DEPENDENCIES.md"}
    for path in ROOT.rglob("*.md"):
        rel = path.relative_to(ROOT).as_posix()
        if rel in allowed or rel.startswith("AdrenoMetal-reference/"):
            continue
        if rejected in path.read_text(encoding="utf-8"):
            failures.append(f"{rel}: stale reference to the rejected Mesa pin")

    # Every source file states its licence in an SPDX header. This is the cheap
    # guard for "always keep the licence and the credit": a copied file without
    # its header fails here before it is committed.
    source_suffixes = {".py", ".sh", ".c", ".cc", ".cpp", ".h", ".hpp", ".mm", ".m", ".rs"}
    for path in ROOT.rglob("*"):
        if not path.is_file() or path.suffix not in source_suffixes:
            continue
        rel = path.relative_to(ROOT).as_posix()
        if rel.startswith(("AdrenoMetal-reference/", "build/", "third_party/")):
            continue
        head = "\n".join(path.read_text(encoding="utf-8", errors="replace").splitlines()[:8])
        if "SPDX-License-Identifier:" not in head:
            failures.append(f"{rel}: no SPDX-License-Identifier in the first 8 lines")

    if failures:
        for failure in failures:
            print(f"FAIL {failure}")
        return 1
    print(f"OK   pins: {len(by_id)} dependencies, all documents and LICENSE agree")
    return 0


if __name__ == "__main__":
    sys.exit(main())
