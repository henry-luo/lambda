// webview_layer_mac.mm — macOS offscreen WKWebView backend (layer mode)
//
// Creates a hidden WKWebView for offscreen rendering. Content is captured
// via takeSnapshot and stored as an RGBA bitmap in an ImageSurface.
// Events are injected via JavaScript dispatchEvent.

#ifdef __APPLE__

#import <WebKit/WebKit.h>
#import <objc/runtime.h>
#include <unistd.h>

#include "webview.h"
#include "webview_handle_mac.h"

// view.hpp defines 'Rect' which conflicts with macOS MacTypes.h Rect.
// Temporarily rename to avoid redefinition error in ObjC++ compilation.
#define Rect RadiantRect
#include "view.hpp"
#undef Rect

extern "C" {
#include "../lib/log.h"
#include "../lib/mem.h"
}

// ---------------------------------------------------------------------------
// Navigation delegate — tracks load completion and marks dirty
// ---------------------------------------------------------------------------

@interface LayerWebViewDelegate : NSObject <WKNavigationDelegate>
@property (nonatomic, assign) WebViewHandle* handle;
@end

@implementation LayerWebViewDelegate

- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
    if (_handle) {
        _handle->loaded = true;
        _handle->dirty = true;
    }
    log_info("webview layer: navigation finished: %s",
             webView.URL.absoluteString.UTF8String ?: "(srcdoc)");
}

- (void)webView:(WKWebView*)webView didFailNavigation:(WKNavigation*)navigation
      withError:(NSError*)error {
    log_error("webview layer: navigation failed: %s", error.localizedDescription.UTF8String);
}

- (void)webView:(WKWebView*)webView
    didFailProvisionalNavigation:(WKNavigation*)navigation
      withError:(NSError*)error {
    log_error("webview layer: provisional navigation failed: %s",
              error.localizedDescription.UTF8String);
}

@end

// ---------------------------------------------------------------------------
// IPC/dirty message handler — receives messages from web content
// ---------------------------------------------------------------------------

@interface LayerMessageHandler : NSObject <WKScriptMessageHandler>
@property (nonatomic, assign) WebViewHandle* handle;
@end

@implementation LayerMessageHandler

- (void)userContentController:(WKUserContentController*)controller
      didReceiveScriptMessage:(WKScriptMessage*)message {
    if (_handle) {
        _handle->dirty = true;
    }
}

@end

// ---------------------------------------------------------------------------
// lambda:// URL scheme handler (reused from child mode, same logic)
// ---------------------------------------------------------------------------

@interface LayerSchemeHandler : NSObject <WKURLSchemeHandler>
@property (nonatomic, copy) NSString* base_path;
@end

@implementation LayerSchemeHandler

- (void)webView:(WKWebView*)webView startURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
    NSURL* url = urlSchemeTask.request.URL;
    NSString* path = url.path;

    NSString* full_path = nil;
    if (_base_path && path.length > 0) {
        NSString* rel = [path hasPrefix:@"/"] ? [path substringFromIndex:1] : path;
        full_path = [_base_path stringByAppendingPathComponent:rel];
    }

    if (!full_path) {
        [urlSchemeTask didFailWithError:[NSError errorWithDomain:NSURLErrorDomain
                                                           code:NSURLErrorFileDoesNotExist
                                                       userInfo:nil]];
        return;
    }

    // security: prevent path traversal
    NSString* resolved = full_path.stringByResolvingSymlinksInPath;
    NSString* base_resolved = _base_path.stringByResolvingSymlinksInPath;
    if (![resolved hasPrefix:base_resolved]) {
        log_error("webview layer: lambda:// path traversal blocked: %s", resolved.UTF8String);
        [urlSchemeTask didFailWithError:[NSError errorWithDomain:NSURLErrorDomain
                                                           code:NSURLErrorNoPermissionsToReadFile
                                                       userInfo:nil]];
        return;
    }

    NSData* data = [NSData dataWithContentsOfFile:full_path];
    if (!data) {
        [urlSchemeTask didFailWithError:[NSError errorWithDomain:NSURLErrorDomain
                                                           code:NSURLErrorFileDoesNotExist
                                                       userInfo:nil]];
        return;
    }

    // MIME type from extension
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

    NSURLResponse* response = [[NSURLResponse alloc] initWithURL:url
                                                        MIMEType:mime
                                           expectedContentLength:(NSInteger)data.length
                                                textEncodingName:nil];
    [urlSchemeTask didReceiveResponse:response];
    [urlSchemeTask didReceiveData:data];
    [urlSchemeTask didFinish];
}

