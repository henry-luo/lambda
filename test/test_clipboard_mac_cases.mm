#import <Cocoa/Cocoa.h>
#include <gtest/gtest.h>
#include "../radiant/clipboard_mac.hpp"
#include <string.h>

// isolated platform representations avoid touching the user's general pasteboard.
@interface ClipboardMacFixture : NSObject {
@public
    NSDictionary* flavors;
    NSArray* files;
    unsigned image_requests;
}
- (NSData*)dataForType:(NSString*)type;
- (NSArray*)readObjectsForClasses:(NSArray*)classes options:(NSDictionary*)options;
@end

@implementation ClipboardMacFixture
- (NSData*)dataForType:(NSString*)type {
    if ([type isEqual:NSPasteboardTypePNG] || [type isEqual:NSPasteboardTypeTIFF] || [type isEqual:@"public.jpeg"])
        image_requests++;
    return [flavors objectForKey:type];
}
- (NSArray*)readObjectsForClasses:(NSArray*)classes options:(NSDictionary*)options {
    EXPECT_TRUE([classes containsObject:[NSURL class]]);
    EXPECT_TRUE([[options objectForKey:NSPasteboardURLReadingFileURLsOnlyKey] boolValue]);
    return files;
}
@end

static NSData* clipboard_icon(NSString* type) {
    NSBitmapImageRep* icon = [[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
        pixelsWide:8 pixelsHigh:8 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
        colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0] autorelease];
    memset([icon bitmapData], 255, [icon bytesPerRow] * [icon pixelsHigh]);
    return [type isEqual:NSPasteboardTypeTIFF] ? [icon TIFFRepresentation]
        : [icon representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
}

static ClipboardMacFixture* clipboard_fixture(NSArray* files, NSString* type, NSData* data) {
    ClipboardMacFixture* fixture = [[[ClipboardMacFixture alloc] init] autorelease];
    fixture->files = files;
    fixture->flavors = @{type:data};
    return fixture;
}

static void expect_image_content(ArrayList* items, NSData* expected) {
    ClipboardEntry* entry = clipboard_items_find(items, "image/png");
    ASSERT_NE(entry, nullptr);
    NSBitmapImageRep* actual = [NSBitmapImageRep imageRepWithData:[NSData dataWithBytes:entry->data length:entry->data_len]];
    NSBitmapImageRep* source = [NSBitmapImageRep imageRepWithData:expected];
    ASSERT_NE(actual, nil);
    ASSERT_EQ([actual pixelsWide], [source pixelsWide]);
    ASSERT_EQ([actual pixelsHigh], [source pixelsHigh]);
    // check source pixels as well as dimensions so a file preview cannot pass as content.
    const NSInteger sample_x[] = {0, 50, 120};
    const NSInteger sample_y[] = {0, 25, 75};
    for (NSInteger y : sample_y) for (NSInteger x : sample_x) {
        NSColor* a = [[actual colorAtX:x y:y] colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]];
        NSColor* b = [[source colorAtX:x y:y] colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]];
        EXPECT_NEAR([a redComponent], [b redComponent], 1.0 / 255.0);
        EXPECT_NEAR([a greenComponent], [b greenComponent], 1.0 / 255.0);
        EXPECT_NEAR([a blueComponent], [b blueComponent], 1.0 / 255.0);
        EXPECT_NEAR([a alphaComponent], [b alphaComponent], 1.0 / 255.0);
    }
}

TEST(ClipboardMacImage, CopiedFileContentPrecedesPngAndTiffIcons) {
    @autoreleasepool {
        NSURL* file = [NSURL fileURLWithPath:@"test/ui/note/fixtures/clipboard.png"];
        NSData* source = [NSData dataWithContentsOfURL:file];
        ASSERT_NE(source, nil);
        for (NSString* type in @[NSPasteboardTypePNG, NSPasteboardTypeTIFF]) {
            ClipboardMacFixture* fixture = clipboard_fixture(@[file], type, clipboard_icon(type));
            ArrayList* items = clipboard_mac_read_items((NSPasteboard*)fixture);
            expect_image_content(items, source);
            EXPECT_EQ(fixture->image_requests, 0u);
            clipboard_items_free(items);
        }
    }
}

TEST(ClipboardMacImage, FileCopyNeverFallsBackToAnIcon) {
    @autoreleasepool {
        NSURL* file = [NSURL fileURLWithPath:@"test/ui/note/fixtures/clipboard.png"];
        NSArray* cases = @[
            @[[NSURL fileURLWithPath:@"temp/sketch/missing-clipboard.png"]],
            @[[NSURL fileURLWithPath:@"test/demo/sketch/sketch.css"]],
            @[file, file]
        ];
        for (NSArray* files in cases) {
            ClipboardMacFixture* fixture = clipboard_fixture(files, NSPasteboardTypeTIFF, clipboard_icon(NSPasteboardTypeTIFF));
            ArrayList* items = clipboard_mac_read_items((NSPasteboard*)fixture);
            EXPECT_EQ(clipboard_items_find(items, "image/png"), nullptr);
            EXPECT_EQ(fixture->image_requests, 0u);
            clipboard_items_free(items);
        }
    }
}

TEST(ClipboardMacImage, PixelClipboardWithoutFileUrlsRetainsPngAndTiff) {
    @autoreleasepool {
        NSData* source = [NSData dataWithContentsOfFile:@"test/ui/note/fixtures/clipboard.png"];
        ASSERT_NE(source, nil);
        NSBitmapImageRep* image = [NSBitmapImageRep imageRepWithData:source];
        for (NSString* type in @[NSPasteboardTypePNG, NSPasteboardTypeTIFF]) {
            NSData* data = [type isEqual:NSPasteboardTypePNG] ? source : [image TIFFRepresentation];
            ClipboardMacFixture* fixture = clipboard_fixture(@[], type, data);
            ArrayList* items = clipboard_mac_read_items((NSPasteboard*)fixture);
            expect_image_content(items, source);
            clipboard_items_free(items);
        }
    }
}
