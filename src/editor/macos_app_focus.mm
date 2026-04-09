#include "macos_app_focus.hpp"

#if defined(__APPLE__)

#import <AppKit/AppKit.h>

namespace marrow::editor::platform {

bool activate_editor_application() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        const BOOL activation_policy_set =
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp activateIgnoringOtherApps:YES];
        return activation_policy_set == YES && uses_regular_activation_policy();
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
