/* SPDX-License-Identifier: GPL-2.0-only
 *
 * The imported Metal object ABI must also be usable from C++ with the same
 * linkage. Compiled with -std=c++17 -fsyntax-only.
 */
#include "NativeMetalObjects.h"

#include <type_traits>

static_assert(std::is_same_v<decltype(&nmb_device_open), NmbDevice *(*)(const char *, const char *)>,
              "nmb_device_open signature");
static_assert(std::is_same_v<decltype(&nmb_device_release), void (*)(NmbDevice *)>,
              "nmb_device_release signature");
static_assert(std::is_same_v<decltype(&nmb_last_error), const char *(*)()>,
              "nmb_last_error signature");

int main() {
    /* Referencing the symbol keeps the declarations from being unused; the
     * static_asserts above are the real check. */
    const char *(*const last_error)(void) = nmb_last_error;
    return last_error == nullptr ? 1 : 0;
}
