// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// HTTPS on macOS and iOS: the system's URL loading. Trust uses the system's
// evaluation; a certificate the system rejects fails the fetch and is never
// retried as HTTP. OA ships no certificate list.

#import <Foundation/Foundation.h>

#include "hop_body.hpp"
#include "tls.hpp"

#include "oa/formats/url.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace oa::netgame::http {

/// What the delegate and the waiting thread share. The waiting thread reads
/// it only after `finished` is set and the delegate queue has gone idle.
struct HopState {
    const Request* request = nullptr;
    BodySink* sink = nullptr;
    std::string final_url;
    Method method = Method::get;
    const std::vector<uint8_t>* body = nullptr;
    FetchClock clock{};
    HttpsExchange exchange;
    std::unique_ptr<HopBody> hop;
    std::mutex mu;
    bool finished = false;
    dispatch_semaphore_t semaphore = nullptr;
    /// 1 when the caller cancelled, 2 when the caller's clock ran out.
    std::atomic<int> stop{0};
};

/// Records a response's status and headers once.
///
/// @param state the hop
/// @param response the system's response
void remember_response(const std::shared_ptr<HopState>& state, NSURLResponse* response) {
    if (![response isKindOfClass:[NSHTTPURLResponse class]])
        return;
    NSHTTPURLResponse* http = (NSHTTPURLResponse*)response;
    if (state->exchange.status != 0)
        return;
    state->exchange.status = static_cast<int>(http.statusCode);
    NSDictionary* fields = http.allHeaderFields;
    for (id key in fields) {
        if (![key isKindOfClass:[NSString class]])
            continue;
        id value = fields[key];
        NSString* text = nil;
        if ([value isKindOfClass:[NSString class]])
            text = (NSString*)value;
        else
            continue;
        const char* name = [(NSString*)key UTF8String];
        const char* header_value = [text UTF8String];
        if (name == nullptr || header_value == nullptr)
            continue;
        Header header;
        header.name = name;
        header.value = header_value;
        state->exchange.headers.push_back(std::move(header));
    }
}

/// Starts the sink for a response that is not a redirect.
///
/// @param state the hop
/// @return false when the hop has already failed
bool begin_body(const std::shared_ptr<HopState>& state) {
    if (state->hop)
        return state->exchange.failure == Failure::none;
    state->hop =
        std::make_unique<HopBody>(*state->request, *state->sink, state->exchange, state->final_url);
    return state->hop->take_head();
}

/// Maps an URL-loading error. Certificate errors stay certificate errors.
///
/// @param error the error; null is not an error
/// @param state the hop
void map_error(NSError* error, const std::shared_ptr<HopState>& state) {
    if (error == nil || state->exchange.failure != Failure::none)
        return;
    if (![error.domain isEqualToString:NSURLErrorDomain]) {
        state->exchange.failure = Failure::connection_lost;
        state->exchange.detail = "the secure connection failed";
        return;
    }
    const NSInteger code = error.code;
    if (code == NSURLErrorCancelled) {
        const int stop = state->stop.load();
        if (stop == 2) {
            state->exchange.failure = Failure::timed_out;
            state->exchange.detail = "the time limit ran out";
        } else {
            state->exchange.failure = Failure::cancelled;
            state->exchange.detail = "the fetch was cancelled";
        }
        return;
    }
    if (code == NSURLErrorTimedOut) {
        state->exchange.failure = Failure::timed_out;
        state->exchange.detail = "the time limit ran out";
        return;
    }
    if (code == NSURLErrorCannotFindHost || code == NSURLErrorDNSLookupFailed) {
        state->exchange.failure = Failure::cannot_resolve;
        state->exchange.detail = "the name could not be resolved";
        return;
    }
    if (code == NSURLErrorCannotConnectToHost) {
        state->exchange.failure = Failure::cannot_connect;
        state->exchange.detail = "the connection could not be opened";
        return;
    }
    if (code == NSURLErrorNetworkConnectionLost) {
        state->exchange.failure = Failure::connection_lost;
        state->exchange.detail = "the connection closed early";
        return;
    }
    if (code == NSURLErrorServerCertificateHasBadDate) {
        state->exchange.failure = Failure::certificate;
        state->exchange.detail = "the server's certificate has expired or is not yet valid";
        return;
    }
    if (code == NSURLErrorServerCertificateNotYetValid) {
        state->exchange.failure = Failure::certificate;
        state->exchange.detail = "the server's certificate is not yet valid";
        return;
    }
    if (code == NSURLErrorServerCertificateUntrusted ||
        code == NSURLErrorServerCertificateHasUnknownRoot) {
        state->exchange.failure = Failure::certificate;
        state->exchange.detail = "the server's certificate is not trusted";
        return;
    }
    if (code == NSURLErrorSecureConnectionFailed || code == NSURLErrorClientCertificateRejected ||
        code == NSURLErrorClientCertificateRequired) {
        state->exchange.failure = Failure::certificate;
        state->exchange.detail = code == NSURLErrorSecureConnectionFailed
                                     ? "the secure connection failed"
                                     : "the server's certificate was rejected";
        return;
    }
    state->exchange.failure = Failure::connection_lost;
    state->exchange.detail = "the secure connection failed";
}

