//
//  GIOMonitorCrashMonitor.c
//
//  Created by Karl Stenerud on 2012-02-12.
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


#include "GIOMonitorCrashMonitor.h"
#include "GIOMonitorCrashMonitorContext.h"
#include "GIOMonitorCrashMonitorType.h"

#include "GIOMonitorCrashReportStore.h"
#include "GIOMonitorCrashReport.h"
#include "GIOMonitorCrashFileUtils.h"

//#include "GIOMonitorCrashMonitor_Deadlock.h"
//#include "GIOMonitorCrashMonitor_MachException.h"
////#include "GIOMonitorCrashMonitor_CPPException.h"
//#include "GIOMonitorCrashMonitor_NSException.h"
//#include "GIOMonitorCrashMonitor_Signal.h"
//#include "GIOMonitorCrashMonitor_System.h"
//#include "GIOMonitorCrashMonitor_User.h"
//#include "GIOMonitorCrashMonitor_AppState.h"
//#include "GIOMonitorCrashMonitor_Zombie.h"
//#include "GIOMonitorCrashDebug.h"
#include "GIOMonitorCrashThread.h"
#include "GIOMonitorCrashSystemCapabilities.h"

#include <memory.h>

//#define GIOMonitorCrashLogger_LocalLevel TRACE
#include "GIOMonitorCrashLogger.h"


// ============================================================================
#pragma mark - Globals -
// ============================================================================

typedef struct
{
    GIOMonitorCrashMonitorType monitorType;
    GIOMonitorCrashMonitorAPI* (*getAPI)(void);
} Monitor;

static Monitor g_monitors[] =
{
//#if GIOMonitorCrashCRASH_HAS_MACH
//    {
//        .monitorType = GIOMonitorCrashMonitorTypeMachException,
//        .getAPI = gioMonitorCrashCM_machexception_getAPI,
//    },
//#endif
//#if GIOMonitorCrashCRASH_HAS_SIGNAL
//    {
//        .monitorType = GIOMonitorCrashMonitorTypeSignal,
//        .getAPI = gioMonitorCrashCM_signal_getAPI,
//    },
//#endif
//#if GIOMonitorCrashCRASH_HAS_OBJC
//    {
//        .monitorType = GIOMonitorCrashMonitorTypeNSException,
//        .getAPI = gioMonitorCrashCM_nsexception_getAPI,
//    },
//    {
//        .monitorType = GIOMonitorCrashMonitorTypeMainThreadDeadlock,
//        .getAPI = gioMonitorCrashCM_deadlock_getAPI,
//    },
//    {
//        .monitorType = GIOMonitorCrashMonitorTypeZombie,
//        .getAPI = gioMonitorCrashCM_zombie_getAPI,
//    },
//#endif
//        删除c++异常采集
//    {
//        .monitorType = GIOMonitorCrashMonitorTypeCPPException,
//        .getAPI = gioMonitorCrashCM_cppexception_getAPI,
//    },
//    {
//        .monitorType = GIOMonitorCrashMonitorTypeUserReported,
//        .getAPI = gioMonitorCrashCM_user_getAPI,
//    },
//    {
//        .monitorType = GIOMonitorCrashMonitorTypeSystem,
//        .getAPI = gioMonitorCrashCM_system_getAPI,
//    },
//    {
//        .monitorType = GIOMonitorCrashMonitorTypeApplicationState,
//        .getAPI = gioMonitorCrashCM_appstate_getAPI,
//    },
};
static int g_monitorsCount = sizeof(g_monitors) / sizeof(*g_monitors);

static GIOMonitorCrashMonitorType g_activeMonitors = GIOMonitorCrashMonitorTypeNone;

static bool g_handlingFatalException = false;
static bool g_crashedDuringExceptionHandling = false;
static bool g_requiresAsyncSafety = false;

static void (*g_onExceptionEvent)(struct GIOMonitorCrash_MonitorContext* monitorContext);

static char g_lastCrashReportFilePath[GIOMonitorCrashFU_MAX_PATH_LENGTH];

// ============================================================================
#pragma mark - API -
// ============================================================================

static inline GIOMonitorCrashMonitorAPI* getAPI(Monitor* monitor)
{
    if(monitor != NULL && monitor->getAPI != NULL)
    {
        return monitor->getAPI();
    }
    return NULL;
}

static inline bool isMonitorEnabled(Monitor* monitor)
{
    GIOMonitorCrashMonitorAPI* api = getAPI(monitor);
    if(api != NULL && api->isEnabled != NULL)
    {
        return api->isEnabled();
    }
    return false;
}

static inline void addContextualInfoToEvent(Monitor* monitor, struct GIOMonitorCrash_MonitorContext* eventContext)
{
    GIOMonitorCrashMonitorAPI* api = getAPI(monitor);
    if(api != NULL && api->addContextualInfoToEvent != NULL)
    {
        api->addContextualInfoToEvent(eventContext);
    }
}

