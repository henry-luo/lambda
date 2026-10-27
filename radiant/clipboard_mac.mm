// NSPasteboard is a byte transport; the editing package chooses what to copy (D7.2.5).
#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include "clipboard_mac.hpp"
#include <string.h>

static NSString* clipboard_mac_type(const char* mime) {
    const struct { const char* mime; NSString* type; } flavors[] = {
        {"text/plain", NSPasteboardTypeString}, {"text/html", NSPasteboardTypeHTML},
        {"image/png", NSPasteboardTypePNG}, {"image/tiff", NSPasteboardTypeTIFF},
        {"image/jpeg", @"public.jpeg"}
    };
    for (const auto& flavor : flavors) if (strcmp(mime, flavor.mime) == 0) return flavor.type;
    return nil;
}

static void clipboard_mac_write(ClipboardBackend*, ArrayList* items) {
    @autoreleasepool {
        NSPasteboard* board = [NSPasteboard generalPasteboard];
        [board clearContents];
        for (int i = 0; items && i < items->length; i++) {
            ClipboardItem* item = (ClipboardItem*)items->data[i];
            for (int j = 0; item && j < item->entries->length; j++) {
                ClipboardEntry* entry = (ClipboardEntry*)item->entries->data[j];
                NSString* type = clipboard_mac_type(entry->mime);
                if (!type) continue;
                NSData* data = [NSData dataWithBytes:entry->data length:entry->data_len];
                [board setData:data forType:type];
                if (strncmp(entry->mime, "image/", 6) == 0 && ![type isEqual:NSPasteboardTypeTIFF]) {
                    NSBitmapImageRep* image = [NSBitmapImageRep imageRepWithData:data];
                    NSData* tiff = [image TIFFRepresentation];
                    if (tiff) [board setData:tiff forType:NSPasteboardTypeTIFF];
                }
            }
        }
    }
}

ArrayList* clipboard_mac_read_items(NSPasteboard* board) {
    @autoreleasepool {
        ArrayList* items = arraylist_new(1);
        ClipboardItem* item = clipboard_item_new();
        const char* mimes[] = {"text/plain", "text/html"};
        for (const char* mime : mimes) {
            NSData* data = [board dataForType:clipboard_mac_type(mime)];
            if (data) clipboard_item_add(item, mime, [data bytes], [data length]);
        }
        NSArray* files = [board readObjectsForClasses:@[[NSURL class]]
            options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];
        NSData* png = nil;
        NSData* source = nil;
        if ([files count]>0) {
            // Finder's PNG/TIFF alternatives can be file icons, so resolve the file before image flavors.
            if ([files count]==1) source = [NSData dataWithContentsOfURL:[files firstObject]];
        } else {
            png = [board dataForType:NSPasteboardTypePNG];
            if (!png) source = [board dataForType:NSPasteboardTypeTIFF];
            if (!png && !source) source = [board dataForType:@"public.jpeg"];
        }
        if (source) {
            NSBitmapImageRep* image = [NSBitmapImageRep imageRepWithData:source];
            png = [image representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        }
        if (png) clipboard_item_add(item, "image/png", [png bytes], [png length]);
        arraylist_append(items, item);
        return items;
    }
}

static ArrayList* clipboard_mac_read(ClipboardBackend*) {
    return clipboard_mac_read_items([NSPasteboard generalPasteboard]);
}

static void clipboard_mac_clear(ClipboardBackend*) {
    @autoreleasepool { [[NSPasteboard generalPasteboard] clearContents]; }
}

extern "C" void radiant_clipboard_mac_init(void) {
    static ClipboardBackend backend = {clipboard_mac_write, clipboard_mac_read, clipboard_mac_clear, nullptr};
    clipboard_store_set_backend(&backend);
}
#endif
