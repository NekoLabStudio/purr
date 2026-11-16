#include "latency_critical.hpp"

#import <Foundation/Foundation.h>

namespace latency_critical {

namespace {

id<NSObject> g_activity = nil;

}  // namespace

void begin() {
    if (!g_activity) {
        g_activity = [NSProcessInfo.processInfo beginActivityWithOptions:NSActivityUserInitiated | NSActivityLatencyCritical
                                                                  reason:@"Streaming to a VR headset"];
    }
}

void end() {
    if (g_activity) {
        [NSProcessInfo.processInfo endActivity:g_activity];
        g_activity = nil;
    }
}

}  // namespace latency_critical
