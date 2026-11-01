/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Publish policy for the Metal plugin. See NativeMaliPublish.h.
 *
 * The token separators match AdrenoMetal's listedClient(): space, comma, tab,
 * CR and LF. Names are compared as whole tokens, so "backboardd2" never
 * matches "backboardd".
 */
#include "NativeMaliPublish.h"

#include <string.h>

namespace {

bool is_separator(char character) {
    return character == ' ' || character == ',' || character == '\t' ||
           character == '\r' || character == '\n';
}

} /* namespace */

bool NativeMaliClientListed(const char *list_text, const char *process_name) {
    if (list_text == nullptr || process_name == nullptr || process_name[0] == '\0') {
        return false;
    }

    const size_t name_length = strlen(process_name);
    const char *cursor = list_text;
    while (*cursor != '\0') {
        while (*cursor != '\0' && is_separator(*cursor)) {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }

        const char *start = cursor;
        while (*cursor != '\0' && !is_separator(*cursor)) {
            ++cursor;
        }
        const size_t token_length = static_cast<size_t>(cursor - start);

        if (token_length == 1 && start[0] == '*') {
            return true;
        }
        if (token_length == name_length && memcmp(start, process_name, name_length) == 0) {
            return true;
        }
    }
    return false;
}

NativeMaliPublishDecision NativeMaliPublish(bool marker_present,
                                            const char *list_text,
                                            const char *process_name,
                                            bool device_ready) {
    if (!marker_present) {
        return NativeMaliPublishDecision::RefuseNotRequested;
    }
    if (!NativeMaliClientListed(list_text, process_name)) {
        return NativeMaliPublishDecision::RefuseClientNotListed;
    }
    if (!device_ready) {
        return NativeMaliPublishDecision::RefuseDeviceNotReady;
    }
    return NativeMaliPublishDecision::Publish;
}

const char *NativeMaliPublishDecisionName(NativeMaliPublishDecision decision) {
    switch (decision) {
        case NativeMaliPublishDecision::Publish: return "publish";
        case NativeMaliPublishDecision::RefuseNotRequested: return "refuse-not-requested";
        case NativeMaliPublishDecision::RefuseClientNotListed: return "refuse-client-not-listed";
        case NativeMaliPublishDecision::RefuseDeviceNotReady: return "refuse-device-not-ready";
    }
    return "refuse-unknown";
}
