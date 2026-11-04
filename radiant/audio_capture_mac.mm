#ifdef __APPLE__
#include "audio_capture.hpp"
#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

AudioCapturePermission audio_capture_mac_permission(bool request) {
    @autoreleasepool {
        AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
        if (status == AVAuthorizationStatusAuthorized) return AUDIO_CAPTURE_ALLOWED;
        if (status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted) return AUDIO_CAPTURE_DENIED;
        // TCC requires a host purpose string; never request from an unidentified embedding executable.
        id purpose = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"NSMicrophoneUsageDescription"];
        if (![purpose isKindOfClass:[NSString class]] || ![(NSString*)purpose length]) return AUDIO_CAPTURE_UNAVAILABLE;
        static int pending = 0;
        if (request) {
            int idle = 0;
            if (__atomic_compare_exchange_n(&pending, &idle, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                // the OS completion owns no document, PCM queue or GC value (D5.5.2).
                [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL) {
                    __atomic_store_n(&pending, 0, __ATOMIC_RELEASE);
                }];
            }
        }
        return __atomic_load_n(&pending, __ATOMIC_ACQUIRE) ? AUDIO_CAPTURE_PENDING : AUDIO_CAPTURE_UNKNOWN;
    }
}
#endif