/// True when the caller's cancel flag or total clock has fired.
///
/// @param state the hop
/// @return 1 for cancel, 2 for the clock, 0 when the hop may continue
int caller_stop(const HopState& state) {
    if (state.clock.cancel != nullptr && state.clock.cancel->load())
        return 1;
    if (state.clock.timeouts.total_ms == 0)
        return 0;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - state.clock.started
    )
                             .count();
    if (elapsed >= static_cast<int64_t>(state.clock.timeouts.total_ms))
        return 2;
    return 0;
}

} // namespace oa::netgame::http

/// The session's delegate. It runs on the session's queue, never on the
/// thread that is blocked in fetch.
@interface OaHttpsDelegate : NSObject <NSURLSessionDataDelegate>
- (instancetype)initWithState:(std::shared_ptr<oa::netgame::http::HopState>)state;
@end

@implementation OaHttpsDelegate {
    std::shared_ptr<oa::netgame::http::HopState> state_;
}

- (instancetype)initWithState:(std::shared_ptr<oa::netgame::http::HopState>)state {
    self = [super init];
    if (self != nil)
        state_ = std::move(state);
    return self;
}

- (void)URLSession:(NSURLSession*)session
                   task:(NSURLSessionTask*)task
    didReceiveChallenge:(NSURLAuthenticationChallenge*)challenge
      completionHandler:
          (void (^)(NSURLSessionAuthChallengeDisposition, NSURLCredential*))completion {
    // The system's evaluation. A failed trust is a failure, not a credential
    // this code accepts, and not a retry over HTTP.
    (void)session;
    (void)task;
    (void)challenge;
    completion(NSURLSessionAuthChallengePerformDefaultHandling, nil);
}

- (void)URLSession:(NSURLSession*)session
                          task:(NSURLSessionTask*)task
    willPerformHTTPRedirection:(NSHTTPURLResponse*)response
                    newRequest:(NSURLRequest*)request
             completionHandler:(void (^)(NSURLRequest*))completion {
    (void)session;
    (void)task;
    (void)request;
    if (state_)
        oa::netgame::http::remember_response(state_, response);
    completion(nil);
}

