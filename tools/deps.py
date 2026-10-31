#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Verify MaliMetal's dependency pins against local checkouts.

The pins live in deps/deps.toml and are explained in docs/DEPENDENCIES.md. This
tool exists so a build (and a human) can refuse to proceed when a checkout is
not the revision that was read when the design was written.

Usage:
    python3 tools/deps.py verify --checkout mesa=/path/to/mesa
    python3 tools/deps.py verify --require-all \
        --checkout mesa=/path/to/mesa \
        --checkout metal2vulkan=/path/to/metal2vulkan
    python3 tools/deps.py selftest

Exit status is 0 only when every checked dependency matches. A dependency that
is not mentioned on the command line is reported as SKIP; --require-all turns
that into a failure, which is what a release build should use.

This tool never downloads anything and never touches a checkout.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
import tempfile
import tomllib
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class Dependency:
    id: str
    repository: str
    revision: str
    tag: str
    files: list[tuple[str, str]] = field(default_factory=list)


def load_pins(path: Path) -> list[Dependency]:
    with path.open("rb") as handle:
        data = tomllib.load(handle)
    if data.get("version") != 1:
        raise SystemExit(f"{path}: unsupported pin file version {data.get('version')!r}")
    deps: list[Dependency] = []
    for entry in data.get("dependency", []):
        dep = Dependency(
            id=entry["id"],
            repository=entry["repository"],
            revision=entry["revision"],
            tag=entry.get("tag", ""),
        )
        for item in entry.get("file", []):
            dep.files.append((item["path"], item["sha256"].lower()))
        if not dep.files:
            raise SystemExit(f"{path}: dependency {dep.id} records no files")
        deps.append(dep)
    if not deps:
        raise SystemExit(f"{path}: no dependencies")
    return deps


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_head(checkout: Path) -> str | None:
    if not (checkout / ".git").exists():
        return None
    try:
        out = subprocess.run(
            ["git", "-C", str(checkout), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return None
    return out.stdout.strip()


def verify_dependency(dep: Dependency, checkout: Path) -> list[str]:
    """Return the list of problems; an empty list means the checkout matches."""
    problems: list[str] = []
    if not checkout.is_dir():
        return [f"{dep.id}: checkout {checkout} is not a directory"]

    head = git_head(checkout)
    if head is None:
        problems.append(
            f"{dep.id}: {checkout} is not a git checkout; cannot confirm revision "
            f"{dep.revision}"
        )
    elif head != dep.revision:
        problems.append(
            f"{dep.id}: HEAD is {head}, pinned revision is {dep.revision}"
        )

    for rel, expected in dep.files:
        path = checkout / rel
        if not path.is_file():
            problems.append(f"{dep.id}: missing file {rel}")
            continue
        actual = sha256_file(path)
        if actual != expected:
            problems.append(f"{dep.id}: {rel} sha256 is {actual}, pinned {expected}")

    return problems


def parse_checkouts(values: list[str], known: set[str]) -> dict[str, Path]:
    checkouts: dict[str, Path] = {}
    for value in values:
        if "=" not in value:
            raise SystemExit(f"--checkout expects <id>=<path>, got {value!r}")
        ident, _, raw = value.partition("=")
        if ident not in known:
            raise SystemExit(
                f"--checkout {ident}: unknown dependency (known: {', '.join(sorted(known))})"
            )
        checkouts[ident] = Path(os.path.expanduser(raw)).resolve()
    return checkouts


def cmd_verify(args: argparse.Namespace) -> int:
    pin_path = Path(args.pin)
    deps = load_pins(pin_path)
    known = {dep.id for dep in deps}
    checkouts = parse_checkouts(args.checkout, known)

    if args.require_all:
        missing = sorted(known - set(checkouts))
        if missing:
            print(f"FAIL: no checkout given for {', '.join(missing)}")
            return 1

    failed = False
    for dep in deps:
        if dep.id not in checkouts:
            print(f"SKIP {dep.id}: no checkout on the command line")
            continue
        problems = verify_dependency(dep, checkouts[dep.id])
        label = dep.tag or dep.revision[:12]
        if problems:
            failed = True
            print(f"FAIL {dep.id} ({label})")
            for problem in problems:
                print(f"  {problem}")
        else:
            print(f"OK   {dep.id} ({label}) {dep.revision}")

    return 1 if failed else 0


def run_git(args: list[str], cwd: Path | None = None) -> None:
    cmd = ["git"] + (["-C", str(cwd)] if cwd else []) + args
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)


def cmd_fetch(args: argparse.Namespace) -> int:
    """Clone each pinned dependency at its exact revision into --dest.

    This is a convenience for a fresh machine, not an automated re-pin: it only
    ever fetches the revision written in deps/deps.toml.
    """
    deps = load_pins(Path(args.pin))
    known = {dep.id for dep in deps}
    wanted = set(args.id or [])
    unknown = wanted - known
    if unknown:
        print(f"FAIL: unknown dependency {', '.join(sorted(unknown))}")
        return 1

    dest = Path(args.dest).expanduser().resolve()
    dest.mkdir(parents=True, exist_ok=True)

    failed = False
    for dep in deps:
        if wanted and dep.id not in wanted:
            continue
        target = dest / dep.id
        if target.exists():
            print(f"SKIP {dep.id}: {target} already exists (verify it instead)")
            continue
        print(f"FETCH {dep.id}: {dep.repository} @ {dep.revision[:12]}")
        try:
            run_git(["init", "--quiet", str(target)])
            run_git(["remote", "add", "origin", dep.repository], target)
            try:
                run_git(["fetch", "--quiet", "--depth", "1", "origin", dep.revision], target)
            except subprocess.CalledProcessError:
                if not dep.tag:
                    raise
                run_git(["fetch", "--quiet", "--depth", "1", "origin", dep.tag], target)
            run_git(["checkout", "--quiet", "--detach", "FETCH_HEAD"], target)
        except (OSError, subprocess.CalledProcessError) as error:
            failed = True
            print(f"FAIL {dep.id}: fetch failed: {error}")
            continue

        problems = verify_dependency(dep, target)
        if problems:
            failed = True
            print(f"FAIL {dep.id} after fetch")
            for problem in problems:
                print(f"  {problem}")
        else:
            print(f"OK   {dep.id} {dep.revision}")

    return 1 if failed else 0


def cmd_selftest(_args: argparse.Namespace) -> int:
    """Exercise verify_dependency without git and without a real dependency."""
    failures: list[str] = []
    with tempfile.TemporaryDirectory() as raw:
        root = Path(raw)
        sample = root / "sample.c"
        sample.write_text("/* pinned */\n", encoding="utf-8")
        good_hash = sha256_file(sample)

        dep = Dependency(
            id="fixture",
            repository="https://example.invalid/fixture",
            revision="0" * 40,
            tag="fixture-1",
            files=[("sample.c", good_hash)],
        )

        # A matching file, in a directory that is not a git checkout: the file
        # hash must still be checked, and the missing git metadata reported.
        problems = verify_dependency(dep, root)
        if len(problems) != 1 or "not a git checkout" not in problems[0]:
            failures.append(f"non-git checkout: unexpected problems {problems}")

        # A wrong hash must be reported, and must name the file.
        dep.files = [("sample.c", "0" * 64)]
        problems = verify_dependency(dep, root)
        if not any("sample.c sha256" in p for p in problems):
            failures.append(f"hash mismatch not reported: {problems}")

        # A missing file must be reported, not crash.
        dep.files = [("absent.c", good_hash)]
        problems = verify_dependency(dep, root)
        if not any("missing file absent.c" in p for p in problems):
            failures.append(f"missing file not reported: {problems}")

        # A missing checkout directory must be reported, not crash.
        problems = verify_dependency(dep, root / "nope")
        if not any("is not a directory" in p for p in problems):
            failures.append(f"missing checkout not reported: {problems}")

    if failures:
        for failure in failures:
            print(f"FAIL selftest: {failure}")
        return 1
    print("OK   selftest: hash match, hash mismatch, missing file, missing checkout")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--pin",
        default=str(Path(__file__).resolve().parent.parent / "deps" / "deps.toml"),
        help="pin file (default: deps/deps.toml)",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    verify = sub.add_parser("verify", help="check local checkouts against the pin")
    verify.add_argument(
        "--checkout",
        action="append",
        default=[],
        metavar="ID=PATH",
        help="dependency checkout to check, repeatable",
    )
    verify.add_argument(
        "--require-all",
        action="store_true",
        help="fail when a pinned dependency has no checkout on the command line",
    )
    verify.set_defaults(func=cmd_verify)

    selftest = sub.add_parser("selftest", help="run the tool's own tests")
    selftest.set_defaults(func=cmd_selftest)

    fetch = sub.add_parser(
        "fetch", help="clone pinned dependencies at their exact revisions"
    )
    fetch.add_argument(
        "--dest",
        default=str(Path(__file__).resolve().parent.parent / "third_party"),
        help="destination directory (default: third_party/)",
    )
    fetch.add_argument(
        "--id",
        action="append",
        default=[],
        metavar="ID",
        help="fetch only this dependency, repeatable",
    )
    fetch.set_defaults(func=cmd_fetch)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
