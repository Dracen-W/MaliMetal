#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Host checks that need neither a phone nor a GPU.
#
#   tests/run.sh          build and run every host test
#   tests/run.sh --san    the same with AddressSanitizer + UBSan
#
# Dependency checkouts are optional here. To include them:
#
#   MALI_DEPS_DIR=/path/with/mesa-and-metal2vulkan tests/run.sh
#
# where the directory contains one subdirectory per dependency id. The build
# (when it exists) must use `tools/deps.py verify --require-all` instead of this
# optional form.

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

sanitize=0
case "${1:-}" in
    "") ;;
    --san) sanitize=1 ;;
    *)
        printf 'usage: %s [--san]\n' "$0" >&2
        exit 2
        ;;
esac

cxxflags="-std=c++17 -Wall -Wextra -Werror"
if [ "$sanitize" -eq 1 ]; then
    cxxflags="$cxxflags -g -fsanitize=address,undefined -fno-omit-frame-pointer"
fi

# compile <output> <sources...>
compile() {
    output=$1
    shift
    # shellcheck disable=SC2086 # cxxflags is a deliberate flag list
    "${CXX:-c++}" $cxxflags -Ibuild/abi -Isrc/native_mali -Isrc/transport -Isrc/backend -Itests \
        "$@" -o "$output"
}

# run <name> <sources...>
run_test() {
    name=$1
    shift
    compile "build/tests/$name" "$@"
    "build/tests/$name"
}

python3 tools/deps.py selftest
python3 tests/test_pins.py

# The ABI records are defined in Mali-G52. Generate the pinned copy, then
# compile the host-testable userspace code against exactly that copy.
python3 tools/sync-abi.py
mkdir -p build/tests

abi_sources="src/native_mali/NativeMaliAbiTable.cpp"
client_sources="$abi_sources src/native_mali/NativeMaliClient.cpp src/native_mali/NativeMaliError.cpp"
lifecycle_sources="$client_sources src/native_mali/NativeMaliBoTable.cpp \
src/native_mali/NativeMaliIovaAllocator.cpp src/native_mali/NativeMaliFenceTracker.cpp \
src/native_mali/NativeMaliDevice.cpp"

# shellcheck disable=SC2086 # the source lists are deliberate word lists
run_test test_abi_table $abi_sources tests/test_abi_table.cpp
# shellcheck disable=SC2086
run_test test_native_mali_client $client_sources tests/test_native_mali_client.cpp
# shellcheck disable=SC2086
run_test test_iova_allocator src/native_mali/NativeMaliIovaAllocator.cpp \
    tests/test_iova_allocator.cpp
# shellcheck disable=SC2086
run_test test_bo_table src/native_mali/NativeMaliBoTable.cpp tests/test_bo_table.cpp
# shellcheck disable=SC2086
run_test test_fence_tracker src/native_mali/NativeMaliFenceTracker.cpp \
    tests/test_fence_tracker.cpp
# shellcheck disable=SC2086
run_test test_device_lifecycle $lifecycle_sources tests/test_device_lifecycle.cpp
# shellcheck disable=SC2086
run_test test_fake_service $lifecycle_sources tests/test_fake_service.cpp
# shellcheck disable=SC2086
run_test test_kmod_api $lifecycle_sources src/backend/native_mali_kmod.cpp \
    tests/test_kmod_api.cpp

if [ "$(uname -s)" = Darwin ]; then
    # shellcheck disable=SC2086
    compile build/tests/test_iokit_transport \
        src/transport/NativeMaliIokitTransport.cpp \
        src/native_mali/NativeMaliClient.cpp src/native_mali/NativeMaliAbiTable.cpp \
        src/native_mali/NativeMaliError.cpp \
        tests/test_iokit_transport.cpp -framework IOKit
    build/tests/test_iokit_transport
else
    printf '%s\n' "SKIP iokit transport: not an Apple platform"
fi

if [ -n "${MALI_DEPS_DIR:-}" ]; then
    python3 tools/deps.py verify --require-all \
        --checkout "mesa=$MALI_DEPS_DIR/mesa" \
        --checkout "metal2vulkan=$MALI_DEPS_DIR/metal2vulkan"
else
    printf '%s\n' "SKIP dependency checkouts (set MALI_DEPS_DIR to include them)"
fi