- (void)URLSession:(NSURLSession*)session
              dataTask:(NSURLSessionDataTask*)task
    didReceiveResponse:(NSURLResponse*)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completion {
    (void)session;
    (void)task;
    if (!state_) {
        completion(NSURLSessionResponseCancel);
        return;
    }
    oa::netgame::http::remember_response(state_, response);
    if (!oa::netgame::http::begin_body(state_) || (state_->hop && state_->hop->redirect())) {
        completion(NSURLSessionResponseCancel);
        return;
    }
    completion(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession*)session
          dataTask:(NSURLSessionDataTask*)task
    didReceiveData:(NSData*)data {
    (void)session;
    (void)task;
    if (!state_ || !state_->hop || state_->hop->redirect() || data == nil)
        return;
    const auto* bytes = static_cast<const uint8_t*>(data.bytes);
    if (bytes == nullptr || data.length == 0)
        return;
    if (!state_->hop->take_bytes(
            std::span<const uint8_t>(bytes, static_cast<std::size_t>(data.length))
        ))
        [task cancel];
}

- (void)URLSession:(NSURLSession*)session
                    task:(NSURLSessionTask*)task
    didCompleteWithError:(NSError*)error {
    (void)session;
    (void)task;
    if (!state_)
        return;
    std::lock_guard<std::mutex> lock(state_->mu);
    if (state_->finished)
        return;
    oa::netgame::http::map_error(error, state_);
    if (state_->exchange.failure == oa::netgame::http::Failure::none && state_->hop &&
        !state_->hop->finish() && state_->exchange.failure == oa::netgame::http::Failure::none) {
        state_->exchange.failure = oa::netgame::http::Failure::decode_failed;
        state_->exchange.detail = "the body could not be decoded";
    }
    state_->finished = true;
    if (state_->semaphore != nullptr)
        dispatch_semaphore_signal(state_->semaphore);
}

@end

namespace oa::netgame::http {
namespace {

TlsIoResult socket_unused() {
    TlsIoResult result;
    result.kind = TlsIoResult::Kind::failed;
    result.failure = Failure::connection_lost;
    result.detail = "the secure connection failed";
    return result;
}

} // namespace

struct TlsSession {};

HttpsTransport probe_https_transport() noexcept {
    return HttpsTransport::platform;
}

TlsSession* tls_open(
    intptr_t /*fd*/,
    std::string_view /*host*/,
    bool /*ip_literal*/,
    Failure& failure,
    std::string& detail
) {
    failure = Failure::connection_lost;
    detail = "the secure connection failed";
    return nullptr;
}

void tls_close(TlsSession* session) noexcept {
    delete session;
}

TlsIoResult tls_handshake(TlsSession* /*session*/) {
    return socket_unused();
}

TlsIoResult tls_read(TlsSession* /*session*/, uint8_t* /*bytes*/, std::size_t /*size*/) {
    return socket_unused();
}

TlsIoResult tls_write(TlsSession* /*session*/, const uint8_t* /*bytes*/, std::size_t /*size*/) {
    return socket_unused();
}

HttpsExchange fetch_https_hop(
    const Request& request,
    const oa::formats::url::Url& url,
    Method method,
    const std::vector<uint8_t>& body,
    std::string_view user_agent,
    BodySink& sink,
    const FetchClock& clock
) {
    @autoreleasepool {
        auto state = std::make_shared<HopState>();
        state->request = &request;
        state->sink = &sink;
        state->final_url = oa::formats::url::url_text(url);
        state->method = method;
        state->body = &body;
        state->clock = clock;
        state->semaphore = dispatch_semaphore_create(0);

        const std::string text = state->final_url;
        NSString* address = [NSString stringWithUTF8String:text.c_str()];
        NSURL* target = address == nil ? nil : [NSURL URLWithString:address];
        if (target == nil) {
            HttpsExchange exchange;
            exchange.failure = Failure::bad_url;
            exchange.detail = "the address is not one OA can fetch";
            return exchange;
        }

        NSMutableURLRequest* call = [NSMutableURLRequest requestWithURL:target];
        if (method == Method::head)
            call.HTTPMethod = @"HEAD";
        else if (method == Method::post)
            call.HTTPMethod = @"POST";
        else
            call.HTTPMethod = @"GET";
        const NSTimeInterval idle =
            clock.timeouts.idle_ms == 0
                ? 60.0
                : static_cast<NSTimeInterval>(clock.timeouts.idle_ms) / 1000.0;
        call.timeoutInterval = idle;
        call.cachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
        [call setValue:[NSString stringWithUTF8String:std::string(user_agent).c_str()]
            forHTTPHeaderField:@"User-Agent"];
        [call setValue:@"*/*" forHTTPHeaderField:@"Accept"];
        if (request.accept_gzip)
            [call setValue:@"gzip" forHTTPHeaderField:@"Accept-Encoding"];
        if (request.range_from.has_value()) {
            const std::string range = "bytes=" + std::to_string(*request.range_from) + "-";
            [call setValue:[NSString stringWithUTF8String:range.c_str()]
                forHTTPHeaderField:@"Range"];
        }
        if (method == Method::post) {
            const std::string type = request.content_type.empty()
                                         ? std::string("application/octet-stream")
                                         : request.content_type;
            [call setValue:[NSString stringWithUTF8String:type.c_str()]
                forHTTPHeaderField:@"Content-Type"];
            if (!body.empty())
                call.HTTPBody = [NSData dataWithBytes:body.data() length:body.size()];
        }
        for (const Header& header : request.headers) {
            [call setValue:[NSString stringWithUTF8String:header.value.c_str()]
                forHTTPHeaderField:[NSString stringWithUTF8String:header.name.c_str()]];
        }

        NSURLSessionConfiguration* configuration =
            [NSURLSessionConfiguration ephemeralSessionConfiguration];
        configuration.HTTPCookieAcceptPolicy = NSHTTPCookieAcceptPolicyNever;
        configuration.HTTPShouldSetCookies = NO;
        configuration.HTTPCookieStorage = nil;
        configuration.URLCache = nil;
        configuration.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
        configuration.timeoutIntervalForRequest = idle;
        if (clock.timeouts.total_ms > 0) {
            configuration.timeoutIntervalForResource =
                static_cast<NSTimeInterval>(clock.timeouts.total_ms) / 1000.0;
        }

        NSOperationQueue* operations = [[NSOperationQueue alloc] init];
        operations.maxConcurrentOperationCount = 1;
        operations.name = @"oa.http.tls";
        OaHttpsDelegate* delegate = [[OaHttpsDelegate alloc] initWithState:state];
        NSURLSession* session = [NSURLSession sessionWithConfiguration:configuration
                                                              delegate:delegate
                                                         delegateQueue:operations];
        NSURLSessionDataTask* task = [session dataTaskWithRequest:call];
        [task resume];

        bool stopping = false;
        while (dispatch_semaphore_wait(
                   state->semaphore, dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC)
               ) != 0) {
            if (stopping)
                continue;
            const int stop = caller_stop(*state);
            if (stop != 0) {
                stopping = true;
                state->stop.store(stop);
                [task cancel];
            }
        }

        HttpsExchange exchange = state->exchange;
        [session finishTasksAndInvalidate];
        [operations waitUntilAllOperationsAreFinished];
        session = nil;
        delegate = nil;
        operations = nil;
        return exchange;
    }
}

} // namespace oa::netgame::http
