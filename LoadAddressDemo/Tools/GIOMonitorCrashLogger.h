//
//  GIOMonitorCrashLogger.h
//
//  Created by Karl Stenerud on 11-06-25.
//
//  Copyright (c) 2011 Karl Stenerud. All rights reserved.
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


/**
 * GIOMonitorCrashLogger
 * ========
 *
 * Prints log entries to the console consisting of:
 * - Level (Error, Warn, Info, Debug, Trace)
 * - File
 * - Line
 * - Function
 * - Message
 *
 * Allows setting the minimum logging level in the preprocessor.
 *
 * Works in C or Objective-C contexts, with or without ARC, using CLANG or GCC.
 *
 *
 * =====
 * USAGE
 * =====
 *
 * Set the log level in your "Preprocessor Macros" build setting. You may choose
 * TRACE, DEBUG, INFO, WARN, ERROR. If nothing is set, it defaults to ERROR.
 *
 * Example: GIOMonitorCrashLogger_Level=WARN
 *
 * Anything below the level specified for GIOMonitorCrashLogger_Level will not be compiled
 * or printed.
 *
 *
 * Next, include the header file:
 *
 * #include "GIOMonitorCrashLogger.h"
 *
 *
 * Next, call the logger functions from your code (using objective-c strings
 * in objective-C files and regular strings in regular C files):
 *
 * Code:
 *    GIOMonitorCrashLOG_ERROR(@"Some error message");
 *
 * Prints:
 *    2011-07-16 05:41:01.379 TestApp[4439:f803] ERROR: SomeClass.m (21): -[SomeFunction]: Some error message
 *
 * Code:
 *    GIOMonitorCrashLOG_INFO(@"Info about %@", someObject);
 *
 * Prints:
 *    2011-07-16 05:44:05.239 TestApp[4473:f803] INFO : SomeClass.m (20): -[SomeFunction]: Info about <NSObject: 0xb622840>
 *
 *
 * The "BASIC" versions of the macros behave exactly like NSLog() or printf(),
 * except they respect the GIOMonitorCrashLogger_Level setting:
 *
 * Code:
 *    GIOMonitorCrashLOGBASIC_ERROR(@"A basic log entry");
 *
 * Prints:
 *    2011-07-16 05:44:05.916 TestApp[4473:f803] A basic log entry
 *
 *
 * NOTE: In C files, use "" instead of @"" in the format field. Logging calls
 *       in C files do not print the NSLog preamble:
 *
 * Objective-C version:
 *    GIOMonitorCrashLOG_ERROR(@"Some error message");
 *
 *    2011-07-16 05:41:01.379 TestApp[4439:f803] ERROR: SomeClass.m (21): -[SomeFunction]: Some error message
 *
 * C version:
 *    GIOMonitorCrashLOG_ERROR("Some error message");
 *
 *    ERROR: SomeClass.c (21): SomeFunction(): Some error message
 *
 *
 * =============
 * LOCAL LOGGING
 * =============
 *
 * You can control logging messages at the local file level using the
 * "GIOMonitorCrashLogger_LocalLevel" define. Note that it must be defined BEFORE
 * including GIOMonitorCrashLogger.h
 *
 * The GIOMonitorCrashLOG_XX() and GIOMonitorCrashLOGBASIC_XX() macros will print out based on the LOWER
 * of GIOMonitorCrashLogger_Level and GIOMonitorCrashLogger_LocalLevel, so if GIOMonitorCrashLogger_Level is DEBUG
 * and GIOMonitorCrashLogger_LocalLevel is TRACE, it will print all the way down to the trace
 * level for the local file where GIOMonitorCrashLogger_LocalLevel was defined, and to the
 * debug level everywhere else.
 *
 * Example:
 *
 * // GIOMonitorCrashLogger_LocalLevel, if defined, MUST come BEFORE including GIOMonitorCrashLogger.h
 * #define GIOMonitorCrashLogger_LocalLevel TRACE
 * #import "GIOMonitorCrashLogger.h"
 *
 *
 * ===============
 * IMPORTANT NOTES
 * ===============
 *
 * The C logger changes its behavior depending on the value of the preprocessor
 * define GIOMonitorCrashLogger_CBufferSize.
 *
 * If GIOMonitorCrashLogger_CBufferSize is > 0, the C logger will behave in an async-safe
 * manner, calling write() instead of printf(). Any log messages that exceed the
 * length specified by GIOMonitorCrashLogger_CBufferSize will be truncated.
 *
 * If GIOMonitorCrashLogger_CBufferSize == 0, the C logger will use printf(), and there will
 * be no limit on the log message length.
 *
 * GIOMonitorCrashLogger_CBufferSize can only be set as a preprocessor define, and will
 * default to 1024 if not specified during compilation.
 */


