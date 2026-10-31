#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Build AdrenoMetal's Metal bridge (NativeMetalBridge.rs) on the host against
# the pinned metal2vulkan, applying the pinned patch series first.
#
#   METAL2VULKAN_DIR=... ADRENOMETAL_DIR=... tools/build-bridge-host.sh
#
# Both checkouts must be at the revisions in deps/deps.toml and the
# metal2vulkan checkout must be pristine: this script patches it in place and
# refuses to stack a second series. LLVM's llvm-as (21 or newer) is required to
# assemble the two AIR fixtures; set LLVM_AS if it is not on PATH or in
# /opt/homebrew/opt/llvm/bin.
#
# The output is build/bridge-host/ (gitignored). Nothing is installed, and this
# is a compile check: the bridge's own tests are not run here.

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
adrenometal=${ADRENOMETAL_DIR:?set ADRENOMETAL_DIR to an AdrenoMetal checkout}
metal2vulkan=${METAL2VULKAN_DIR:?set METAL2VULKAN_DIR to a pristine metal2vulkan checkout}

llvm_as=${LLVM_AS:-}
if [ -z "$llvm_as" ]; then
    candidate=$(command -v llvm-as 2>/dev/null || true)
    if [ -n "$candidate" ]; then
        llvm_as=$candidate
    elif [ -x /opt/homebrew/opt/llvm/bin/llvm-as ]; then
        llvm_as=/opt/homebrew/opt/llvm/bin/llvm-as
    fi
fi
if [ -z "$llvm_as" ]; then
    echo "llvm-as (LLVM 21 or newer) not found; set LLVM_AS" >&2
    exit 1
fi

python3 - "$root/deps/deps.toml" "$metal2vulkan" "$adrenometal" <<'PY'
import subprocess
import sys
import tomllib

pin_path, *checkouts = sys.argv[1:]
pins = {entry["id"]: entry["revision"]
        for entry in tomllib.load(open(pin_path, "rb"))["dependency"]}
for ident, path in zip(("metal2vulkan", "adrenometal"), checkouts):
    head = subprocess.check_output(["git", "-C", path, "rev-parse", "HEAD"], text=True).strip()
    if head != pins[ident]:
        raise SystemExit(f"{ident}: HEAD {head} is not the pinned {pins[ident]}")
PY

if [ -n "$(git -C "$metal2vulkan" status --porcelain)" ]; then
    echo "metal2vulkan checkout is not pristine; refusing to stack patches" >&2
    exit 1
fi

patches=0
for patch in "$adrenometal"/bridge/patches/*.patch; do
    git -C "$metal2vulkan" apply "$patch"
    patches=$((patches + 1))
done
echo "applied $patches patch(es) from $adrenometal/bridge/patches"

out="$root/build/bridge-host"
rm -rf "$out"
mkdir -p "$out"

fixtures="$metal2vulkan/crates/metal2vulkan-validation/fixtures/public"
"$llvm_as" -o "$out/runtime_examples.air" "$fixtures/examples/runtime_examples.ll"
"$llvm_as" -o "$out/function_constant.air" "$fixtures/kernel_function_constant_gate_fold.ll"

# The runtime hashes this into its shader-cache key; a deterministic value is
# all a compile check needs.
runtime_id=$(python3 -c 'import hashlib,sys; print(hashlib.sha256((":".join(sys.argv[1:])).encode()).hexdigest()[:32])' \
    "$(git -C "$metal2vulkan" rev-parse HEAD)" "$(git -C "$adrenometal" rev-parse HEAD)")

cat > "$out/Cargo.toml" <<EOF
[package]
name = "malimetal-bridge-host-check"
version = "0.1.0"
edition = "2024"
rust-version = "1.98"

[workspace]

[lib]
crate-type = ["staticlib"]
path = "$adrenometal/bridge/NativeMetalBridge.rs"

[dependencies]
metal2vulkan = { path = "$metal2vulkan/crates/metal2vulkan", features = ["ash"] }
metal2vulkan-runtime = { path = "$metal2vulkan/crates/metal2vulkan-runtime" }
libc = "0.2"
sha2 = { version = "0.10", features = ["asm"] }
EOF

NMB_RUNTIME_ID="$runtime_id" \
NMB_FIXTURE_AIR="$out/runtime_examples.air" \
NMB_FC_AIR="$out/function_constant.air" \
    cargo build --manifest-path "$out/Cargo.toml"

echo "OK   bridge compiled against metal2vulkan $(git -C "$metal2vulkan" rev-parse --short HEAD), $patches patches"
