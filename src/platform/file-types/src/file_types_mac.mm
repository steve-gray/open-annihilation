// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// macOS: whether this copy is the default opener of each file type, and making it so, or
// handing the type back. Both questions use Launch Services on every version. The calls are
// deprecated and still present; this file is the only place that warning is accepted. The
// bundle's declaration is left as it is. After a change the opener is read back, and a
// change the read does not show is reported.

#include "apple_handlers.hpp"

#import <CoreServices/CoreServices.h>
#import <Foundation/Foundation.h>

#include <string>

// LSCopyDefaultRoleHandlerForContentType and LSSetDefaultRoleHandlerForContentType are
// deprecated from macOS 12 and are the calls that answer at once. The replacement that
// takes a completion handler does not finish when the opener would change.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace oa::platform::file_types {
namespace {

/// A content type as Foundation holds a string that may not end in a zero.
NSString* text_of(std::string_view text) {
    return [[NSString alloc] initWithBytes:text.data()
                                    length:text.size()
                                  encoding:NSUTF8StringEncoding];
}

/// `text` as UTF-8, or empty when it is not.
std::string utf8_of(NSString* text) {
    if (text == nil)
        return {};
    const char* bytes = text.UTF8String;
    return bytes == nullptr ? std::string{} : std::string(bytes);
}

/// Reports whether two bundle identifiers name the same program, ignoring ASCII case.
bool same_bundle(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size())
        return false;
    const auto lower = [](unsigned char letter) noexcept {
        return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a')
                                              : static_cast<char>(letter);
    };
    for (std::size_t index = 0; index < left.size(); ++index)
        if (lower(static_cast<unsigned char>(left[index])) !=
            lower(static_cast<unsigned char>(right[index])))
            return false;
    return true;
}

/// The default opener of `content_type`. An empty handler with an empty `failure` means
/// nothing is the opener.
Handler opener_of(std::string_view content_type, std::string& failure) {
    NSString* identifier = text_of(content_type);
    if (identifier == nil) {
        failure = "unknown content type " + std::string(content_type);
        return {};
    }
    CFStringRef owner =
        LSCopyDefaultRoleHandlerForContentType((__bridge CFStringRef)identifier, kLSRolesAll);
    if (owner == nullptr)
        return {};
    Handler handler;
    handler.bundle_id = utf8_of(CFBridgingRelease(owner));
    return handler;
}

Handler read_handler(void* /*context*/, std::string_view content_type, std::string& failure) {
    return opener_of(content_type, failure);
}

bool write_handler(
    void* /*context*/, std::string_view content_type, const Handler& handler, std::string& failure
) {
    if (handler.bundle_id.empty()) {
        failure = "no program opens " + std::string(content_type);
        return false;
    }
    NSString* identifier = text_of(content_type);
    NSString* owner = text_of(handler.bundle_id);
    if (identifier == nil || owner == nil) {
        failure = "unknown content type " + std::string(content_type);
        return false;
    }
    const OSStatus status = LSSetDefaultRoleHandlerForContentType(
        (__bridge CFStringRef)identifier, kLSRolesAll, (__bridge CFStringRef)owner
    );
    if (status != noErr) {
        failure = "the system refused (" + std::to_string(static_cast<int>(status)) + ")";
        return false;
    }
    // The call answers at once. The opener afterwards is what a read reports, and a set
    // the read does not show did not take.
    const Handler now = opener_of(content_type, failure);
    if (!failure.empty())
        return false;
    if (!same_bundle(now.bundle_id, handler.bundle_id)) {
        failure = "the change did not take";
        return false;
    }
    return true;
}

} // namespace

HandlerCalls apple_handler_calls() {
    HandlerCalls calls;
    calls.default_handler = &read_handler;
    calls.set_handler = &write_handler;
    return calls;
}

Handler apple_running_application() {
    NSBundle* bundle = [NSBundle mainBundle];
    NSString* identifier = bundle.bundleIdentifier;
    NSURL* url = bundle.bundleURL;
    if (identifier.length == 0 || url == nil)
        return {};
    // A tool that is not a bundle reports the folder that holds it. Only an application
    // bundle is an opener the system can be asked to prefer.
    if (![url.pathExtension.lowercaseString isEqualToString:@"app"])
        return {};
    Handler application;
    application.bundle_id = utf8_of(identifier);
    application.application_url = utf8_of(url.absoluteString);
    return application;
}

} // namespace oa::platform::file_types

#pragma clang diagnostic pop
