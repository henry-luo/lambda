// webview_child_mac.mm — macOS WKWebView child window backend
//
// Creates a WKWebView as a subview of the GLFW window's NSView content view.
// The web view is positioned at the layout-computed coordinates and sized to match.
// macOS uses a flipped coordinate system (origin at bottom-left), so Y is inverted.

#ifdef __APPLE__

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <WebKit/WebKit.h>
#import <objc/runtime.h>
#include <unistd.h>

#include "webview.h"
#include "webview_handle_mac.h"

extern "C" {
#include "../lib/log.h"
#include "../lib/mem.h"
}

// Navigation delegate to track load completion
@interface LambdaWebViewDelegate : NSObject <WKNavigationDelegate>
@property (nonatomic, assign) WebViewHandle* handle;
@end

@implementation LambdaWebViewDelegate

- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
    log_info("webview navigation finished: %s", webView.URL.absoluteString.UTF8String);
}

- (void)webView:(WKWebView*)webView didFailNavigation:(WKNavigation*)navigation
      withError:(NSError*)error {
    log_error("webview navigation failed: %s", error.localizedDescription.UTF8String);
}

- (void)webView:(WKWebView*)webView
    didFailProvisionalNavigation:(WKNavigation*)navigation
      withError:(NSError*)error {
    log_error("webview provisional navigation failed: %s", error.localizedDescription.UTF8String);
}

@end

// ---------------------------------------------------------------------------
// lambda:// URL scheme handler — serves local files from the document's base directory
// ---------------------------------------------------------------------------

@interface LambdaSchemeHandler : NSObject <WKURLSchemeHandler>
@property (nonatomic, copy) NSString* base_path;  // base directory for resolving paths
@end

@implementation LambdaSchemeHandler

- (void)webView:(WKWebView*)webView startURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
    NSURL* url = urlSchemeTask.request.URL;
    NSString* path = url.path;  // e.g. /assets/chart.js from lambda://assets/chart.js

    // resolve relative to base path
    NSString* full_path = nil;
    if (_base_path && path.length > 0) {
        // strip leading / from path
        NSString* rel = [path hasPrefix:@"/"] ? [path substringFromIndex:1] : path;
        full_path = [_base_path stringByAppendingPathComponent:rel];
    }

    if (!full_path) {
        log_error("lambda:// scheme: cannot resolve path: %s", path.UTF8String);
        [urlSchemeTask didFailWithError:[NSError errorWithDomain:NSURLErrorDomain
                                                           code:NSURLErrorFileDoesNotExist
                                                       userInfo:nil]];
        return;
    }

    // security: prevent path traversal — ensure resolved path stays within base
    NSString* resolved = full_path.stringByResolvingSymlinksInPath;
    NSString* base_resolved = _base_path.stringByResolvingSymlinksInPath;
    if (![resolved hasPrefix:base_resolved]) {
        log_error("lambda:// scheme: path traversal blocked: %s", resolved.UTF8String);
        [urlSchemeTask didFailWithError:[NSError errorWithDomain:NSURLErrorDomain
                                                           code:NSURLErrorNoPermissionsToReadFile
                                                       userInfo:nil]];
        return;
    }

    NSData* data = [NSData dataWithContentsOfFile:full_path];
    if (!data) {
        log_error("lambda:// scheme: file not found: %s", full_path.UTF8String);
        [urlSchemeTask didFailWithError:[NSError errorWithDomain:NSURLErrorDomain
                                                           code:NSURLErrorFileDoesNotExist
                                                       userInfo:nil]];
        return;
    }

    // determine MIME type from extension
    NSString* ext = [full_path pathExtension].lowercaseString;
    NSString* mime = @"application/octet-stream";
    if ([ext isEqualToString:@"html"] || [ext isEqualToString:@"htm"])  mime = @"text/html";
    else if ([ext isEqualToString:@"css"])   mime = @"text/css";
    else if ([ext isEqualToString:@"js"])    mime = @"application/javascript";
    else if ([ext isEqualToString:@"json"])  mime = @"application/json";
    else if ([ext isEqualToString:@"png"])   mime = @"image/png";
    else if ([ext isEqualToString:@"jpg"] || [ext isEqualToString:@"jpeg"]) mime = @"image/jpeg";
    else if ([ext isEqualToString:@"gif"])   mime = @"image/gif";
    else if ([ext isEqualToString:@"svg"])   mime = @"image/svg+xml";
    else if ([ext isEqualToString:@"webp"])  mime = @"image/webp";
    else if ([ext isEqualToString:@"woff"])  mime = @"font/woff";
    else if ([ext isEqualToString:@"woff2"]) mime = @"font/woff2";
    else if ([ext isEqualToString:@"ttf"])   mime = @"font/ttf";
    else if ([ext isEqualToString:@"otf"])   mime = @"font/otf";

    NSURLResponse* response = [[NSURLResponse alloc] initWithURL:url
                                                        MIMEType:mime
                                           expectedContentLength:(NSInteger)data.length
                                                textEncodingName:nil];
    [urlSchemeTask didReceiveResponse:response];
    [urlSchemeTask didReceiveData:data];
    [urlSchemeTask didFinish];

    log_debug("lambda:// scheme: served %s (%lu bytes, %s)",
              full_path.UTF8String, (unsigned long)data.length, mime.UTF8String);
}

