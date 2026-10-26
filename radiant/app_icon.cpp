// Application icon for Radiant GUI windows.
//
// The artwork lives in lambda/asset/ and is produced by utils/generate_icon.py,
// which also emits app_icon_data.h with the PNGs installed here. Each platform
// takes its icon from a different place:
//   macOS    the Dock tile, set at runtime (app_icon_mac.mm)
//   Windows  the GLFW_ICON resource from lambda/asset/lambda.rc, nothing to do here
//   others   the window icon, handed to the platform backend as decoded RGBA images

#include "platform.hpp"

#include "../lib/log.h"

#ifndef _WIN32
#include "../lib/image.h"
#include "app_icon_data.h"
#endif

#ifdef __APPLE__
extern "C" void radiant_app_icon_mac_set(const unsigned char* png, size_t length);
#endif

extern "C" void radiant_app_icon_install(RadiantWindow* window) {
#if defined(__APPLE__)
    (void)window;
    radiant_app_icon_mac_set(app_icon_png_dock, sizeof(app_icon_png_dock));
#elif defined(_WIN32)
    // GLFW registers its window class with the executable's GLFW_ICON resource
    (void)window;
#else
    RadiantIconImage images[APP_ICON_PNG_COUNT];
    unsigned char* decoded[APP_ICON_PNG_COUNT];
    int count = 0;
    for (int i = 0; i < APP_ICON_PNG_COUNT; i++) {
        int channels = 0;
        unsigned char* pixels = image_load_from_memory(app_icon_pngs[i].data, app_icon_pngs[i].length,
                                                       &images[count].width, &images[count].height, &channels);
        if (!pixels) {
            log_error("app_icon: failed to decode embedded icon %d", i);
            continue;
        }
        decoded[count] = pixels;
        images[count++].rgba = pixels;
    }
    // the backend copies the pixels, so the decoded buffers are released right after
    if (count > 0) radiant_platform_set_window_icon(window, images, count);
    for (int i = 0; i < count; i++) image_free(decoded[i]);
    log_debug("app_icon: installed %d window icon sizes", count);
#endif
}
