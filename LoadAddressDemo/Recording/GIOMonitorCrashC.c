//
//  GIOMonitorCrashC.c
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


#include "GIOMonitorCrashC.h"

#include "GIOMonitorCrashCachedData.h"
#include "GIOMonitorCrashReport.h"
//#include "GIOMonitorCrashReportFixer.h"
#include "GIOMonitorCrashReportStore.h"
//#include "GIOMonitorCrashMonitor_Deadlock.h"
//#include "GIOMonitorCrashMonitor_User.h"
#include "GIOMonitorCrashFileUtils.h"
#include "GIOMonitorCrashObjC.h"
#include "GIOMonitorCrashString.h"
//#include "GIOMonitorCrashMonitor_System.h"
//#include "GIOMonitorCrashMonitor_Zombie.h"
//#include "GIOMonitorCrashMonitor_AppState.h"
#include "GIOMonitorCrashMonitorContext.h"
#include "GIOMonitorCrashSystemCapabilities.h"

//#define GIOMonitorCrashLogger_LocalLevel TRACE
#include "GIOMonitorCrashLogger.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// ============================================================================
#pragma mark - Globals -
// ============================================================================

/** True if GIOMonitorCrash has been installed. */
static volatile bool g_installed = 0;

static bool g_shouldAddConsoleLogToReport = false;
static bool g_shouldPrintPreviousLog = false;
static char g_consoleLogPath[GIOMonitorCrashFU_MAX_PATH_LENGTH];
static GIOMonitorCrashMonitorType g_monitoring = GIOMonitorCrashMonitorTypeProductionSafeMinimal;
static char g_lastCrashReportFilePath[GIOMonitorCrashFU_MAX_PATH_LENGTH];


// ============================================================================
#pragma mark - Utility -
// ============================================================================

static void printPreviousLog(const char* filePath)
{
    char* data;
    int length;
    if(gioMonitorCrashFileUtils_readEntireFile(filePath, &data, &length, 0))
    {
        printf("\nvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv Previous Log vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv\n\n");
        printf("%s\n", data);
        printf("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n\n");
        fflush(stdout);
    }
}


// ============================================================================
#pragma mark - API -
// ============================================================================

GIOMonitorCrashMonitorType gioMonitorCrash_install(const char* appName, const char* const installPath)
{
    GIOMonitorCrashLOG_DEBUG("Installing crash reporter.");

    if(g_installed)
    {
        GIOMonitorCrashLOG_DEBUG("Crash reporter already installed.");
        return g_monitoring;
    }
    g_installed = 1;

    char path[GIOMonitorCrashFU_MAX_PATH_LENGTH];
    snprintf(path, sizeof(path), "%s/Reports", installPath);
    gioMonitorCrashFileUtils_makePath(path);
    gioMonitorCRS_initialize(appName, path);

    snprintf(path, sizeof(path), "%s/Data", installPath);
    gioMonitorCrashFileUtils_makePath(path);
    snprintf(path, sizeof(path), "%s/Data/CrashState.json", installPath);
//    gioMonitorCrashState_initialize(path);

    snprintf(g_consoleLogPath, sizeof(g_consoleLogPath), "%s/Data/ConsoleLog.txt", installPath);
    if(g_shouldPrintPreviousLog)
    {
        printPreviousLog(g_consoleLogPath);
    }
    gioMonitorCrashLog_setLogFilename(g_consoleLogPath, true);

    gioMonitorCCD_init(60);

//    gioMonitorCrashCM_setEventCallback(onCrash);
//    GIOMonitorCrashMonitorType monitors = gioMonitorCrash_setMonitoring(g_monitoring);

    GIOMonitorCrashLOG_DEBUG("Installation complete.");
    return GIOMonitorCrashMonitorTypeNone;
}



void gioMonitorCrash_setUserInfoJSON(const char* const userInfoJSON)
{
    gioMonitorCrashReport_setUserInfoJSON(userInfoJSON);
}

void gioMonitorCrash_setIntrospectMemory(bool introspectMemory)
{
    gioMonitorCrashReport_setIntrospectMemory(introspectMemory);
}

void gioMonitorCrash_setDoNotIntrospectClasses(const char** doNotIntrospectClasses, int length)
{
    gioMonitorCrashReport_setDoNotIntrospectClasses(doNotIntrospectClasses, length);
}

void gioMonitorCrash_setCrashNotifyCallback(const GIOMonitorCrashReportWriteCallback onCrashNotify)
{
    gioMonitorCrashReport_setUserSectionWriteCallback(onCrashNotify);
}

void gioMonitorCrash_setAddConsoleLogToReport(bool shouldAddConsoleLogToReport)
{
    g_shouldAddConsoleLogToReport = shouldAddConsoleLogToReport;
}

void gioMonitorCrash_setPrintPreviousLog(bool shouldPrintPreviousLog)
{
    g_shouldPrintPreviousLog = shouldPrintPreviousLog;
}

void gioMonitorCrash_setMaxReportCount(int maxReportCount)
{
    gioMonitorCRS_setMaxReportCount(maxReportCount);
}

int gioMonitorCrash_getReportCount()
{
    return gioMonitorCRS_getReportCount();
}

int gioMonitorCrash_getReportIDs(int64_t* reportIDs, int count)
{
    return gioMonitorCRS_getReportIDs(reportIDs, count);
}

int64_t gioMonitorCrash_addUserReport(const char* report, int reportLength)
{
    return gioMonitorCRS_addUserReport(report, reportLength);
}

void gioMonitorCrash_deleteAllReports()
{
    gioMonitorCRS_deleteAllReports();
}

void gioMonitorCrash_deleteReportWithID(int64_t reportID)
{
    gioMonitorCRS_deleteReportWithID(reportID);
}
