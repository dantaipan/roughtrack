#include "roughtrack/MicrophoneAccess.h"

#import <AVFoundation/AVFoundation.h>

namespace roughtrack
{

// juce::RuntimePermissions has no macOS implementation, so it would report
// access as granted even when the user has denied it.
MicrophoneAccess getMicrophoneAccess()
{
    switch ([AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio])
    {
        case AVAuthorizationStatusAuthorized:    return MicrophoneAccess::granted;
        case AVAuthorizationStatusDenied:        return MicrophoneAccess::denied;
        case AVAuthorizationStatusRestricted:    return MicrophoneAccess::restricted;
        case AVAuthorizationStatusNotDetermined: return MicrophoneAccess::notDetermined;
    }

    return MicrophoneAccess::notDetermined;
}

void requestMicrophoneAccess (std::function<void (bool granted)> callback)
{
    [AVCaptureDevice requestAccessForMediaType: AVMediaTypeAudio
                             completionHandler: ^(BOOL granted)
                             {
                                 callback (granted == YES);
                             }];
}

} // namespace roughtrack
