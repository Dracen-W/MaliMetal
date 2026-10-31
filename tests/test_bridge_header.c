/* SPDX-License-Identifier: GPL-2.0-only
 *
 * The imported Metal object ABI must be usable from plain C. This file is
 * compiled with -std=c11 -fsyntax-only; it links nothing and calls nothing.
 */
#include "NativeMetalObjects.h"

/* Taking the addresses of a few declarations proves the header declares them
 * with the documented signature and that the extern "C" guard does not leak. */
static NmbDevice *(*const open_device)(const char *, const char *) = nmb_device_open;
static void (*const release_device)(NmbDevice *) = nmb_device_release;
static const char *(*const last_error)(void) = nmb_last_error;

int main(void)
{
    return open_device == 0 || release_device == 0 || last_error == 0;
}