- (void)webView:(WKWebView*)webView stopURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
    // nothing to clean up for synchronous file reads
}

@end

// ---------------------------------------------------------------------------
// IPC message handler — receives window.__lambda.invoke() calls from web content
// ---------------------------------------------------------------------------

@interface LambdaScriptMessageHandler : NSObject <WKScriptMessageHandler>
@property (nonatomic, assign) WebViewHandle* handle;
@end

@implementation LambdaScriptMessageHandler

- (void)userContentController:(WKUserContentController*)controller
      didReceiveScriptMessage:(WKScriptMessage*)message {
    if (![message.name isEqualToString:@"lambda"]) return;

    NSString* body = nil;
    if ([message.body isKindOfClass:[NSString class]]) {
        body = (NSString*)message.body;
    }

    if (!body) {
        log_error("webview IPC: received non-string message");
        return;
    }

    log_info("webview IPC: received message: %s", body.UTF8String);

    // TODO: parse JSON { command, payload } and dispatch to Lambda runtime
    // For now, just log the message. Full wiring requires Lambda runtime integration.
}

@end

// ---------------------------------------------------------------------------
// Platform API implementation
// ---------------------------------------------------------------------------

WebViewHandle* webview_platform_create(GLFWwindow* window,
                                       float x, float y, float w, float h,
                                       float pixel_ratio) {
    @autoreleasepool {
        NSWindow* ns_window = glfwGetCocoaWindow(window);
        if (!ns_window) {
            log_error("webview_platform_create: failed to get NSWindow from GLFW");
            return nullptr;
        }

        NSView* content_view = [ns_window contentView];
        if (!content_view) {
            log_error("webview_platform_create: NSWindow has no content view");
            return nullptr;
        }

        // configure the web view
        WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
        WKWebpagePreferences* page_prefs = [[WKWebpagePreferences alloc] init];
        page_prefs.allowsContentJavaScript = YES;
        config.defaultWebpagePreferences = page_prefs;

        // register lambda:// URL scheme handler for local asset serving
        LambdaSchemeHandler* scheme_handler = [[LambdaSchemeHandler alloc] init];
        // resolve base path from current working directory
        char cwd_buf[4096];
        if (getcwd(cwd_buf, sizeof(cwd_buf))) {
            scheme_handler.base_path = [NSString stringWithUTF8String:cwd_buf];
        } else {
            scheme_handler.base_path = [[NSFileManager defaultManager] currentDirectoryPath];
        }
        [config setURLSchemeHandler:scheme_handler forURLScheme:@"lambda"];

        // register IPC message handler for window.__lambda.invoke()
        LambdaScriptMessageHandler* ipc_handler = [[LambdaScriptMessageHandler alloc] init];
        [config.userContentController addScriptMessageHandler:ipc_handler name:@"lambda"];

        // inject the Lambda IPC bridge script
        NSString* bridge_js = @
            "window.__lambda = {"
            "  invoke: function(command, payload) {"
            "    window.webkit.messageHandlers.lambda.postMessage("
            "      JSON.stringify({ command: command, payload: payload })"
            "    );"
            "  },"
            "  _callbacks: {},"
            "  _nextId: 1,"
            "  on: function(event, callback) {"
            "    this._callbacks[event] = callback;"
            "  }"
            "};";

        WKUserScript* script = [[WKUserScript alloc]
            initWithSource:bridge_js
            injectionTime:WKUserScriptInjectionTimeAtDocumentStart
            forMainFrameOnly:YES];
        [config.userContentController addUserScript:script];

        // compute frame in NSView coordinates (flipped Y: macOS origin is bottom-left)
        float view_height = content_view.bounds.size.height;

        // NSView uses logical (point) coordinates; WKWebView handles retina scaling internally
        NSRect frame = NSMakeRect(x, view_height / pixel_ratio - y - h, w, h);

        WKWebView* wk_view = [[WKWebView alloc] initWithFrame:frame configuration:config];
        wk_view.autoresizingMask = 0;  // we manage position manually

        // set up navigation delegate
        LambdaWebViewDelegate* delegate = [[LambdaWebViewDelegate alloc] init];
        wk_view.navigationDelegate = delegate;

        // add as subview
        [content_view addSubview:wk_view];

        // allocate handle
        WebViewHandle* handle = (WebViewHandle*)mem_calloc(1, sizeof(WebViewHandle), MEM_CAT_LAYOUT);
        handle->wk_view = wk_view;
        handle->parent_view = content_view;
        handle->pixel_ratio = pixel_ratio;
        handle->scheme_handler = scheme_handler;
        handle->mode = WEBVIEW_MODE_WINDOW;

        delegate.handle = handle;
        ipc_handler.handle = handle;
        // prevent delegate from being deallocated (associate with the web view)
        objc_setAssociatedObject(wk_view, "lambda_delegate", delegate,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        objc_setAssociatedObject(wk_view, "lambda_ipc_handler", ipc_handler,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        log_info("webview_platform_create: WKWebView created at (%.0f,%.0f) %.0fx%.0f",
                 x, y, w, h);
        return handle;
    }
}

void webview_platform_destroy(WebViewHandle* handle) {
    if (!handle) return;
    @autoreleasepool {
        if (handle->wk_view) {
            [handle->wk_view removeFromSuperview];
            handle->wk_view = nil;
        }
        mem_free(handle);
        log_info("webview_platform_destroy: handle released");
    }
}

void webview_platform_navigate(WebViewHandle* handle, const char* url) {
    if (!handle || !handle->wk_view || !url) return;
    @autoreleasepool {
        NSString* ns_url = [NSString stringWithUTF8String:url];
        NSURL* nsurl = [NSURL URLWithString:ns_url];
        if (!nsurl) {
            log_error("webview_platform_navigate: invalid URL: %s", url);
            return;
        }
        NSURLRequest* request = [NSURLRequest requestWithURL:nsurl];
        [handle->wk_view loadRequest:request];
        log_info("webview_platform_navigate: loading %s", url);
    }
}

void webview_platform_set_html(WebViewHandle* handle, const char* html) {
    if (!handle || !handle->wk_view || !html) return;
    @autoreleasepool {
        NSString* ns_html = [NSString stringWithUTF8String:html];
        [handle->wk_view loadHTMLString:ns_html baseURL:nil];
        log_debug("webview_platform_set_html: loaded inline HTML (%zu bytes)", strlen(html));
    }
}

void webview_platform_eval_js(WebViewHandle* handle, const char* js) {
    if (!handle || !handle->wk_view || !js) return;
    @autoreleasepool {
        NSString* ns_js = [NSString stringWithUTF8String:js];
        [handle->wk_view evaluateJavaScript:ns_js completionHandler:^(id _result, NSError* error) {
            (void)_result;
            if (error) {
                log_error("webview eval_js error: %s", error.localizedDescription.UTF8String);
            }
        }];
    }
}

void webview_platform_set_bounds(WebViewHandle* handle,
                                 float x, float y, float w, float h,
                                 float pixel_ratio) {
    if (!handle || !handle->wk_view || !handle->parent_view) return;
    @autoreleasepool {
        // NSView coordinates: origin at bottom-left, Y increases upward
        float view_height = handle->parent_view.bounds.size.height;
        NSRect frame = NSMakeRect(x, view_height / pixel_ratio - y - h, w, h);
        [handle->wk_view setFrame:frame];
        handle->pixel_ratio = pixel_ratio;
    }
}

void webview_platform_set_visible(WebViewHandle* handle, bool visible) {
    if (!handle || !handle->wk_view) return;
    @autoreleasepool {
        [handle->wk_view setHidden:!visible];
        log_debug("webview_platform_set_visible: %s", visible ? "true" : "false");
    }
}

#endif // __APPLE__
