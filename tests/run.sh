#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Host checks that need neither a phone nor a GPU.
#
#   tests/run.sh
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

python3 tools/deps.py selftest
python3 tests/test_pins.py

# The ABI records are defined in Mali-G52. Generate the pinned copy, then
# compile the host-testable userspace code against exactly that copy.
python3 tools/sync-abi.py
mkdir -p build/tests
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -Ibuild/abi -Isrc/native_mali \
    src/native_mali/NativeMaliAbiTable.cpp tests/test_abi_table.cpp \
    -o build/tests/test_abi_table
build/tests/test_abi_table

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -Ibuild/abi -Isrc/native_mali \
    src/native_mali/NativeMaliAbiTable.cpp src/native_mali/NativeMaliClient.cpp \
    tests/test_native_mali_client.cpp -o build/tests/test_native_mali_client
build/tests/test_native_mali_client

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -Ibuild/abi -Isrc/native_mali \
    src/native_mali/NativeMaliIovaAllocator.cpp tests/test_iova_allocator.cpp \
    -o build/tests/test_iova_allocator
build/tests/test_iova_allocator

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -Ibuild/abi -Isrc/native_mali \
    src/native_mali/NativeMaliBoTable.cpp tests/test_bo_table.cpp \
    -o build/tests/test_bo_table
build/tests/test_bo_table

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -Ibuild/abi -Isrc/native_mali \
    src/native_mali/NativeMaliClient.cpp src/native_mali/NativeMaliAbiTable.cpp \
    src/native_mali/NativeMaliBoTable.cpp src/native_mali/NativeMaliIovaAllocator.cpp \
    src/native_mali/NativeMaliFenceTracker.cpp src/native_mali/NativeMaliDevice.cpp \
    tests/test_device_lifecycle.cpp -o build/tests/test_device_lifecycle
build/tests/test_device_lifecycle

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -Ibuild/abi -Isrc/native_mali \
    src/native_mali/NativeMaliFenceTracker.cpp tests/test_fence_tracker.cpp \
    -o build/tests/test_fence_tracker
build/tests/test_fence_tracker

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -Ibuild/abi -Isrc/native_mali -Itests \
    src/native_mali/NativeMaliClient.cpp src/native_mali/NativeMaliAbiTable.cpp \
    src/native_mali/NativeMaliBoTable.cpp src/native_mali/NativeMaliIovaAllocator.cpp \
    src/native_mali/NativeMaliFenceTracker.cpp src/native_mali/NativeMaliDevice.cpp \
    tests/test_fake_service.cpp -o build/tests/test_fake_service
build/tests/test_fake_service

if [ "$(uname -s)" = Darwin ]; then
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
        -Ibuild/abi -Isrc/native_mali -Isrc/transport \
        src/transport/NativeMaliIokitTransport.cpp \
        src/native_mali/NativeMaliClient.cpp src/native_mali/NativeMaliAbiTable.cpp \
        tests/test_iokit_transport.cpp -framework IOKit \
        -o build/tests/test_iokit_transport
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
