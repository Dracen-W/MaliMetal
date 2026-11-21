#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Cross-build NativeMetalBridge.rs (AdrenoMetal cc0a4e8 + metal2vulkan 46e7cc96) for aarch64-apple-ios.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
adrenometal=${ADRENOMETAL_DIR:?set ADRENOMETAL_DIR to an AdrenoMetal checkout}
metal2vulkan=${METAL2VULKAN_DIR:?set METAL2VULKAN_DIR to a pristine metal2vulkan checkout}

llvm_as=${LLVM_AS:-}
if [ -z "$llvm_as" ]; then
    if command -v llvm-as >/dev/null 2>&1; then
        llvm_as=$(command -v llvm-as)
    elif [ -x /opt/homebrew/opt/llvm/bin/llvm-as ]; then
        llvm_as=/opt/homebrew/opt/llvm/bin/llvm-as
    fi
fi

if [ -z "$(git -C "$metal2vulkan" status --porcelain)" ]; then
    # Apply 0063 only (0064 is macOS-only and fails on iOS copied.rs:1252).
    git -C "$metal2vulkan" apply "$adrenometal/bridge/patches/0063-main-optimizations-on-development-46e7cc96.patch"
fi

out="$root/build/bridge-ios"
rm -rf "$out"
mkdir -p "$out"

fixtures="$metal2vulkan/crates/metal2vulkan-validation/fixtures/public"
"$llvm_as" -o "$out/runtime_examples.air" "$fixtures/examples/runtime_examples.ll"
"$llvm_as" -o "$out/function_constant.air" "$fixtures/kernel_function_constant_gate_fold.ll"
if [ -f "$adrenometal/shaders/scanout_convert.ll" ]; then
    "$llvm_as" -o "$out/scanout_convert.air" "$adrenometal/shaders/scanout_convert.ll"
else
    cp "$out/function_constant.air" "$out/scanout_convert.air"
fi

runtime_id=$(python3 -c 'import hashlib,sys; print(hashlib.sha256((":".join(sys.argv[1:])).encode()).hexdigest()[:32])' \
    "$(git -C "$metal2vulkan" rev-parse HEAD)" "$(git -C "$adrenometal" rev-parse HEAD)")

cat > "$out/Cargo.toml" <<EOF
[package]
name = "malimetal-bridge-ios"
version = "0.1.0"
edition = "2024"

[workspace]

[lib]
crate-type = ["staticlib"]
path = "$adrenometal/bridge/NativeMetalBridge.rs"

[dependencies]
metal2vulkan = { path = "$metal2vulkan/crates/metal2vulkan", features = ["ash"] }
metal2vulkan-runtime = { path = "$metal2vulkan/crates/metal2vulkan-runtime" }
libc = "0.2"
sha2 = { version = "0.10", features = ["asm"] }

[profile.release]
# Optimize for speed (not size) so metal2vulkan AIR->SPIR-V translation and
# command encoding get full inlining and auto-vectorization.
opt-level = 3
panic = "unwind"
debug = false
strip = "symbols"
lto = "fat"
codegen-units = 1
EOF

component_id=$(git -C "$adrenometal" rev-parse HEAD)

NMB_COMPONENT_ID="$component_id" \
NMB_RUNTIME_ID="$runtime_id" \
NMB_FIXTURE_AIR="$out/runtime_examples.air" \
NMB_FC_AIR="$out/function_constant.air" \
NMB_SCANOUT_AIR="$out/scanout_convert.air" \
IPHONEOS_DEPLOYMENT_TARGET="17.0" \
cargo build --release --manifest-path "$out/Cargo.toml" --target aarch64-apple-ios --target-dir "$out/target"

cp "$out/target/aarch64-apple-ios/release/libmalimetal_bridge_ios.a" "$root/build/libmali_metal_bridge.a"
echo "Built $root/build/libmali_metal_bridge.a"
