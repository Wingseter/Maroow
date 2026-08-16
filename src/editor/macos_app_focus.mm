#include "macos_app_focus.hpp"

#if defined(__APPLE__)

#import <AppKit/AppKit.h>

namespace marrow::editor::platform {

bool activate_editor_application() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];
        [[NSRunningApplication currentApplication]
            activateWithOptions:NSApplicationActivateAllWindows];
        [NSApp activateIgnoringOtherApps:YES];

        for (int attempt = 0; attempt < 10; ++attempt) {
            if (uses_regular_activation_policy()) {
                return true;
            }
            [[NSRunLoop currentRunLoop]
                runMode:NSDefaultRunLoopMode
                beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        }
        return uses_regular_activation_policy();
    }
}

bool uses_regular_activation_policy() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        const NSApplicationActivationPolicy app_policy = [NSApp activationPolicy];
        const NSApplicationActivationPolicy process_policy =
            [[NSRunningApplication currentApplication] activationPolicy];
        return app_policy == NSApplicationActivationPolicyRegular &&
            process_policy == NSApplicationActivationPolicyRegular;
    }
}

} // namespace marrow::editor::platform

#endif