void gioMonitorCrashCM_setEventCallback(void (*onEvent)(struct GIOMonitorCrash_MonitorContext* monitorContext))
{
    g_onExceptionEvent = onEvent;
}

void gioMonitorCrashCM_setActiveMonitors(GIOMonitorCrashMonitorType monitorTypes)
{
    //  GIO DELETE
//    if(gioMonitorCrashDebug_isBeingTraced() && (monitorTypes & GIOMonitorCrashMonitorTypeDebuggerUnsafe))
//    {
//        static bool hasWarned = false;
//        if(!hasWarned)
//        {
//            hasWarned = true;
//            GIOMonitorCrashLOGBASIC_WARN("    ************************ Crash Handler Notice ************************");
//            GIOMonitorCrashLOGBASIC_WARN("    *     App is running in a debugger. Masking out unsafe monitors.     *");
//            GIOMonitorCrashLOGBASIC_WARN("    * This means that most crashes WILL NOT BE RECORDED while debugging! *");
//            GIOMonitorCrashLOGBASIC_WARN("    **********************************************************************");
//        }
//        monitorTypes &= GIOMonitorCrashMonitorTypeDebuggerSafe;
//    }
//    if(g_requiresAsyncSafety && (monitorTypes & GIOMonitorCrashMonitorTypeAsyncUnsafe))
//    {
//        GIOMonitorCrashLOG_DEBUG("Async-safe environment detected. Masking out unsafe monitors.");
//        monitorTypes &= GIOMonitorCrashMonitorTypeAsyncSafe;
//    }
//
//    GIOMonitorCrashLOG_DEBUG("Changing active monitors from 0x%x tp 0x%x.", g_activeMonitors, monitorTypes);
//
//    GIOMonitorCrashMonitorType activeMonitors = GIOMonitorCrashMonitorTypeNone;
//    for(int i = 0; i < g_monitorsCount; i++)
//    {
//        Monitor* monitor = &g_monitors[i];
//        bool isEnabled = monitor->monitorType & monitorTypes;
//        setMonitorEnabled(monitor, isEnabled);
//        if(isMonitorEnabled(monitor))
//        {
//            activeMonitors |= monitor->monitorType;
//        }
//        else
//        {
//            activeMonitors &= ~monitor->monitorType;
//        }
//    }
//
//    GIOMonitorCrashLOG_DEBUG("Active monitors are now 0x%x.", activeMonitors);
//    g_activeMonitors = activeMonitors;
}

GIOMonitorCrashMonitorType gioMonitorCrashCM_getActiveMonitors()
{
    return g_activeMonitors;
}


// ============================================================================
#pragma mark - Private API -
// ============================================================================

bool gioMonitorCrashCM_notifyFatalExceptionCaptured(bool isAsyncSafeEnvironment)
{
    g_requiresAsyncSafety |= isAsyncSafeEnvironment; // Don't let it be unset.
    if(g_handlingFatalException)
    {
        g_crashedDuringExceptionHandling = true;
    }
    g_handlingFatalException = true;
    if(g_crashedDuringExceptionHandling)
    {
        GIOMonitorCrashLOG_INFO("Detected crash in the crash reporter. Uninstalling GIOMonitorCrash.");
        gioMonitorCrashCM_setActiveMonitors(GIOMonitorCrashMonitorTypeNone);
    }
    return g_crashedDuringExceptionHandling;
}

// ============================================================================
#pragma mark - Callbacks -
// ============================================================================

/** Called when a crash occurs.
 *
 * This function gets passed as a callback to a crash handler.
 */
static void onCrash(struct GIOMonitorCrash_MonitorContext* monitorContext)
{
    char crashReportFilePath[GIOMonitorCrashFU_MAX_PATH_LENGTH];
    gioMonitorCRS_getNextCrashReportPath(crashReportFilePath);
    strncpy(g_lastCrashReportFilePath, crashReportFilePath, sizeof(g_lastCrashReportFilePath));
    gioMonitorCrashReport_writeStandardReport(monitorContext, crashReportFilePath);
}

void gioMonitorCrashCM_handleException(struct GIOMonitorCrash_MonitorContext* context)
{
    context->requiresAsyncSafety = g_requiresAsyncSafety;
    if(g_crashedDuringExceptionHandling)
    {
        context->crashedDuringCrashHandling = true;
    }
    for(int i = 0; i < g_monitorsCount; i++)
    {
        Monitor* monitor = &g_monitors[i];
        if(isMonitorEnabled(monitor))
        {
            addContextualInfoToEvent(monitor, context);
        }
    }

    g_onExceptionEvent = onCrash;
    
    g_onExceptionEvent(context);

    if (context->currentSnapshotUserReported) {
        g_handlingFatalException = false;
    } else {
        if(g_handlingFatalException && !g_crashedDuringExceptionHandling) {
            GIOMonitorCrashLOG_DEBUG("Exception is fatal. Restoring original handlers.");
            gioMonitorCrashCM_setActiveMonitors(GIOMonitorCrashMonitorTypeNone);
        }
    }
}

