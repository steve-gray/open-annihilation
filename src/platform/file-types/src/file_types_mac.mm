// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// macOS: whether this copy is the default opener of each file type, and making it so, or
// handing the type back. macOS 12 and later names an application by its file URL. Before
// that, the same questions take a bundle identifier. The bundle's declaration is left as
// it is.

#include "apple_handlers.hpp"

#import <AppKit/AppKit.h>
#import <CoreServices/CoreServices.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <chrono>
#include <string>

namespace oa::platform::file_types {
namespace {

/// How long a default-opener change may take to answer.
constexpr std::chrono::seconds kAnswerLimit{5};

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

/// The bundle identifier of the program at `url`.
std::string bundle_id_of(NSURL* url) {
    if (url == nil)
        return {};
    return utf8_of([NSBundle bundleWithURL:url].bundleIdentifier);
}

/// The default opener of `content_type`, on macOS 12 and later.
API_AVAILABLE(macos(12.0))

Handler opener_now(std::string_view content_type, std::string& failure) {
    UTType* type = [UTType typeWithIdentifier:text_of(content_type)];
    if (type == nil) {
        failure = "unknown content type " + std::string(content_type);
        return {};
    }
    NSURL* url = [[NSWorkspace sharedWorkspace] URLForApplicationToOpenContentType:type];
    if (url == nil)
        return {};
    Handler handler;
    handler.bundle_id = bundle_id_of(url);
    handler.application_url = utf8_of(url.absoluteString);
    return handler;
}

/// The default opener of `content_type`, before macOS 12.
Handler opener_before(std::string_view content_type, std::string& failure) {
    NSString* identifier = text_of(content_type);
    if (identifier == nil) {
        failure = "unknown content type " + std::string(content_type);
        return {};
    }
    Handler handler;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CFStringRef owner =
        LSCopyDefaultRoleHandlerForContentType((__bridge CFStringRef)identifier, kLSRolesAll);
#pragma clang diagnostic pop
    if (owner == nullptr)
        return {};
    handler.bundle_id = utf8_of(CFBridgingRelease(owner));
    return handler;
}

/// Waits for `gate`, pumping the run loop when this is the main thread, so a completion
/// that comes back there can run. Sets `failure` when the wait runs out.
bool wait_for(dispatch_semaphore_t gate, std::string& failure) {
    const auto deadline = std::chrono::steady_clock::now() + kAnswerLimit;
    if ([NSThread isMainThread]) {
        while (dispatch_semaphore_wait(gate, DISPATCH_TIME_NOW) != 0) {
            if (std::chrono::steady_clock::now() >= deadline) {
                failure = "the system did not answer";
                return false;
            }
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                     beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
        }
        return true;
    }
    const auto left = deadline - std::chrono::steady_clock::now();
    const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(left).count();
    if (nanoseconds <= 0 ||
        dispatch_semaphore_wait(gate, dispatch_time(DISPATCH_TIME_NOW, nanoseconds)) != 0) {
        failure = "the system did not answer";
        return false;
    }
    return true;
}

/// Makes `handler` the default opener of `content_type` on macOS 12 and later.
API_AVAILABLE(macos(12.0))

bool set_opener_now(std::string_view content_type, const Handler& handler, std::string& failure) {
    UTType* type = [UTType typeWithIdentifier:text_of(content_type)];
    if (type == nil) {
        failure = "unknown content type " + std::string(content_type);
        return false;
    }
    NSURL* url = nil;
    if (!handler.application_url.empty())
        url = [NSURL URLWithString:text_of(handler.application_url)];
    if (url == nil && !handler.bundle_id.empty()) {
        url = [[NSWorkspace sharedWorkspace]
            URLForApplicationWithBundleIdentifier:text_of(handler.bundle_id)];
    }
    if (url == nil) {
        failure = "no program opens " + std::string(content_type);
        return false;
    }
    __block NSString* message = nil;
    dispatch_semaphore_t gate = dispatch_semaphore_create(0);
    [[NSWorkspace sharedWorkspace] setDefaultApplicationAtURL:url
                                            toOpenContentType:type
                                            completionHandler:^(NSError* problem) {
                                                if (problem != nil)
                                                    message = problem.localizedDescription;
                                                dispatch_semaphore_signal(gate);
                                            }];
    if (!wait_for(gate, failure))
        return false;
    if (message != nil) {
        failure = utf8_of(message);
        if (failure.empty())
            failure = "the system refused";
        return false;
    }
    return true;
}

/// Makes `handler` the default opener of `content_type` before macOS 12.
bool set_opener_before(
    std::string_view content_type, const Handler& handler, std::string& failure
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
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    const OSStatus status = LSSetDefaultRoleHandlerForContentType(
        (__bridge CFStringRef)identifier, kLSRolesAll, (__bridge CFStringRef)owner
    );
#pragma clang diagnostic pop
    if (status != noErr) {
        failure = "the system refused (" + std::to_string(static_cast<int>(status)) + ")";
        return false;
    }
    return true;
}

Handler read_handler(void* /*context*/, std::string_view content_type, std::string& failure) {
    if (@available(macOS 12.0, *))
        return opener_now(content_type, failure);
    return opener_before(content_type, failure);
}

bool write_handler(
    void* /*context*/, std::string_view content_type, const Handler& handler, std::string& failure
) {
    if (@available(macOS 12.0, *))
        return set_opener_now(content_type, handler, failure);
    return set_opener_before(content_type, handler, failure);
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
