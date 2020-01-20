//
//  GIOMonitorCrashMonitor_NSException.m
//
//  Created by Karl Stenerud on 2012-01-28.
//
//  Copyright (c) 2012 Karl Stenerud. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall remain in place
// in this source code.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

//#import "GIOMonitorCrash.h"
#import "GIOMonitorCrashMonitor_NSException.h"
#import "GIOMonitorCrashStackCursor_Backtrace.h"
#include "GIOMonitorCrashMonitorContext.h"
//#include "GIOMonitorCrashID.h"
#include "GIOMonitorCrashThread.h"

//#define GIOMonitorCrashLogger_LocalLevel TRACE
#import "GIOMonitorCrashLogger.h"


// ============================================================================
#pragma mark - Globals -
// ============================================================================

static volatile bool g_isEnabled = 1;

static GIOMonitorCrash_MonitorContext g_monitorContext;

/** The exception handler that was in place before we installed ours. */
static NSUncaughtExceptionHandler* g_previousUncaughtExceptionHandler;


// ============================================================================
#pragma mark - Callbacks -
// ============================================================================

/** Our custom excepetion handler.
 * Fetch the stack trace from the exception and write a report.
 *
 * @param exception The exception that was raised.
 */

void handleException(NSException* exception, BOOL currentSnapshotUserReported) {
    GIOMonitorCrashLOG_DEBUG(@"Trapped exception %@", exception);
    if(g_isEnabled)
    {
        gioMonitorCrashMachineContext_suspendEnvironment();
        gioMonitorCrashCM_notifyFatalExceptionCaptured(false);

        GIOMonitorCrashLOG_DEBUG(@"Filling out context.");
        // 如果没有真正的异常，[exception callStackReturnAddresses] 返回的是空的，所以为了能获得调用栈，使用了[NSThread callStackReturnAddresses];
//        NSArray* addresses = [exception callStackReturnAddresses];
//        NSArray* symbols = [exception callStackSymbols];
        NSArray *addresses = [NSThread callStackReturnAddresses];
        
        NSUInteger numFrames = addresses.count;
        uintptr_t* callstack = malloc(numFrames * sizeof(*callstack));
        for(NSUInteger i = 0; i < numFrames; i++)
        {
            callstack[i] = (uintptr_t)[addresses[i] unsignedLongLongValue];
        }

        char eventID[37];
//        gioMonitorCrashID_generate(eventID);
        GIOMonitorCrashMC_NEW_CONTEXT(machineContext);
        gioMonitorCrashMachineContext_getContextForThread(gioMonitorCrashThread_self(), machineContext, true);
        GIOMonitorCrashStackCursor cursor;
        gioMonitorCrashStackCursor_initWithBacktrace(&cursor, callstack, (int)numFrames, 0);

        GIOMonitorCrash_MonitorContext* crashContext = &g_monitorContext;
        memset(crashContext, 0, sizeof(*crashContext));
        crashContext->crashType = GIOMonitorCrashMonitorTypeNSException;
        crashContext->eventID = eventID;
        crashContext->offendingMachineContext = machineContext;
        crashContext->registersAreValid = false;
        crashContext->NSException.name = [[exception name] UTF8String];
        crashContext->NSException.userInfo = [[NSString stringWithFormat:@"%@", exception.userInfo] UTF8String];
        crashContext->exceptionName = crashContext->NSException.name;
        crashContext->crashReason = [[exception reason] UTF8String];
        crashContext->stackCursor = &cursor;
        crashContext->currentSnapshotUserReported = currentSnapshotUserReported;

        GIOMonitorCrashLOG_DEBUG(@"Calling main crash handler.");
        gioMonitorCrashCM_handleException(crashContext);

        free(callstack);
        if (currentSnapshotUserReported) {
            gioMonitorCrashMachineContext_resumeEnvironment();
        }
    }
}

static void handleUncaughtException(NSException* exception) {
    handleException(exception, false);
}

// ============================================================================
#pragma mark - API -
// ============================================================================

static void setEnabled(bool isEnabled)
{
    if(isEnabled != g_isEnabled)
    {
        g_isEnabled = isEnabled;
        if(isEnabled)
        {
            GIOMonitorCrashLOG_DEBUG(@"Backing up original handler.");
            g_previousUncaughtExceptionHandler = NSGetUncaughtExceptionHandler();

            GIOMonitorCrashLOG_DEBUG(@"Setting new handler.");
            NSSetUncaughtExceptionHandler(&handleUncaughtException);
//            GIOMonitorCrash.sharedInstance.uncaughtExceptionHandler = &handleUncaughtException;
//            GIOMonitorCrash.sharedInstance.currentSnapshotUserReportedExceptionHandler = &handleCurrentSnapshotUserReportedException;
        }
        else
        {
            GIOMonitorCrashLOG_DEBUG(@"Restoring original handler.");
            NSSetUncaughtExceptionHandler(g_previousUncaughtExceptionHandler);
        }
    }
}

static bool isEnabled()
{
    return g_isEnabled;
}

GIOMonitorCrashMonitorAPI* gioMonitorCrashCM_nsexception_getAPI()
{
    static GIOMonitorCrashMonitorAPI api =
    {
        .setEnabled = setEnabled,
        .isEnabled = isEnabled
    };
    return &api;
}