// ============================================================================
#pragma mark - (internal) -
// ============================================================================


#ifndef HDR_GIOMonitorCrashLogger_h
#define HDR_GIOMonitorCrashLogger_h

#ifdef __cplusplus
extern "C" {
#endif


#include <stdbool.h>


#ifdef __OBJC__

#import <CoreFoundation/CoreFoundation.h>

void i_gioMonitorCrashLog_logObjC(const char* level,
                     const char* file,
                     int line,
                     const char* function,
                     CFStringRef fmt, ...);

void i_gioMonitorCrashLog_logObjCBasic(CFStringRef fmt, ...);

#define i_GIOMonitorCrashLOG_FULL(LEVEL,FILE,LINE,FUNCTION,FMT,...) i_gioMonitorCrashLog_logObjC(LEVEL,FILE,LINE,FUNCTION,(__bridge CFStringRef)FMT,##__VA_ARGS__)
#define i_GIOMonitorCrashLOG_BASIC(FMT, ...) i_gioMonitorCrashLog_logObjCBasic((__bridge CFStringRef)FMT,##__VA_ARGS__)

#else // __OBJC__

void i_gioMonitorCrashLog_logC(const char* level,
                  const char* file,
                  int line,
                  const char* function,
                  const char* fmt, ...);

void i_gioMonitorCrashLog_logCBasic(const char* fmt, ...);

#define i_GIOMonitorCrashLOG_FULL i_gioMonitorCrashLog_logC
#define i_GIOMonitorCrashLOG_BASIC i_gioMonitorCrashLog_logCBasic

#endif // __OBJC__


/* Back up any existing defines by the same name */
#ifdef GIOMonitorCrash_NONE
    #define GIOMonitorCrashLOG_BAK_NONE GIOMonitorCrash_NONE
    #undef GIOMonitorCrash_NONE
#endif
#ifdef ERROR
    #define GIOMonitorCrashLOG_BAK_ERROR ERROR
    #undef ERROR
#endif
#ifdef WARN
    #define GIOMonitorCrashLOG_BAK_WARN WARN
    #undef WARN
#endif
#ifdef INFO
    #define GIOMonitorCrashLOG_BAK_INFO INFO
    #undef INFO
#endif
#ifdef DEBUG
    #define GIOMonitorCrashLOG_BAK_DEBUG DEBUG
    #undef DEBUG
#endif
#ifdef TRACE
    #define GIOMonitorCrashLOG_BAK_TRACE TRACE
    #undef TRACE
#endif


#define GIOMonitorCrashLogger_Level_None   0
#define GIOMonitorCrashLogger_Level_Error 10
#define GIOMonitorCrashLogger_Level_Warn  20
#define GIOMonitorCrashLogger_Level_Info  30
#define GIOMonitorCrashLogger_Level_Debug 40
#define GIOMonitorCrashLogger_Level_Trace 50

#define GIOMonitorCrash_NONE  GIOMonitorCrashLogger_Level_None
#define ERROR GIOMonitorCrashLogger_Level_Error
#define WARN  GIOMonitorCrashLogger_Level_Warn
#define INFO  GIOMonitorCrashLogger_Level_Info
#define DEBUG GIOMonitorCrashLogger_Level_Debug
#define TRACE GIOMonitorCrashLogger_Level_Trace


#ifndef GIOMonitorCrashLogger_Level
    #define GIOMonitorCrashLogger_Level GIOMonitorCrashLogger_Level_Error
#endif

#ifndef GIOMonitorCrashLogger_LocalLevel
    #define GIOMonitorCrashLogger_LocalLevel GIOMonitorCrashLogger_Level_None
#endif

#define a_GIOMonitorCrashLOG_FULL(LEVEL, FMT, ...) \
    i_GIOMonitorCrashLOG_FULL(LEVEL, \
                 __FILE__, \
                 __LINE__, \
                 __PRETTY_FUNCTION__, \
                 FMT, \
                 ##__VA_ARGS__)



// ============================================================================
#pragma mark - API -
// ============================================================================

/** Set the filename to log to.
 *
 * @param filename The file to write to (NULL = write to stdout).
 *
 * @param overwrite If true, overwrite the log file.
 */
bool gioMonitorCrashLog_setLogFilename(const char* filename, bool overwrite);

/** Clear the log file. */
bool gioMonitorCrashLog_clearLogFile(void);

/** Tests if the logger would print at the specified level.
 *
 * @param LEVEL The level to test for. One of:
 *            GIOMonitorCrashLogger_Level_Error,
 *            GIOMonitorCrashLogger_Level_Warn,
 *            GIOMonitorCrashLogger_Level_Info,
 *            GIOMonitorCrashLogger_Level_Debug,
 *            GIOMonitorCrashLogger_Level_Trace,
 *
 * @return TRUE if the logger would print at the specified level.
 */
#define GIOMonitorCrashLOG_PRINTS_AT_LEVEL(LEVEL) \
    (GIOMonitorCrashLogger_Level >= LEVEL || GIOMonitorCrashLogger_LocalLevel >= LEVEL)

/** Log a message regardless of the log settings.
 * Normal version prints out full context. Basic version prints directly.
 *
 * @param FMT The format specifier, followed by its arguments.
 */
#define GIOMonitorCrashLOG_ALWAYS(FMT, ...) a_GIOMonitorCrashLOG_FULL("FORCE", FMT, ##__VA_ARGS__)
#define GIOMonitorCrashLOGBASIC_ALWAYS(FMT, ...) i_GIOMonitorCrashLOG_BASIC(FMT, ##__VA_ARGS__)


/** Log an error.
 * Normal version prints out full context. Basic version prints directly.
 *
 * @param FMT The format specifier, followed by its arguments.
 */
#if GIOMonitorCrashLOG_PRINTS_AT_LEVEL(GIOMonitorCrashLogger_Level_Error)
    #define GIOMonitorCrashLOG_ERROR(FMT, ...) a_GIOMonitorCrashLOG_FULL("ERROR", FMT, ##__VA_ARGS__)
    #define GIOMonitorCrashLOGBASIC_ERROR(FMT, ...) i_GIOMonitorCrashLOG_BASIC(FMT, ##__VA_ARGS__)
#else
    #define GIOMonitorCrashLOG_ERROR(FMT, ...)
    #define GIOMonitorCrashLOGBASIC_ERROR(FMT, ...)
#endif

/** Log a warning.
 * Normal version prints out full context. Basic version prints directly.
 *
 * @param FMT The format specifier, followed by its arguments.
 */
#if GIOMonitorCrashLOG_PRINTS_AT_LEVEL(GIOMonitorCrashLogger_Level_Warn)
    #define GIOMonitorCrashLOG_WARN(FMT, ...)  a_GIOMonitorCrashLOG_FULL("WARN ", FMT, ##__VA_ARGS__)
    #define GIOMonitorCrashLOGBASIC_WARN(FMT, ...) i_GIOMonitorCrashLOG_BASIC(FMT, ##__VA_ARGS__)
#else
    #define GIOMonitorCrashLOG_WARN(FMT, ...)
    #define GIOMonitorCrashLOGBASIC_WARN(FMT, ...)
#endif

/** Log an info message.
 * Normal version prints out full context. Basic version prints directly.
 *
 * @param FMT The format specifier, followed by its arguments.
 */
#if GIOMonitorCrashLOG_PRINTS_AT_LEVEL(GIOMonitorCrashLogger_Level_Info)
    #define GIOMonitorCrashLOG_INFO(FMT, ...)  a_GIOMonitorCrashLOG_FULL("INFO ", FMT, ##__VA_ARGS__)
    #define GIOMonitorCrashLOGBASIC_INFO(FMT, ...) i_GIOMonitorCrashLOG_BASIC(FMT, ##__VA_ARGS__)
#else
    #define GIOMonitorCrashLOG_INFO(FMT, ...)
    #define GIOMonitorCrashLOGBASIC_INFO(FMT, ...)
#endif

/** Log a debug message.
 * Normal version prints out full context. Basic version prints directly.
 *
 * @param FMT The format specifier, followed by its arguments.
 */
#if GIOMonitorCrashLOG_PRINTS_AT_LEVEL(GIOMonitorCrashLogger_Level_Debug)
    #define GIOMonitorCrashLOG_DEBUG(FMT, ...) a_GIOMonitorCrashLOG_FULL("DEBUG", FMT, ##__VA_ARGS__)
    #define GIOMonitorCrashLOGBASIC_DEBUG(FMT, ...) i_GIOMonitorCrashLOG_BASIC(FMT, ##__VA_ARGS__)
#else
    #define GIOMonitorCrashLOG_DEBUG(FMT, ...)
    #define GIOMonitorCrashLOGBASIC_DEBUG(FMT, ...)
#endif

/** Log a trace message.
 * Normal version prints out full context. Basic version prints directly.
 *
 * @param FMT The format specifier, followed by its arguments.
 */
#if GIOMonitorCrashLOG_PRINTS_AT_LEVEL(GIOMonitorCrashLogger_Level_Trace)
    #define GIOMonitorCrashLOG_TRACE(FMT, ...) a_GIOMonitorCrashLOG_FULL("TRACE", FMT, ##__VA_ARGS__)
    #define GIOMonitorCrashLOGBASIC_TRACE(FMT, ...) i_GIOMonitorCrashLOG_BASIC(FMT, ##__VA_ARGS__)
#else
    #define GIOMonitorCrashLOG_TRACE(FMT, ...)
    #define GIOMonitorCrashLOGBASIC_TRACE(FMT, ...)
#endif



// ============================================================================
#pragma mark - (internal) -
// ============================================================================

/* Put everything back to the way we found it. */
#undef ERROR
#ifdef GIOMonitorCrashLOG_BAK_ERROR
    #define ERROR GIOMonitorCrashLOG_BAK_ERROR
    #undef GIOMonitorCrashLOG_BAK_ERROR
#endif
#undef WARNING
#ifdef GIOMonitorCrashLOG_BAK_WARN
    #define WARNING GIOMonitorCrashLOG_BAK_WARN
    #undef GIOMonitorCrashLOG_BAK_WARN
#endif
#undef INFO
#ifdef GIOMonitorCrashLOG_BAK_INFO
    #define INFO GIOMonitorCrashLOG_BAK_INFO
    #undef GIOMonitorCrashLOG_BAK_INFO
#endif
#undef DEBUG
#ifdef GIOMonitorCrashLOG_BAK_DEBUG
    #define DEBUG GIOMonitorCrashLOG_BAK_DEBUG
    #undef GIOMonitorCrashLOG_BAK_DEBUG
#endif
#undef TRACE
#ifdef GIOMonitorCrashLOG_BAK_TRACE
    #define TRACE GIOMonitorCrashLOG_BAK_TRACE
    #undef GIOMonitorCrashLOG_BAK_TRACE
#endif


#ifdef __cplusplus
}
#endif

#endif // HDR_GIOMonitorCrashLogger_h