- (void)webView:(WKWebView*)webView stopURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
    // nothing to clean up
}

@end

// ---------------------------------------------------------------------------
// Platform API — layer mode
// ---------------------------------------------------------------------------

WebViewHandle* webview_layer_platform_create(float w, float h, float pixel_ratio) {
    @autoreleasepool {
        WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
        WKWebpagePreferences* page_prefs = [[WKWebpagePreferences alloc] init];
        page_prefs.allowsContentJavaScript = YES;
        config.defaultWebpagePreferences = page_prefs;

        // register lambda:// scheme handler
        LayerSchemeHandler* scheme_handler = [[LayerSchemeHandler alloc] init];
        char cwd_buf[4096];
        if (getcwd(cwd_buf, sizeof(cwd_buf))) {
            scheme_handler.base_path = [NSString stringWithUTF8String:cwd_buf];
        } else {
            scheme_handler.base_path = [[NSFileManager defaultManager] currentDirectoryPath];
        }
        [config setURLSchemeHandler:scheme_handler forURLScheme:@"lambda"];

        // inject IPC bridge and MutationObserver dirty tracking
        NSString* bridge_js = @
            "window.__lambda = {"
            "  invoke: function(command, payload) {"
            "    window.webkit.messageHandlers.lambda.postMessage("
            "      JSON.stringify({ command: command, payload: payload })"
            "    );"
            "  },"
            "  _callbacks: {},"
            "  on: function(event, callback) { this._callbacks[event] = callback; }"
            "};"
            // MutationObserver to auto-mark dirty on DOM changes
            "new MutationObserver(function() {"
            "  window.webkit.messageHandlers.lambda.postMessage("
            "    JSON.stringify({ command: '__dirty', payload: {} })"
            "  );"
            "}).observe(document, { childList: true, subtree: true, attributes: true, characterData: true });";

        WKUserScript* script = [[WKUserScript alloc]
            initWithSource:bridge_js
            injectionTime:WKUserScriptInjectionTimeAtDocumentEnd
            forMainFrameOnly:YES];
        [config.userContentController addUserScript:script];

        // register IPC/dirty message handler (must be before WKWebView creation)
        LayerMessageHandler* msg_handler = [[LayerMessageHandler alloc] init];
        [config.userContentController addScriptMessageHandler:msg_handler name:@"lambda"];

        // create WKWebView with physical pixel dimensions but not attached to a window
        // use logical size * pixel_ratio for the frame so snapshots capture at retina resolution
        float phys_w = w * pixel_ratio;
        float phys_h = h * pixel_ratio;
        NSRect frame = NSMakeRect(0, 0, phys_w, phys_h);
        WKWebView* wk_view = [[WKWebView alloc] initWithFrame:frame configuration:config];

        // set up navigation delegate
        LayerWebViewDelegate* delegate = [[LayerWebViewDelegate alloc] init];

        WebViewHandle* handle = (WebViewHandle*)mem_calloc(1, sizeof(WebViewHandle), MEM_CAT_LAYOUT);
        handle->wk_view = wk_view;
        handle->mode = WEBVIEW_MODE_LAYER;
        handle->pixel_ratio = pixel_ratio;
        handle->width = w;
        handle->height = h;
        handle->dirty = true;
        handle->loaded = false;
        handle->snapshot_in_progress = false;

        delegate.handle = handle;
        msg_handler.handle = handle;
        wk_view.navigationDelegate = delegate;
        objc_setAssociatedObject(wk_view, "layer_delegate", delegate,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        objc_setAssociatedObject(wk_view, "layer_scheme_handler", scheme_handler,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        objc_setAssociatedObject(wk_view, "layer_msg_handler", msg_handler,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        log_info("webview layer: created offscreen WKWebView %.0fx%.0f (phys %.0fx%.0f, pr=%.1f)",
                 w, h, phys_w, phys_h, pixel_ratio);
        return handle;
    }
}

void webview_layer_platform_destroy(WebViewHandle* handle) {
    if (!handle) return;
    @autoreleasepool {
        if (handle->wk_view) {
            [handle->wk_view stopLoading];
            handle->wk_view.navigationDelegate = nil;
            handle->wk_view = nil;
        }
        mem_free(handle);
        log_info("webview layer: handle destroyed");
    }
}

void webview_layer_platform_navigate(WebViewHandle* handle, const char* url) {
    if (!handle || !handle->wk_view || !url) return;
    @autoreleasepool {
        NSString* ns_url = [NSString stringWithUTF8String:url];
        NSURL* nsurl = [NSURL URLWithString:ns_url];
        if (!nsurl) {
            log_error("webview layer: invalid URL: %s", url);
            return;
        }
        handle->loaded = false;
        handle->dirty = true;
        [handle->wk_view loadRequest:[NSURLRequest requestWithURL:nsurl]];
        log_info("webview layer: navigating to %s", url);
    }
}

void webview_layer_platform_set_html(WebViewHandle* handle, const char* html) {
    if (!handle || !handle->wk_view || !html) return;
    @autoreleasepool {
        handle->loaded = false;
        handle->dirty = true;
        NSString* ns_html = [NSString stringWithUTF8String:html];
        [handle->wk_view loadHTMLString:ns_html baseURL:nil];
        log_debug("webview layer: loaded inline HTML (%zu bytes)", strlen(html));
    }
}

void webview_layer_platform_eval_js(WebViewHandle* handle, const char* js) {
    if (!handle || !handle->wk_view || !js) return;
    @autoreleasepool {
        NSString* ns_js = [NSString stringWithUTF8String:js];
        [handle->wk_view evaluateJavaScript:ns_js completionHandler:^(id _result, NSError* error) {
            (void)_result;
            if (error) {
                log_error("webview layer eval_js error: %s", error.localizedDescription.UTF8String);
            }
            // JS execution may have changed content
            if (handle) handle->dirty = true;
        }];
    }
}

void webview_layer_platform_resize(WebViewHandle* handle, float w, float h, float pixel_ratio) {
    if (!handle || !handle->wk_view) return;
    @autoreleasepool {
        handle->width = w;
        handle->height = h;
        handle->pixel_ratio = pixel_ratio;
        float phys_w = w * pixel_ratio;
        float phys_h = h * pixel_ratio;
        [handle->wk_view setFrame:NSMakeRect(0, 0, phys_w, phys_h)];
        handle->dirty = true;
        log_debug("webview layer: resized to %.0fx%.0f (phys %.0fx%.0f)", w, h, phys_w, phys_h);
    }
}

bool webview_layer_platform_snapshot(WebViewHandle* handle, ImageSurface* surface) {
    if (!handle || !handle->wk_view || !surface) return false;
    if (handle->snapshot_in_progress) return false;

    @autoreleasepool {
        handle->snapshot_in_progress = true;

        // compute physical pixel dimensions
        int phys_w = (int)(handle->width * handle->pixel_ratio);  // INT_CAST_OK: pixel dimension for bitmap
        int phys_h = (int)(handle->height * handle->pixel_ratio); // INT_CAST_OK: pixel dimension for bitmap
        if (phys_w <= 0 || phys_h <= 0) {
            handle->snapshot_in_progress = false;
            return false;
        }

        WKSnapshotConfiguration* snap_config = [[WKSnapshotConfiguration alloc] init];
        snap_config.snapshotWidth = [NSNumber numberWithFloat:handle->width];

        __block bool success = false;
        __block bool done = false;

        [handle->wk_view takeSnapshotWithConfiguration:snap_config
                                     completionHandler:^(NSImage* image, NSError* error) {
            if (error || !image) {
                log_error("webview layer: snapshot failed: %s",
                          error ? error.localizedDescription.UTF8String : "nil image");
                done = true;
                return;
            }

            // convert NSImage → RGBA bitmap
            NSBitmapImageRep* bitmap = nil;
            CGImageRef cg_image = [image CGImageForProposedRect:nil context:nil hints:nil];
            if (cg_image) {
                size_t cg_w = CGImageGetWidth(cg_image);
                size_t cg_h = CGImageGetHeight(cg_image);

                // allocate or reallocate the surface pixel buffer
                int target_w = (int)cg_w;   // INT_CAST_OK: bitmap dimension
                int target_h = (int)cg_h;   // INT_CAST_OK: bitmap dimension
                int target_pitch = target_w * 4;

                if (surface->width != target_w || surface->height != target_h || !surface->pixels) {
                    if (surface->pixels) {
                        mem_free(surface->pixels);
                    }
                    surface->pixels = mem_calloc(target_pitch * target_h, 1, MEM_CAT_LAYOUT);
                    surface->width = target_w;
                    surface->height = target_h;
                    surface->pitch = target_pitch;
                    surface->format = IMAGE_FORMAT_PNG;  // RGBA raster
                }

                // draw into RGBA context
                CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
                CGContextRef ctx = CGBitmapContextCreate(
                    surface->pixels, target_w, target_h, 8, target_pitch,
                    color_space, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);

                if (ctx) {
                    // NSImage CGImageForProposedRect returns a top-left-origin image,
                    // and our surface is also top-left-origin — no flip needed.
                    CGContextDrawImage(ctx, CGRectMake(0, 0, target_w, target_h), cg_image);
                    CGContextRelease(ctx);
                    success = true;
                }
                CGColorSpaceRelease(color_space);

                log_debug("webview layer: snapshot captured %dx%d", target_w, target_h);
            } else {
                log_error("webview layer: failed to get CGImage from snapshot");
            }
            done = true;
        }];

        // run the run loop briefly to allow the async snapshot to complete
        // takeSnapshot is async but we need it synchronously for the render pipeline
        NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:0.5]; // 500ms timeout
        while (!done && [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                                 beforeDate:deadline]) {
            // spin
        }

        handle->snapshot_in_progress = false;
        if (success) {
            handle->dirty = false;
        }
        return success;
    }
}

bool webview_layer_platform_is_dirty(WebViewHandle* handle) {
    if (!handle) return false;
    return handle->dirty;
}

void webview_layer_platform_mark_dirty(WebViewHandle* handle, bool dirty) {
    if (!handle) return;
    handle->dirty = dirty;
}

// ---------------------------------------------------------------------------
// Event injection — use JavaScript dispatchEvent for portable input forwarding
// ---------------------------------------------------------------------------

void webview_layer_platform_inject_mouse(WebViewHandle* handle,
                                          int type, float x, float y,
                                          int button, int mods) {
    if (!handle || !handle->wk_view) return;
    @autoreleasepool {
        // map type to DOM event name
        const char* event_name;
        switch (type) {
            case 0: event_name = "mousedown"; break;
            case 1: event_name = "mouseup"; break;
            case 2: event_name = "mousemove"; break;
            case 3: event_name = "click"; break;
            default: event_name = "mousemove"; break;
        }

        log_debug("webview layer: inject_mouse type=%d (%s) at (%.1f, %.1f)",
                  type, event_name, x, y);

        char js[1024];
        if (type == 3) {
            // For click: use el.click() which reliably triggers onclick handlers,
            // then also dispatch a MouseEvent for addEventListener-based handlers
            snprintf(js, sizeof(js),
                "(function(){"
                "var el=document.elementFromPoint(%f,%f);"
                "if(el){el.click();}"
                "return el?el.tagName:'null';"
                "})()",
                x, y);
        } else {
            snprintf(js, sizeof(js),
                "(function(){"
                "var el=document.elementFromPoint(%f,%f);"
                "if(el)el.dispatchEvent(new MouseEvent('%s',{"
                "clientX:%f,clientY:%f,button:%d,bubbles:true,cancelable:true}));"
                "return el?el.tagName:'null';"
                "})()",
                x, y, event_name, x, y, button);
        }

        NSString* ns_js = [NSString stringWithUTF8String:js];
        [handle->wk_view evaluateJavaScript:ns_js completionHandler:^(id _r, NSError* err) {
            if (err) {
                log_error("webview layer: inject_mouse error: %s",
                          err.localizedDescription.UTF8String);
            } else if (_r && [_r isKindOfClass:[NSString class]]) {
                log_debug("webview layer: inject_mouse hit element: %s",
                          ((NSString*)_r).UTF8String);
            }
            if (handle) handle->dirty = true;
        }];
    }
}

void webview_layer_platform_inject_key(WebViewHandle* handle,
                                        int type, int keycode, int mods) {
    if (!handle || !handle->wk_view) return;
    @autoreleasepool {
        const char* event_name = (type == 0) ? "keydown" : "keyup";
        char js[512];
        snprintf(js, sizeof(js),
            "(function(){"
            "var el=document.activeElement||document.body;"
            "el.dispatchEvent(new KeyboardEvent('%s',{"
            "keyCode:%d,bubbles:true,cancelable:true}));"
            "})()",
            event_name, keycode);

        NSString* ns_js = [NSString stringWithUTF8String:js];
        [handle->wk_view evaluateJavaScript:ns_js completionHandler:^(id _r, NSError* err) {
            (void)_r;
            if (err) log_error("webview layer: inject_key error: %s",
                               err.localizedDescription.UTF8String);
            if (handle) handle->dirty = true;
        }];
    }
}

void webview_layer_platform_inject_text(WebViewHandle* handle, uint32_t codepoint) {
    if (!handle || !handle->wk_view) return;
    @autoreleasepool {
        char utf8[8];
        int len = 0;
        if (codepoint < 0x80) {
            utf8[0] = (char)codepoint; // INT_CAST_OK: ASCII range
            len = 1;
        } else if (codepoint < 0x800) {
            utf8[0] = 0xC0 | (codepoint >> 6);
            utf8[1] = 0x80 | (codepoint & 0x3F);
            len = 2;
        } else if (codepoint < 0x10000) {
            utf8[0] = 0xE0 | (codepoint >> 12);
            utf8[1] = 0x80 | ((codepoint >> 6) & 0x3F);
            utf8[2] = 0x80 | (codepoint & 0x3F);
            len = 3;
        } else {
            utf8[0] = 0xF0 | (codepoint >> 18);
            utf8[1] = 0x80 | ((codepoint >> 12) & 0x3F);
            utf8[2] = 0x80 | ((codepoint >> 6) & 0x3F);
            utf8[3] = 0x80 | (codepoint & 0x3F);
            len = 4;
        }
        utf8[len] = '\0';

        char js[256];
        snprintf(js, sizeof(js),
            "(function(){"
            "var el=document.activeElement||document.body;"
            "el.dispatchEvent(new InputEvent('input',{"
            "data:'%s',inputType:'insertText',bubbles:true}));"
            "})()", utf8);

        NSString* ns_js = [NSString stringWithUTF8String:js];
        [handle->wk_view evaluateJavaScript:ns_js completionHandler:^(id _r, NSError* err) {
            (void)_r; (void)err;
            if (handle) handle->dirty = true;
        }];
    }
}

void webview_layer_platform_inject_scroll(WebViewHandle* handle,
                                           float dx, float dy,
                                           float x, float y) {
    if (!handle || !handle->wk_view) return;
    @autoreleasepool {
        char js[256];
        snprintf(js, sizeof(js),
            "(function(){"
            "var el=document.elementFromPoint(%f,%f);"
            "if(el)el.dispatchEvent(new WheelEvent('wheel',{"
            "deltaX:%f,deltaY:%f,clientX:%f,clientY:%f,bubbles:true}));"
            "})()", x, y, dx, dy, x, y);

        NSString* ns_js = [NSString stringWithUTF8String:js];
        [handle->wk_view evaluateJavaScript:ns_js completionHandler:^(id _r, NSError* err) {
            (void)_r; (void)err;
            if (handle) handle->dirty = true;
        }];
    }
}

#endif // __APPLE__
