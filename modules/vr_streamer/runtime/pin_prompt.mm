#include "pin_prompt.hpp"

#import <AppKit/AppKit.h>

namespace pin_prompt {

namespace {

// The prompt on screen, if any; touched on the main thread only.
NSAlert* g_alert = nil;
NSWindow* g_sheet_parent = nil;

}  // namespace

void ask(bool again, std::function<void(std::optional<std::string>)> answer) {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (g_alert) {
            return;
        }
        // The system's own sheet, as plain as it gets, so it sits well over any game.
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @"Enter the PIN shown in your headset";
        if (again) {
            alert.informativeText = @"That PIN didn't match.";
        }
        NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 120, 24)];
        field.placeholderString = @"PIN";
        alert.accessoryView = field;
        [alert addButtonWithTitle:@"Pair"];
        [alert addButtonWithTitle:@"Cancel"];
        alert.window.initialFirstResponder = field;
        g_alert = alert;
        void (^finished)(NSModalResponse) = ^(NSModalResponse response) {
            g_alert = nil;
            g_sheet_parent = nil;
            if (response == NSAlertFirstButtonReturn) {
                NSString* digits = [[field.stringValue componentsSeparatedByCharactersInSet:NSCharacterSet.decimalDigitCharacterSet.invertedSet]
                    componentsJoinedByString:@""];
                answer(std::string(digits.UTF8String));
            } else if (response == NSAlertSecondButtonReturn) {
                answer(std::nullopt);
            }
            // Taken away (dismiss): the headset already stopped asking.
        };
        NSWindow* window = NSApp.keyWindow ?: NSApp.mainWindow;
        if (window) {
            g_sheet_parent = window;
            [alert beginSheetModalForWindow:window completionHandler:finished];
        } else {
            [NSApp activateIgnoringOtherApps:YES];
            finished([alert runModal]);
        }
    });
}

void dismiss() {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (g_alert && g_sheet_parent) {
            [g_sheet_parent endSheet:g_alert.window returnCode:NSModalResponseAbort];
        } else if (g_alert) {
            [NSApp abortModal];
        }
    });
}

}  // namespace pin_prompt
