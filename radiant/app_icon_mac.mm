// macOS Dock icon shim for radiant/app_icon.cpp.
//
// lambda.exe is a bare executable, not an .app bundle, so there is no
// CFBundleIconFile for the Dock to read and it would show the generic
// executable tile. The icon is therefore set on NSApp once GLFW has created it.

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>

#include <stddef.h>

#include "../lib/log.h"

extern "C" void radiant_app_icon_mac_set(const unsigned char* png, size_t length) {
    @autoreleasepool {
        if (!NSApp) {
            log_error("app_icon: NSApp is not initialised, Dock icon not set");
            return;
        }
        // the PNG is static data that outlives the image, so NSData only references it
        NSData* data = [NSData dataWithBytesNoCopy:(void*)png length:length freeWhenDone:NO];
        NSImage* image = [[NSImage alloc] initWithData:data];
        if (!image) {
            log_error("app_icon: could not decode the embedded Dock icon");
            return;
        }
        [NSApp setApplicationIconImage:image];
#if !__has_feature(objc_arc)
        [image release];
#endif
        log_debug("app_icon: Dock icon installed");
    }
}

#endif // __APPLE__
