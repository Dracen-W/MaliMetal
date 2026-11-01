/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Whether to publish a Metal device, and to whom.
 *
 * This mirrors the decision AdrenoMetal's plugin makes with two files beside
 * the bundle (MIT, see NOTICE):
 *   NativeMaliMetal.publish   present  -> the published mode is requested
 *   NativeMaliMetal.clients   names the processes allowed to open a device
 *
 * The rules, kept identical on purpose:
 *   - no marker: never publish, leave the software renderer alone;
 *   - marker present but the client is not listed: refuse, no fallback device;
 *   - "*" in the list names every process;
 *   - names are separated by spaces, commas, tabs, CR or LF, and match exactly.
 *
 * The file system and the process name stay outside: this file is pure so the
 * rules can be tested without a bundle, a render server or a GPU.
 */
#ifndef MALIMETAL_NATIVE_MALI_PUBLISH_H
#define MALIMETAL_NATIVE_MALI_PUBLISH_H

#include <stddef.h>

enum class NativeMaliPublishDecision {
    Publish = 0,
    RefuseNotRequested,   /* no marker beside the bundle */
    RefuseClientNotListed,/* marker present, this process is not in the list */
    RefuseDeviceNotReady, /* listed and requested, but the device did not open */
};

/* True when `process_name` appears in `list_text` as a whole name, or when the
 * list contains "*". An empty list allows nobody. */
bool NativeMaliClientListed(const char *list_text, const char *process_name);

/* Decide in this order: requested, listed, device ready. A later stage never
 * turns an earlier refusal into a publish. */
NativeMaliPublishDecision NativeMaliPublish(bool marker_present,
                                            const char *list_text,
                                            const char *process_name,
                                            bool device_ready);

const char *NativeMaliPublishDecisionName(NativeMaliPublishDecision decision);

#endif /* MALIMETAL_NATIVE_MALI_PUBLISH_H */
