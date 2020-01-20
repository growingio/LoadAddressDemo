//
//  GIOMonitorCrash.m
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


#import "GIOMonitorCrash.h"
#import "GIOMonitorCrashC.h"
#import "GIOMonitorCrashReportFields.h"
#import "GIOMonitorCrashMonitorContext.h"
#import "GIOMonitorCrashSystemCapabilities.h"
#import "GIOMonitorCrashLogger.h"

#include <inttypes.h>
#if GIOMonitorCrashCRASH_HAS_UIKIT
#import <UIKit/UIKit.h>
#endif

//#import "GrowingGlobal.h"

// ============================================================================
#pragma mark - Globals -
// ============================================================================

@interface GIOMonitorCrash ()

@property(nonatomic,readwrite,retain) NSString* bundleName;
@property(nonatomic,readwrite,retain) NSString* basePath;

@end


static NSString* getBundleName()
{
    NSString* bundleName = [[[NSBundle mainBundle] infoDictionary] objectForKey:@"CFBundleName"];
    if(bundleName == nil)
    {
        bundleName = @"Unknown";
    }
    return bundleName;
}

static NSString* getBasePath()
{
    NSArray* directories = NSSearchPathForDirectoriesInDomains(NSCachesDirectory,
                                                               NSUserDomainMask,
                                                               YES);
    if([directories count] == 0)
    {
        GIOMonitorCrashLOG_ERROR(@"Could not locate cache directory path.");
        return nil;
    }
    NSString* cachePath = [directories objectAtIndex:0];
    if([cachePath length] == 0)
    {
        GIOMonitorCrashLOG_ERROR(@"Could not locate cache directory path.");
        return nil;
    }
    NSString* pathEnd = [@"GIOMonitorCrash" stringByAppendingPathComponent:getBundleName()];
    return [cachePath stringByAppendingPathComponent:pathEnd];
}


@implementation GIOMonitorCrash

// ============================================================================
#pragma mark - Properties -
// ============================================================================

@synthesize userInfo = _userInfo;
@synthesize deleteBehaviorAfterSendAll = _deleteBehaviorAfterSendAll;
@synthesize monitoring = _monitoring;
@synthesize deadlockWatchdogInterval = _deadlockWatchdogInterval;
@synthesize onCrash = _onCrash;
@synthesize bundleName = _bundleName;
@synthesize basePath = _basePath;
@synthesize introspectMemory = _introspectMemory;
@synthesize doNotIntrospectClasses = _doNotIntrospectClasses;
@synthesize demangleLanguages = _demangleLanguages;
@synthesize addConsoleLogToReport = _addConsoleLogToReport;
@synthesize printPreviousLog = _printPreviousLog;
@synthesize maxReportCount = _maxReportCount;
@synthesize uncaughtExceptionHandler = _uncaughtExceptionHandler;
@synthesize currentSnapshotUserReportedExceptionHandler = _currentSnapshotUserReportedExceptionHandler;

// ============================================================================
#pragma mark - Lifecycle -
// ============================================================================

+ (instancetype) sharedInstance
{
    static GIOMonitorCrash *sharedInstance = nil;
    static dispatch_once_t onceToken;

    dispatch_once(&onceToken, ^{
        sharedInstance = [[GIOMonitorCrash alloc] init];
    });
    return sharedInstance;
}

- (id) init
{
    return [self initWithBasePath:getBasePath()];
}

- (id) initWithBasePath:(NSString *)basePath
{
    if((self = [super init]))
    {
        self.bundleName = getBundleName();
        self.basePath = basePath;
        if(self.basePath == nil)
        {
            GIOMonitorCrashLOG_ERROR(@"Failed to initialize crash handler. Crash reporting disabled.");
            return nil;
        }
        self.deleteBehaviorAfterSendAll = GIOMonitorCrashCDeleteAlways;
        self.introspectMemory = YES;
        self.maxReportCount = 5;
        self.monitoring = GIOMonitorCrashMonitorTypeProductionSafeMinimal;
    }
    return self;
}


// ============================================================================
#pragma mark - API -
// ============================================================================

- (NSDictionary*) userInfo
{
   return _userInfo;
}

- (void) setUserInfo:(NSDictionary*) userInfo
{
    @synchronized (self)
    {
        NSError* error = nil;
        NSData* userInfoJSON = nil;
        if(userInfo != nil)
        {
//            userInfoJSON = [self nullTerminated:[GIOMonitorCrashJSONCodec encode:userInfo
//                                                            options:GIOMonitorCrashJSONEncodeOptionSorted
//                                                              error:&error]];
//            if(error != NULL)
//            {
//                GIOMonitorCrashLOG_ERROR(@"Could not serialize user info: %@", error);
//                return;
//            }
        }

        _userInfo = userInfo;
        gioMonitorCrash_setUserInfoJSON([userInfoJSON bytes]);
    }
}

- (void) setMonitoring:(GIOMonitorCrashMonitorType)monitoring
{

}

- (void) setOnCrash:(GIOMonitorCrashReportWriteCallback) onCrash
{
    _onCrash = onCrash;
    gioMonitorCrash_setCrashNotifyCallback(onCrash);
}

- (void) setIntrospectMemory:(BOOL) introspectMemory
{
    _introspectMemory = introspectMemory;
    gioMonitorCrash_setIntrospectMemory(introspectMemory);
}

- (void) setDoNotIntrospectClasses:(NSArray *)doNotIntrospectClasses
{
    _doNotIntrospectClasses = doNotIntrospectClasses;
    NSUInteger count = [doNotIntrospectClasses count];
    if(count == 0)
    {
        gioMonitorCrash_setDoNotIntrospectClasses(nil, 0);
    }
    else
    {
        NSMutableData* data = [NSMutableData dataWithLength:count * sizeof(const char*)];
        const char** classes = data.mutableBytes;
        for(unsigned i = 0; i < count; i++)
        {
            classes[i] = [[doNotIntrospectClasses objectAtIndex:i] cStringUsingEncoding:NSUTF8StringEncoding];
        }
        gioMonitorCrash_setDoNotIntrospectClasses(classes, (int)count);
    }
}

- (void) setMaxReportCount:(int)maxReportCount
{
    _maxReportCount = maxReportCount;
    gioMonitorCrash_setMaxReportCount(maxReportCount);
}

- (NSDictionary*) systemInfo
{
    GIOMonitorCrash_MonitorContext fakeEvent = {0};
//    gioMonitorCrashCM_system_getAPI()->addContextualInfoToEvent(&fakeEvent);
    NSMutableDictionary* dict = [NSMutableDictionary new];

#define COPY_STRING(A) if (fakeEvent.System.A) dict[@#A] = [NSString stringWithUTF8String:fakeEvent.System.A]
#define COPY_PRIMITIVE(A) dict[@#A] = @(fakeEvent.System.A)
    COPY_STRING(systemName);
    COPY_STRING(systemVersion);
    COPY_STRING(machine);
    COPY_STRING(model);
    COPY_STRING(kernelVersion);
    COPY_STRING(osVersion);
    COPY_PRIMITIVE(isJailbroken);
    COPY_STRING(bootTime);
    COPY_STRING(appStartTime);
    COPY_STRING(executablePath);
    COPY_STRING(executableName);
    COPY_STRING(bundleID);
    COPY_STRING(bundleName);
    COPY_STRING(bundleVersion);
    COPY_STRING(bundleShortVersion);
    COPY_STRING(appID);
    COPY_STRING(cpuArchitecture);
    COPY_PRIMITIVE(cpuType);
    COPY_PRIMITIVE(cpuSubType);
    COPY_PRIMITIVE(binaryCPUType);
    COPY_PRIMITIVE(binaryCPUSubType);
    COPY_STRING(timezone);
    COPY_STRING(processName);
    COPY_PRIMITIVE(processID);
    COPY_PRIMITIVE(parentProcessID);
    COPY_STRING(deviceAppHash);
    COPY_STRING(buildType);
    COPY_PRIMITIVE(storageSize);
    COPY_PRIMITIVE(memorySize);
    COPY_PRIMITIVE(freeMemory);
    COPY_PRIMITIVE(usableMemory);

    return dict;
}

- (BOOL) install
{
    _monitoring = gioMonitorCrash_install(self.bundleName.UTF8String,
                                          self.basePath.UTF8String);
    if(self.monitoring == 0)
    {
        return false;
    }

    return true;
}

- (void) deleteAllReports
{
    gioMonitorCrash_deleteAllReports();
}

- (void) deleteReportWithID:(NSNumber*) reportID
{
    gioMonitorCrash_deleteReportWithID([reportID longValue]);
}


- (int) reportCount
{
    return gioMonitorCrash_getReportCount();
}

- (NSArray*)reportIDs
{
    int reportCount = gioMonitorCrash_getReportCount();
    int64_t reportIDsC[reportCount];
    reportCount = gioMonitorCrash_getReportIDs(reportIDsC, reportCount);
    NSMutableArray* reportIDs = [NSMutableArray arrayWithCapacity:(NSUInteger)reportCount];
    for(int i = 0; i < reportCount; i++)
    {
        [reportIDs addObject:@(reportIDsC[i])];
    }
    return reportIDs;
}

- (void) setAddConsoleLogToReport:(BOOL) shouldAddConsoleLogToReport
{
    _addConsoleLogToReport = shouldAddConsoleLogToReport;
    gioMonitorCrash_setAddConsoleLogToReport(shouldAddConsoleLogToReport);
}

- (void) setPrintPreviousLog:(BOOL) shouldPrintPreviousLog
{
    _printPreviousLog = shouldPrintPreviousLog;
    gioMonitorCrash_setPrintPreviousLog(shouldPrintPreviousLog);
}


// ============================================================================
#pragma mark - Utility -
// ============================================================================

- (NSMutableData*) nullTerminated:(NSData*) data
{
    if(data == nil)
    {
        return NULL;
    }
    NSMutableData* mutable = [NSMutableData dataWithData:data];
    [mutable appendBytes:"\0" length:1];
    return mutable;
}

@end


//! Project version number for GIOMonitorCrashFramework.
const double GIOMonitorCrashFrameworkVersionNumber = 1.1518;

//! Project version string for GIOMonitorCrashFramework.
const unsigned char GIOMonitorCrashFrameworkVersionString[] = "1.15.18";
