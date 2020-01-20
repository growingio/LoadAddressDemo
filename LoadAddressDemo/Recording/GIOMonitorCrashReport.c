//
//  GIOMonitorCrashReport.m
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


#include "GIOMonitorCrashReport.h"

#include "GIOMonitorCrashReportFields.h"
#include "GIOMonitorCrashReportWriter.h"
#include "GIOMonitorCrashDynamicLinker.h"
#include "GIOMonitorCrashFileUtils.h"
#include "GIOMonitorCrashJSONCodec.h"
#include "GIOMonitorCrashCPU.h"
#include "GIOMonitorCrashMemory.h"
//#include "GIOMonitorCrashMach.h"
#include "GIOMonitorCrashThread.h"
#include "GIOMonitorCrashObjC.h"
//#include "GIOMonitorCrashSignalInfo.h"
//#include "GIOMonitorCrashMonitor_Zombie.h"
#include "GIOMonitorCrashString.h"
//#include "GIOMonitorCrashReportVersion.h"
#include "GIOMonitorCrashStackCursor_Backtrace.h"
#include "GIOMonitorCrashStackCursor_MachineContext.h"
#include "GIOMonitorCrashSystemCapabilities.h"
#include "GIOMonitorCrashCachedData.h"

//#define GIOMonitorCrashLogger_LocalLevel TRACE
#include "GIOMonitorCrashLogger.h"
//#include "GrowingMonitorFindSubString.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


// ============================================================================
#pragma mark - Constants -
// ============================================================================

/** Default number of objects, subobjects, and ivars to record from a memory loc */
#define kDefaultMemorySearchDepth 15

/** How far to search the stack (in pointer sized jumps) for notable data. */
#define kStackNotableSearchBackDistance 20
#define kStackNotableSearchForwardDistance 10

/** How much of the stack to dump (in pointer sized jumps). */
#define kStackContentsPushedDistance 20
#define kStackContentsPoppedDistance 10
#define kStackContentsTotalDistance (kStackContentsPushedDistance + kStackContentsPoppedDistance)

/** The minimum length for a valid string. */
#define kMinStringLength 4


// ============================================================================
#pragma mark - JSON Encoding -
// ============================================================================

#define getJsonContext(REPORT_WRITER) ((GIOMonitorCrashJSONEncodeContext*)((REPORT_WRITER)->context))

/** Used for writing hex string values. */
static const char g_hexNybbles[] =
{
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

// ============================================================================
#pragma mark - Runtime Config -
// ============================================================================

typedef struct
{
    /** If YES, introspect memory contents during a crash.
     * Any Objective-C objects or C strings near the stack pointer or referenced by
     * cpu registers or exceptions will be recorded in the crash report, along with
     * their contents.
     */
    bool enabled;

    /** List of classes that should never be introspected.
     * Whenever a class in this list is encountered, only the class name will be recorded.
     */
    const char** restrictedClasses;
    int restrictedClassesCount;
} GIOMonitorCrash_IntrospectionRules;

static const char* g_userInfoJSON;
static GIOMonitorCrash_IntrospectionRules g_introspectionRules;
static GIOMonitorCrashReportWriteCallback g_userSectionWriteCallback;


#pragma mark Callbacks

static void addBooleanElement(const GIOMonitorCrashReportWriter* const writer, const char* const key, const bool value)
{
    gioMonitorCrashJSON_addBooleanElement(getJsonContext(writer), key, value);
}

static void addFloatingPointElement(const GIOMonitorCrashReportWriter* const writer, const char* const key, const double value)
{
    gioMonitorCrashJSON_addFloatingPointElement(getJsonContext(writer), key, value);
}

static void addIntegerElement(const GIOMonitorCrashReportWriter* const writer, const char* const key, const int64_t value)
{
    gioMonitorCrashJSON_addIntegerElement(getJsonContext(writer), key, value);
}

static void addUIntegerElement(const GIOMonitorCrashReportWriter* const writer, const char* const key, const uint64_t value)
{
    gioMonitorCrashJSON_addIntegerElement(getJsonContext(writer), key, (int64_t)value);
}

static void addStringElement(const GIOMonitorCrashReportWriter* const writer, const char* const key, const char* const value)
{
    gioMonitorCrashJSON_addStringElement(getJsonContext(writer), key, value, GIOMonitorCrashJSON_SIZE_AUTOMATIC);
}

static void addTextFileElement(const GIOMonitorCrashReportWriter* const writer, const char* const key, const char* const filePath)
{
    const int fd = open(filePath, O_RDONLY);
    if(fd < 0)
    {
        GIOMonitorCrashLOG_ERROR("Could not open file %s: %s", filePath, strerror(errno));
        return;
    }

    if(gioMonitorCrashJSON_beginStringElement(getJsonContext(writer), key) != GIOMonitorCrashJSON_OK)
    {
        GIOMonitorCrashLOG_ERROR("Could not start string element");
        goto done;
    }

    char buffer[512];
    int bytesRead;
    for(bytesRead = (int)read(fd, buffer, sizeof(buffer));
        bytesRead > 0;
        bytesRead = (int)read(fd, buffer, sizeof(buffer)))
    {
        if(gioMonitorCrashJSON_appendStringElement(getJsonContext(writer), buffer, bytesRead) != GIOMonitorCrashJSON_OK)
        {
            GIOMonitorCrashLOG_ERROR("Could not append string element");
            goto done;
        }
    }

done:
    gioMonitorCrashJSON_endStringElement(getJsonContext(writer));
    close(fd);
}

static void addDataElement(const GIOMonitorCrashReportWriter* const writer,
                           const char* const key,
                           const char* const value,
                           const int length)
{
    gioMonitorCrashJSON_addDataElement(getJsonContext(writer), key, value, length);
}

static void beginDataElement(const GIOMonitorCrashReportWriter* const writer, const char* const key)
{
    gioMonitorCrashJSON_beginDataElement(getJsonContext(writer), key);
}

static void appendDataElement(const GIOMonitorCrashReportWriter* const writer, const char* const value, const int length)
{
    gioMonitorCrashJSON_appendDataElement(getJsonContext(writer), value, length);
}

static void endDataElement(const GIOMonitorCrashReportWriter* const writer)
{
    gioMonitorCrashJSON_endDataElement(getJsonContext(writer));
}

static void addUUIDElement(const GIOMonitorCrashReportWriter* const writer, const char* const key, const unsigned char* const value)
{
    if(value == NULL)
    {
        gioMonitorCrashJSON_addNullElement(getJsonContext(writer), key);
    }
    else
    {
        char uuidBuffer[37];
        const unsigned char* src = value;
        char* dst = uuidBuffer;
        for(int i = 0; i < 4; i++)
        {
            *dst++ = g_hexNybbles[(*src>>4)&15];
            *dst++ = g_hexNybbles[(*src++)&15];
        }
        *dst++ = '-';
        for(int i = 0; i < 2; i++)
        {
            *dst++ = g_hexNybbles[(*src>>4)&15];
            *dst++ = g_hexNybbles[(*src++)&15];
        }
        *dst++ = '-';
        for(int i = 0; i < 2; i++)
        {
            *dst++ = g_hexNybbles[(*src>>4)&15];
            *dst++ = g_hexNybbles[(*src++)&15];
        }
        *dst++ = '-';
        for(int i = 0; i < 2; i++)
        {
            *dst++ = g_hexNybbles[(*src>>4)&15];
            *dst++ = g_hexNybbles[(*src++)&15];
        }
        *dst++ = '-';
        for(int i = 0; i < 6; i++)
        {
            *dst++ = g_hexNybbles[(*src>>4)&15];
            *dst++ = g_hexNybbles[(*src++)&15];
        }

        gioMonitorCrashJSON_addStringElement(getJsonContext(writer), key, uuidBuffer, (int)(dst - uuidBuffer));
    }
}

static void addJSONElement(const GIOMonitorCrashReportWriter* const writer,
                           const char* const key,
                           const char* const jsonElement,
                           bool closeLastContainer)
{
    int jsonResult = gioMonitorCrashJSON_addJSONElement(getJsonContext(writer),
                                           key,
                                           jsonElement,
                                           (int)strlen(jsonElement),
                                           closeLastContainer);
    if(jsonResult != GIOMonitorCrashJSON_OK)
    {
        char errorBuff[100];
        snprintf(errorBuff,
                 sizeof(errorBuff),
                 "Invalid JSON data: %s",
                 gioMonitorCrashJSON_stringForError(jsonResult));
        gioMonitorCrashJSON_beginObject(getJsonContext(writer), key);
        gioMonitorCrashJSON_addStringElement(getJsonContext(writer),
                                GIOMonitorCrashField_Error,
                                errorBuff,
                                GIOMonitorCrashJSON_SIZE_AUTOMATIC);
        gioMonitorCrashJSON_addStringElement(getJsonContext(writer),
                                GIOMonitorCrashField_JSONData,
                                jsonElement,
                                GIOMonitorCrashJSON_SIZE_AUTOMATIC);
        gioMonitorCrashJSON_endContainer(getJsonContext(writer));
    }
}

static void addJSONElementFromFile(const GIOMonitorCrashReportWriter* const writer,
                                   const char* const key,
                                   const char* const filePath,
                                   bool closeLastContainer)
{
    gioMonitorCrashJSON_addJSONFromFile(getJsonContext(writer), key, filePath, closeLastContainer);
}

static void beginObject(const GIOMonitorCrashReportWriter* const writer, const char* const key)
{
    gioMonitorCrashJSON_beginObject(getJsonContext(writer), key);
}

static void beginArray(const GIOMonitorCrashReportWriter* const writer, const char* const key)
{
    gioMonitorCrashJSON_beginArray(getJsonContext(writer), key);
}

static void endContainer(const GIOMonitorCrashReportWriter* const writer)
{
    gioMonitorCrashJSON_endContainer(getJsonContext(writer));
}


static void addTextLinesFromFile(const GIOMonitorCrashReportWriter* const writer, const char* const key, const char* const filePath)
{
    char readBuffer[1024];
    GIOMonitorCrashBufferedReader reader;
    if(!gioMonitorCrashFileUtils_openBufferedReader(&reader, filePath, readBuffer, sizeof(readBuffer)))
    {
        return;
    }
    char buffer[1024];
    beginArray(writer, key);
    {
        for(;;)
        {
            int length = sizeof(buffer);
            gioMonitorCrashFileUtils_readBufferedReaderUntilChar(&reader, '\n', buffer, &length);
            if(length <= 0)
            {
                break;
            }
            buffer[length - 1] = '\0';
            gioMonitorCrashJSON_addStringElement(getJsonContext(writer), NULL, buffer, GIOMonitorCrashJSON_SIZE_AUTOMATIC);
        }
    }
    endContainer(writer);
    gioMonitorCrashFileUtils_closeBufferedReader(&reader);
}

static int addJSONData(const char* restrict const data, const int length, void* restrict userData)
{
    GIOMonitorCrashBufferedWriter* writer = (GIOMonitorCrashBufferedWriter*)userData;
    const bool success = gioMonitorCrashFileUtils_writeBufferedWriter(writer, data, length);
    return success ? GIOMonitorCrashJSON_OK : GIOMonitorCrashJSON_ERROR_CANNOT_ADD_DATA;
}


// ============================================================================
#pragma mark - Utility -
// ============================================================================

/** Check if a memory address points to a valid null terminated UTF-8 string.
 *
 * @param address The address to check.
 *
 * @return true if the address points to a string.
 */
static bool isValidString(const void* const address)
{
    if((void*)address == NULL)
    {
        return false;
    }

    char buffer[500];
    if((uintptr_t)address+sizeof(buffer) < (uintptr_t)address)
    {
        // Wrapped around the address range.
        return false;
    }
    if(!gioMonitorCrashMem_copySafely(address, buffer, sizeof(buffer)))
    {
        return false;
    }
    return gioMonitorCrashString_isNullTerminatedUTF8String(buffer, kMinStringLength, sizeof(buffer));
}

/** Get the backtrace for the specified machine context.
 *
 * This function will choose how to fetch the backtrace based on the crash and
 * machine context. It may store the backtrace in backtraceBuffer unless it can
 * be fetched directly from memory. Do not count on backtraceBuffer containing
 * anything. Always use the return value.
 *
 * @param crash The crash handler context.
 *
 * @param machineContext The machine context.
 *
 * @param cursor The stack cursor to fill.
 *
 * @return True if the cursor was filled.
 */
static bool getStackCursor(const GIOMonitorCrash_MonitorContext* const crash,
                           const struct GIOMonitorCrashMachineContext* const machineContext,
                           GIOMonitorCrashStackCursor *cursor)
{
    if(gioMonitorCrashMachineContext_getThreadFromContext(machineContext) == gioMonitorCrashMachineContext_getThreadFromContext(crash->offendingMachineContext))
    {
        *cursor = *((GIOMonitorCrashStackCursor*)crash->stackCursor);
        return true;
    }

    gioMonitorCrashStackCursor_initWithMachineContext(cursor, GIOMonitorCrashSC_STACK_OVERFLOW_THRESHOLD, machineContext);
    return true;
}


// ============================================================================
#pragma mark - Report Writing -
// ============================================================================

/** Write the contents of a memory location.
 * Also writes meta information about the data.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param address The memory address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeMemoryContents(const GIOMonitorCrashReportWriter* const writer,
                                const char* const key,
                                const uintptr_t address,
                                int* limit);

/** Write a string to the report.
 * This will only print the first child of the array.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param objectAddress The object's address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeNSStringContents(const GIOMonitorCrashReportWriter* const writer,
                                  const char* const key,
                                  const uintptr_t objectAddress,
                                  __unused int* limit)
{
    const void* object = (const void*)objectAddress;
    char buffer[200];
    if(gioMonitorCrashObjc_copyStringContents(object, buffer, sizeof(buffer)))
    {
        writer->addStringElement(writer, key, buffer);
    }
}

/** Write a URL to the report.
 * This will only print the first child of the array.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param objectAddress The object's address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeURLContents(const GIOMonitorCrashReportWriter* const writer,
                             const char* const key,
                             const uintptr_t objectAddress,
                             __unused int* limit)
{
    const void* object = (const void*)objectAddress;
    char buffer[200];
    if(gioMonitorCrashObjc_copyStringContents(object, buffer, sizeof(buffer)))
    {
        writer->addStringElement(writer, key, buffer);
    }
}

/** Write a date to the report.
 * This will only print the first child of the array.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param objectAddress The object's address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeDateContents(const GIOMonitorCrashReportWriter* const writer,
                              const char* const key,
                              const uintptr_t objectAddress,
                              __unused int* limit)
{
    const void* object = (const void*)objectAddress;
    writer->addFloatingPointElement(writer, key, gioMonitorCrashObjc_dateContents(object));
}

/** Write a number to the report.
 * This will only print the first child of the array.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param objectAddress The object's address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeNumberContents(const GIOMonitorCrashReportWriter* const writer,
                                const char* const key,
                                const uintptr_t objectAddress,
                                __unused int* limit)
{
    const void* object = (const void*)objectAddress;
    writer->addFloatingPointElement(writer, key, gioMonitorCrashObjc_numberAsFloat(object));
}

/** Write an array to the report.
 * This will only print the first child of the array.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param objectAddress The object's address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeArrayContents(const GIOMonitorCrashReportWriter* const writer,
                               const char* const key,
                               const uintptr_t objectAddress,
                               int* limit)
{
    const void* object = (const void*)objectAddress;
    uintptr_t firstObject;
    if(gioMonitorCrashObjc_arrayContents(object, &firstObject, 1) == 1)
    {
        writeMemoryContents(writer, key, firstObject, limit);
    }
}

/** Write out ivar information about an unknown object.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param objectAddress The object's address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeUnknownObjectContents(const GIOMonitorCrashReportWriter* const writer,
                                       const char* const key,
                                       const uintptr_t objectAddress,
                                       int* limit)
{
    (*limit)--;
    const void* object = (const void*)objectAddress;
    GIOMonitorCrashObjCIvar ivars[10];
    int8_t s8;
    int16_t s16;
    int sInt;
    int32_t s32;
    int64_t s64;
    uint8_t u8;
    uint16_t u16;
    unsigned int uInt;
    uint32_t u32;
    uint64_t u64;
    float f32;
    double f64;
    bool b;
    void* pointer;


    writer->beginObject(writer, key);
    {
        if(gioMonitorCrashObjc_isTaggedPointer(object))
        {
            writer->addIntegerElement(writer, "tagged_payload", (int64_t)gioMonitorCrashObjc_taggedPointerPayload(object));
        }
        else
        {
            const void* class = gioMonitorCrashObjc_isaPointer(object);
            int ivarCount = gioMonitorCrashObjc_ivarList(class, ivars, sizeof(ivars)/sizeof(*ivars));
            *limit -= ivarCount;
            for(int i = 0; i < ivarCount; i++)
            {
                GIOMonitorCrashObjCIvar* ivar = &ivars[i];
                switch(ivar->type[0])
                {
                    case 'c':
                        gioMonitorCrashObjc_ivarValue(object, ivar->index, &s8);
                        writer->addIntegerElement(writer, ivar->name, s8);
                        break;
                    case 'i':
                        gioMonitorCrashObjc_ivarValue(object, ivar->index, &sInt);
                        writer->addIntegerElement(writer, ivar->name, sInt);
                        break;
                    case 's':
                        gioMonitorCrashObjc_ivarValue(object, ivar->index, &s16);
                        writer->addIntegerElement(writer, ivar->name, s16);
                        break;
                    case 'l':
                        gioMonitorCrashObjc_ivarValue(object, ivar->index, &s32);
                        writer->addIntegerElement(writer, ivar->name, s32);
                        break;
                    case 'q':
                        gioMonitorCrashObjc_ivarValue(object, ivar->index, &s64);
                        writer->addIntegerElement(writer, ivar->name, s64);
                        break;
                    case 'C':
                        gioMonitorCrashObjc_ivarValue(object, ivar->index, &u8);
                        writer->addUIntegerElement(writer, ivar->name, u8);
                        break;
                    case 'I':
                        gioMonitorCrashObjc_ivarValue(object, ivar->index, &uInt);
                        writer->addUIntegerElement(writer, ivar->name, uInt);
                        break;
                    case 'S':
                        gioMonitorCrashObjc_ivarValue(object, ivar->index, &u16);
                        writer->addUIntegerElement(writer, ivar->name, u16);
                        break;
                    case 'L':
                        gioMonitorCrashObjc_ivarValue(object, ivar->index, &u32);
                        writer->addUIntegerElement(writer, ivar->name, u32);
                        break;
                    case 'Q':
                        gioMonitorCrashObjc_ivarValue(object, ivar->index, &u64);
                        writer->addUIntegerElement(writer, ivar->name, u64);
                        break;
                    case 'f':
                        gioMonitorCrashObjc_ivarValue(object, ivar->index, &f32);
                        writer->addFloatingPointElement(writer, ivar->name, f32);
                        break;
                    case 'd':
                        gioMonitorCrashObjc_ivarValue(object, ivar->index, &f64);
                        writer->addFloatingPointElement(writer, ivar->name, f64);
                        break;
                    case 'B':
                        gioMonitorCrashObjc_ivarValue(object, ivar->index, &b);
                        writer->addBooleanElement(writer, ivar->name, b);
                        break;
                    case '*':
                    case '@':
                    case '#':
                    case ':':
                        gioMonitorCrashObjc_ivarValue(object, ivar->index, &pointer);
                        writeMemoryContents(writer, ivar->name, (uintptr_t)pointer, limit);
                        break;
                    default:
                        GIOMonitorCrashLOG_DEBUG("%s: Unknown ivar type [%s]", ivar->name, ivar->type);
                }
            }
        }
    }
    writer->endContainer(writer);
}

static bool isRestrictedClass(const char* name)
{
    if(g_introspectionRules.restrictedClasses != NULL)
    {
        for(int i = 0; i < g_introspectionRules.restrictedClassesCount; i++)
        {
            if(strcmp(name, g_introspectionRules.restrictedClasses[i]) == 0)
            {
                return true;
            }
        }
    }
    return false;
}

static void writeZombieIfPresent(const GIOMonitorCrashReportWriter* const writer,
                                 const char* const key,
                                 const uintptr_t address)
{
#if GIOMonitorCrashCRASH_HAS_OBJC
//    const void* object = (const void*)address;
//    const char* zombieClassName = gioMonitorCrashZombie_className(object);
//    if(zombieClassName != NULL)
//    {
//        writer->addStringElement(writer, key, zombieClassName);
//    }
#endif
}

static bool writeObjCObject(const GIOMonitorCrashReportWriter* const writer,
                            const uintptr_t address,
                            int* limit)
{
#if GIOMonitorCrashCRASH_HAS_OBJC
    const void* object = (const void*)address;
    switch(gioMonitorCrashObjc_objectType(object))
    {
        case GIOMonitorCrashObjCTypeClass:
            writer->addStringElement(writer, GIOMonitorCrashField_Type, GIOMonitorCrashMemType_Class);
            writer->addStringElement(writer, GIOMonitorCrashField_Class, gioMonitorCrashObjc_className(object));
            return true;
        case GIOMonitorCrashObjCTypeObject:
        {
            writer->addStringElement(writer, GIOMonitorCrashField_Type, GIOMonitorCrashMemType_Object);
            const char* className = gioMonitorCrashObjc_objectClassName(object);
            writer->addStringElement(writer, GIOMonitorCrashField_Class, className);
            if(!isRestrictedClass(className))
            {
                switch(gioMonitorCrashObjc_objectClassType(object))
                {
                    case GIOMonitorCrashObjCClassTypeString:
                        writeNSStringContents(writer, GIOMonitorCrashField_Value, address, limit);
                        return true;
                    case GIOMonitorCrashObjCClassTypeURL:
                        writeURLContents(writer, GIOMonitorCrashField_Value, address, limit);
                        return true;
                    case GIOMonitorCrashObjCClassTypeDate:
                        writeDateContents(writer, GIOMonitorCrashField_Value, address, limit);
                        return true;
                    case GIOMonitorCrashObjCClassTypeArray:
                        if(*limit > 0)
                        {
                            writeArrayContents(writer, GIOMonitorCrashField_FirstObject, address, limit);
                        }
                        return true;
                    case GIOMonitorCrashObjCClassTypeNumber:
                        writeNumberContents(writer, GIOMonitorCrashField_Value, address, limit);
                        return true;
                    case GIOMonitorCrashObjCClassTypeDictionary:
                    case GIOMonitorCrashObjCClassTypeException:
                        // TODO: Implement these.
                        if(*limit > 0)
                        {
                            writeUnknownObjectContents(writer, GIOMonitorCrashField_Ivars, address, limit);
                        }
                        return true;
                    case GIOMonitorCrashObjCClassTypeUnknown:
                        if(*limit > 0)
                        {
                            writeUnknownObjectContents(writer, GIOMonitorCrashField_Ivars, address, limit);
                        }
                        return true;
                }
            }
            break;
        }
        case GIOMonitorCrashObjCTypeBlock:
            writer->addStringElement(writer, GIOMonitorCrashField_Type, GIOMonitorCrashMemType_Block);
            const char* className = gioMonitorCrashObjc_objectClassName(object);
            writer->addStringElement(writer, GIOMonitorCrashField_Class, className);
            return true;
        case GIOMonitorCrashObjCTypeUnknown:
            break;
    }
#endif

    return false;
}

/** Write the contents of a memory location.
 * Also writes meta information about the data.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param address The memory address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeMemoryContents(const GIOMonitorCrashReportWriter* const writer,
                                const char* const key,
                                const uintptr_t address,
                                int* limit)
{
    (*limit)--;
    const void* object = (const void*)address;
    writer->beginObject(writer, key);
    {
        writer->addUIntegerElement(writer, GIOMonitorCrashField_Address, address);
        writeZombieIfPresent(writer, GIOMonitorCrashField_LastDeallocObject, address);
        if(!writeObjCObject(writer, address, limit))
        {
            if(object == NULL)
            {
                writer->addStringElement(writer, GIOMonitorCrashField_Type, GIOMonitorCrashMemType_NullPointer);
            }
            else if(isValidString(object))
            {
                writer->addStringElement(writer, GIOMonitorCrashField_Type, GIOMonitorCrashMemType_String);
                writer->addStringElement(writer, GIOMonitorCrashField_Value, (const char*)object);
            }
            else
            {
                writer->addStringElement(writer, GIOMonitorCrashField_Type, GIOMonitorCrashMemType_Unknown);
            }
        }
    }
    writer->endContainer(writer);
}

static bool isValidPointer(const uintptr_t address)
{
    if(address == (uintptr_t)NULL)
    {
        return false;
    }

#if GIOMonitorCrashCRASH_HAS_OBJC
    if(gioMonitorCrashObjc_isTaggedPointer((const void*)address))
    {
        if(!gioMonitorCrashObjc_isValidTaggedPointer((const void*)address))
        {
            return false;
        }
    }
#endif

    return true;
}

static bool isNotableAddress(const uintptr_t address)
{
    if(!isValidPointer(address))
    {
        return false;
    }

    const void* object = (const void*)address;

#if GIOMonitorCrashCRASH_HAS_OBJC
//    if(gioMonitorCrashZombie_className(object) != NULL)
//    {
//        return true;
//    }
//
//    if(gioMonitorCrashObjc_objectType(object) != GIOMonitorCrashObjCTypeUnknown)
//    {
//        return true;
//    }
#endif

    if(isValidString(object))
    {
        return true;
    }

    return false;
}

/** Write the contents of a memory location only if it contains notable data.
 * Also writes meta information about the data.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param address The memory address.
 */
static void writeMemoryContentsIfNotable(const GIOMonitorCrashReportWriter* const writer,
                                         const char* const key,
                                         const uintptr_t address)
{
    if(isNotableAddress(address))
    {
        int limit = kDefaultMemorySearchDepth;
        writeMemoryContents(writer, key, address, &limit);
    }
}

/** Look for a hex value in a string and try to write whatever it references.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param string The string to search.
 */
static void writeAddressReferencedByString(const GIOMonitorCrashReportWriter* const writer,
                                           const char* const key,
                                           const char* string)
{
    uint64_t address = 0;
    if(string == NULL || !gioMonitorCrashString_extractHexValue(string, (int)strlen(string), &address))
    {
        return;
    }

    int limit = kDefaultMemorySearchDepth;
    writeMemoryContents(writer, key, (uintptr_t)address, &limit);
}

#pragma mark Backtrace

/** Write a backtrace to the report.
 *
 * @param writer The writer to write the backtrace to.
 *
 * @param key The object key, if needed.
 *
 * @param stackCursor The stack cursor to read from.
 */
static void writeBacktrace(const GIOMonitorCrashReportWriter* const writer,
                           const char* const key,
                           GIOMonitorCrashStackCursor* stackCursor)
{
    writer->beginObject(writer, key);
    {
        writer->beginArray(writer, GIOMonitorCrashField_Contents);
        {
            while(stackCursor->advanceCursor(stackCursor))
            {
                writer->beginObject(writer, NULL);
                {
                    if(stackCursor->symbolicate(stackCursor))
                    {
                        if(stackCursor->stackEntry.imageName != NULL)
                        {
                            writer->addStringElement(writer, GIOMonitorCrashField_ObjectName, gioMonitorCrashFileUtils_lastPathEntry(stackCursor->stackEntry.imageName));
                        }
                        writer->addUIntegerElement(writer, GIOMonitorCrashField_ObjectAddr, stackCursor->stackEntry.imageAddress);
                        if(stackCursor->stackEntry.symbolName != NULL)
                        {
                            writer->addStringElement(writer, GIOMonitorCrashField_SymbolName, stackCursor->stackEntry.symbolName);
                            //  拼接崩溃位置偏移
//                            writer->addStringElement(writer, GIOMonitorCrashField_SymbolName,
//                                                     growingLongToStrig(stackCursor->stackEntry.symbolName, (stackCursor->stackEntry.address - stackCursor->stackEntry.symbolAddress)));
                        }
                        writer->addUIntegerElement(writer, GIOMonitorCrashField_SymbolAddr, stackCursor->stackEntry.symbolAddress);
                    }
                    writer->addUIntegerElement(writer, GIOMonitorCrashField_InstructionAddr, stackCursor->stackEntry.address);
                }
                writer->endContainer(writer);
            }
        }
        writer->endContainer(writer);
        writer->addIntegerElement(writer, GIOMonitorCrashField_Skipped, 0);
    }
    writer->endContainer(writer);
}


#pragma mark Stack

/** Write a dump of the stack contents to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param machineContext The context to retrieve the stack from.
 *
 * @param isStackOverflow If true, the stack has overflowed.
 */
static void writeStackContents(const GIOMonitorCrashReportWriter* const writer,
                               const char* const key,
                               const struct GIOMonitorCrashMachineContext* const machineContext,
                               const bool isStackOverflow)
{
    uintptr_t sp = gioMonitorCrashCpu_stackPointer(machineContext);
    if((void*)sp == NULL)
    {
        return;
    }

    uintptr_t lowAddress = sp + (uintptr_t)(kStackContentsPushedDistance * (int)sizeof(sp) * gioMonitorCrashCpu_stackGrowDirection() * -1);
    uintptr_t highAddress = sp + (uintptr_t)(kStackContentsPoppedDistance * (int)sizeof(sp) * gioMonitorCrashCpu_stackGrowDirection());
    if(highAddress < lowAddress)
    {
        uintptr_t tmp = lowAddress;
        lowAddress = highAddress;
        highAddress = tmp;
    }
    writer->beginObject(writer, key);
    {
        writer->addStringElement(writer, GIOMonitorCrashField_GrowDirection, gioMonitorCrashCpu_stackGrowDirection() > 0 ? "+" : "-");
        writer->addUIntegerElement(writer, GIOMonitorCrashField_DumpStart, lowAddress);
        writer->addUIntegerElement(writer, GIOMonitorCrashField_DumpEnd, highAddress);
        writer->addUIntegerElement(writer, GIOMonitorCrashField_StackPtr, sp);
        writer->addBooleanElement(writer, GIOMonitorCrashField_Overflow, isStackOverflow);
        uint8_t stackBuffer[kStackContentsTotalDistance * sizeof(sp)];
        int copyLength = (int)(highAddress - lowAddress);
        if(gioMonitorCrashMem_copySafely((void*)lowAddress, stackBuffer, copyLength))
        {
            writer->addDataElement(writer, GIOMonitorCrashField_Contents, (void*)stackBuffer, copyLength);
        }
        else
        {
            writer->addStringElement(writer, GIOMonitorCrashField_Error, "Stack contents not accessible");
        }
    }
    writer->endContainer(writer);
}

/** Write any notable addresses near the stack pointer (above and below).
 *
 * @param writer The writer.
 *
 * @param machineContext The context to retrieve the stack from.
 *
 * @param backDistance The distance towards the beginning of the stack to check.
 *
 * @param forwardDistance The distance past the end of the stack to check.
 */
static void writeNotableStackContents(const GIOMonitorCrashReportWriter* const writer,
                                      const struct GIOMonitorCrashMachineContext* const machineContext,
                                      const int backDistance,
                                      const int forwardDistance)
{
    uintptr_t sp = gioMonitorCrashCpu_stackPointer(machineContext);
    if((void*)sp == NULL)
    {
        return;
    }

    uintptr_t lowAddress = sp + (uintptr_t)(backDistance * (int)sizeof(sp) * gioMonitorCrashCpu_stackGrowDirection() * -1);
    uintptr_t highAddress = sp + (uintptr_t)(forwardDistance * (int)sizeof(sp) * gioMonitorCrashCpu_stackGrowDirection());
    if(highAddress < lowAddress)
    {
        uintptr_t tmp = lowAddress;
        lowAddress = highAddress;
        highAddress = tmp;
    }
    uintptr_t contentsAsPointer;
    char nameBuffer[40];
    for(uintptr_t address = lowAddress; address < highAddress; address += sizeof(address))
    {
        if(gioMonitorCrashMem_copySafely((void*)address, &contentsAsPointer, sizeof(contentsAsPointer)))
        {
            sprintf(nameBuffer, "stack@%p", (void*)address);
            writeMemoryContentsIfNotable(writer, nameBuffer, contentsAsPointer);
        }
    }
}


#pragma mark Registers

/** Write the contents of all regular registers to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param machineContext The context to retrieve the registers from.
 */
static void writeBasicRegisters(const GIOMonitorCrashReportWriter* const writer,
                                const char* const key,
                                const struct GIOMonitorCrashMachineContext* const machineContext)
{
    char registerNameBuff[30];
    const char* registerName;
    writer->beginObject(writer, key);
    {
        const int numRegisters = gioMonitorCrashCpu_numRegisters();
        for(int reg = 0; reg < numRegisters; reg++)
        {
            registerName = gioMonitorCrashCpu_registerName(reg);
            if(registerName == NULL)
            {
                snprintf(registerNameBuff, sizeof(registerNameBuff), "r%d", reg);
                registerName = registerNameBuff;
            }
            writer->addUIntegerElement(writer, registerName,
                                       gioMonitorCrashCpu_registerValue(machineContext, reg));
        }
    }
    writer->endContainer(writer);
}

/** Write the contents of all exception registers to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param machineContext The context to retrieve the registers from.
 */
static void writeExceptionRegisters(const GIOMonitorCrashReportWriter* const writer,
                                    const char* const key,
                                    const struct GIOMonitorCrashMachineContext* const machineContext)
{
    char registerNameBuff[30];
    const char* registerName;
    writer->beginObject(writer, key);
    {
        const int numRegisters = gioMonitorCrashCpu_numExceptionRegisters();
        for(int reg = 0; reg < numRegisters; reg++)
        {
            registerName = gioMonitorCrashCpu_exceptionRegisterName(reg);
            if(registerName == NULL)
            {
                snprintf(registerNameBuff, sizeof(registerNameBuff), "r%d", reg);
                registerName = registerNameBuff;
            }
            writer->addUIntegerElement(writer,registerName,
                                       gioMonitorCrashCpu_exceptionRegisterValue(machineContext, reg));
        }
    }
    writer->endContainer(writer);
}

/** Write all applicable registers.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param machineContext The context to retrieve the registers from.
 */
static void writeRegisters(const GIOMonitorCrashReportWriter* const writer,
                           const char* const key,
                           const struct GIOMonitorCrashMachineContext* const machineContext)
{
    writer->beginObject(writer, key);
    {
        writeBasicRegisters(writer, GIOMonitorCrashField_Basic, machineContext);
        if(gioMonitorCrashMachineContext_hasValidExceptionRegisters(machineContext))
        {
            writeExceptionRegisters(writer, GIOMonitorCrashField_Exception, machineContext);
        }
    }
    writer->endContainer(writer);
}

/** Write any notable addresses contained in the CPU registers.
 *
 * @param writer The writer.
 *
 * @param machineContext The context to retrieve the registers from.
 */
static void writeNotableRegisters(const GIOMonitorCrashReportWriter* const writer,
                                  const struct GIOMonitorCrashMachineContext* const machineContext)
{
    char registerNameBuff[30];
    const char* registerName;
    const int numRegisters = gioMonitorCrashCpu_numRegisters();
    for(int reg = 0; reg < numRegisters; reg++)
    {
        registerName = gioMonitorCrashCpu_registerName(reg);
        if(registerName == NULL)
        {
            snprintf(registerNameBuff, sizeof(registerNameBuff), "r%d", reg);
            registerName = registerNameBuff;
        }
        writeMemoryContentsIfNotable(writer,
                                     registerName,
                                     (uintptr_t)gioMonitorCrashCpu_registerValue(machineContext, reg));
    }
}

#pragma mark Thread-specific

/** Write any notable addresses in the stack or registers to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param machineContext The context to retrieve the registers from.
 */
static void writeNotableAddresses(const GIOMonitorCrashReportWriter* const writer,
                                  const char* const key,
                                  const struct GIOMonitorCrashMachineContext* const machineContext)
{
    writer->beginObject(writer, key);
    {
        writeNotableRegisters(writer, machineContext);
        writeNotableStackContents(writer,
                                  machineContext,
                                  kStackNotableSearchBackDistance,
                                  kStackNotableSearchForwardDistance);
    }
    writer->endContainer(writer);
}

/** Write information about a thread to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param crash The crash handler context.
 *
 * @param machineContext The context whose thread to write about.
 *
 * @param shouldWriteNotableAddresses If true, write any notable addresses found.
 */
static void writeThread(const GIOMonitorCrashReportWriter* const writer,
                        const char* const key,
                        const GIOMonitorCrash_MonitorContext* const crash,
                        const struct GIOMonitorCrashMachineContext* const machineContext,
                        const int threadIndex,
                        const bool shouldWriteNotableAddresses)
{
    bool isCrashedThread = gioMonitorCrashMachineContext_isCrashedContext(machineContext);
    GIOMonitorCrashThread thread = gioMonitorCrashMachineContext_getThreadFromContext(machineContext);
    GIOMonitorCrashLOG_DEBUG("Writing thread %x (index %d). is crashed: %d", thread, threadIndex, isCrashedThread);

    GIOMonitorCrashStackCursor stackCursor;
    bool hasBacktrace = getStackCursor(crash, machineContext, &stackCursor);

    writer->beginObject(writer, key);
    {
        if(hasBacktrace)
        {
            writeBacktrace(writer, GIOMonitorCrashField_Backtrace, &stackCursor);
        }
        if(gioMonitorCrashMachineContext_canHaveCPUState(machineContext))
        {
            writeRegisters(writer, GIOMonitorCrashField_Registers, machineContext);
        }
        writer->addIntegerElement(writer, GIOMonitorCrashField_Index, threadIndex);
        const char* name = gioMonitorCCD_getThreadName(thread);
        if(name != NULL)
        {
            writer->addStringElement(writer, GIOMonitorCrashField_Name, name);
        }
        name = gioMonitorCCD_getQueueName(thread);
        if(name != NULL)
        {
            writer->addStringElement(writer, GIOMonitorCrashField_DispatchQueue, name);
        }
        writer->addBooleanElement(writer, GIOMonitorCrashField_Crashed, isCrashedThread);
        writer->addBooleanElement(writer, GIOMonitorCrashField_CurrentThread, thread == gioMonitorCrashThread_self());
        if(isCrashedThread)
        {
            writeStackContents(writer, GIOMonitorCrashField_Stack, machineContext, stackCursor.state.hasGivenUp);
            if(shouldWriteNotableAddresses)
            {
                writeNotableAddresses(writer, GIOMonitorCrashField_NotableAddresses, machineContext);
            }
        }
    }
    writer->endContainer(writer);
}


/** Write information about all threads to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param crash The crash handler context.
 */
static void writeAllThreads(const GIOMonitorCrashReportWriter* const writer,
                            const char* const key,
                            const GIOMonitorCrash_MonitorContext* const crash,
                            bool writeNotableAddresses)
{
    const struct GIOMonitorCrashMachineContext* const context = crash->offendingMachineContext;
    GIOMonitorCrashThread offendingThread = gioMonitorCrashMachineContext_getThreadFromContext(context);
    int threadCount = gioMonitorCrashMachineContext_getThreadCount(context);
    GIOMonitorCrashMC_NEW_CONTEXT(machineContext);
    
    // Fetch info for all threads.
    writer->beginArray(writer, key);
    {
        GIOMonitorCrashLOG_DEBUG("Writing %d threads.", threadCount);
        for(int i = 0; i < threadCount; i++)
        {
            GIOMonitorCrashThread thread = gioMonitorCrashMachineContext_getThreadAtIndex(context, i);
            if(thread == offendingThread)
            {
                writeThread(writer, NULL, crash, context, i, writeNotableAddresses);
            }
            else
            {
                gioMonitorCrashMachineContext_getContextForThread(thread, machineContext, false);
                writeThread(writer, NULL, crash, machineContext, i, writeNotableAddresses);
            }
        }
    }

    writer->endContainer(writer);
}

#pragma mark Global Report Data

/** Write information about a binary image to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param index Which image to write about.
 */
static void writeBinaryImage(const GIOMonitorCrashReportWriter* const writer,
                             const char* const key,
                             const int index)
{
    GIOMonitorCrashBinaryImage image = {0};
    if(!gioMonitorCrashDynamicLinker_getBinaryImage(index, &image))
    {
        return;
    }

    writer->beginObject(writer, key);
    {
        writer->addUIntegerElement(writer, GIOMonitorCrashField_ImageAddress, image.address);
        writer->addUIntegerElement(writer, GIOMonitorCrashField_ImageVmAddress, image.vmAddress);
        writer->addUIntegerElement(writer, GIOMonitorCrashField_ImageSize, image.size);
        writer->addStringElement(writer, GIOMonitorCrashField_Name, image.name);
        writer->addUUIDElement(writer, GIOMonitorCrashField_UUID, image.uuid);
        writer->addIntegerElement(writer, GIOMonitorCrashField_CPUType, image.cpuType);
        writer->addIntegerElement(writer, GIOMonitorCrashField_CPUSubType, image.cpuSubType);
        writer->addUIntegerElement(writer, GIOMonitorCrashField_ImageMajorVersion, image.majorVersion);
        writer->addUIntegerElement(writer, GIOMonitorCrashField_ImageMinorVersion, image.minorVersion);
        writer->addUIntegerElement(writer, GIOMonitorCrashField_ImageRevisionVersion, image.revisionVersion);
    }
    writer->endContainer(writer);
}

/** Write information about all images to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 */
static void writeBinaryImages(const GIOMonitorCrashReportWriter* const writer, const char* const key)
{
    const int imageCount = gioMonitorCrashDynamicLinker_imageCount();

    writer->beginArray(writer, key);
    {
        for(int iImg = 0; iImg < imageCount; iImg++)
        {
            writeBinaryImage(writer, NULL, iImg);
        }
    }
    writer->endContainer(writer);
}

/** Write information about system memory to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 */
static void writeMemoryInfo(const GIOMonitorCrashReportWriter* const writer,
                            const char* const key,
                            const GIOMonitorCrash_MonitorContext* const monitorContext)
{
    writer->beginObject(writer, key);
    {
        writer->addUIntegerElement(writer, GIOMonitorCrashField_Size, monitorContext->System.memorySize);
        writer->addUIntegerElement(writer, GIOMonitorCrashField_Usable, monitorContext->System.usableMemory);
        writer->addUIntegerElement(writer, GIOMonitorCrashField_Free, monitorContext->System.freeMemory);
    }
    writer->endContainer(writer);
}

/** Write information about the error leading to the crash to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param crash The crash handler context.
 */
static void writeError(const GIOMonitorCrashReportWriter* const writer,
                       const char* const key,
                       const GIOMonitorCrash_MonitorContext* const crash)
{
    writer->beginObject(writer, key);
    {
#if GIOMonitorCrashCRASH_HOST_APPLE
        writer->beginObject(writer, GIOMonitorCrashField_Mach);
        {
//            const char* machExceptionName = gioMonitorCrashMach_exceptionName(crash->mach.type);
//            const char* machCodeName = crash->mach.code == 0 ? NULL : gioMonitorCrashMach_kernelReturnCodeName(crash->mach.code);
//            writer->addUIntegerElement(writer, GIOMonitorCrashField_Exception, (unsigned)crash->mach.type);
//            if(machExceptionName != NULL)
//            {
//                writer->addStringElement(writer, GIOMonitorCrashField_ExceptionName, machExceptionName);
//            }
//            writer->addUIntegerElement(writer, GIOMonitorCrashField_Code, (unsigned)crash->mach.code);
//            if(machCodeName != NULL)
//            {
//                writer->addStringElement(writer, GIOMonitorCrashField_CodeName, machCodeName);
//            }
//            writer->addUIntegerElement(writer, GIOMonitorCrashField_Subcode, (unsigned)crash->mach.subcode);
        }
        writer->endContainer(writer);
#endif
        writer->beginObject(writer, GIOMonitorCrashField_Signal);
        {
//            const char* sigName = gioMonitorCrashSignal_signalName(crash->signal.signum);
//            const char* sigCodeName = gioMonitorCrashSignal_signalCodeName(crash->signal.signum, crash->signal.sigcode);
//            writer->addUIntegerElement(writer, GIOMonitorCrashField_Signal, (unsigned)crash->signal.signum);
//            if(sigName != NULL)
//            {
//                writer->addStringElement(writer, GIOMonitorCrashField_Name, sigName);
//            }
//            writer->addUIntegerElement(writer, GIOMonitorCrashField_Code, (unsigned)crash->signal.sigcode);
//            if(sigCodeName != NULL)
//            {
//                writer->addStringElement(writer, GIOMonitorCrashField_CodeName, sigCodeName);
//            }
        }
        writer->endContainer(writer);

        writer->addUIntegerElement(writer, GIOMonitorCrashField_Address, crash->faultAddress);
        if(crash->crashReason != NULL)
        {
            writer->addStringElement(writer, GIOMonitorCrashField_Reason, crash->crashReason);
        }

        // Gather specific info.
        switch(crash->crashType)
        {
            case GIOMonitorCrashMonitorTypeMainThreadDeadlock:
                writer->addStringElement(writer, GIOMonitorCrashField_Type, GIOMonitorCrashExcType_Deadlock);
                break;

            case GIOMonitorCrashMonitorTypeMachException:
                writer->addStringElement(writer, GIOMonitorCrashField_Type, GIOMonitorCrashExcType_Mach);
                break;

            case GIOMonitorCrashMonitorTypeCPPException:
            {
                writer->addStringElement(writer, GIOMonitorCrashField_Type, GIOMonitorCrashExcType_CPPException);
                writer->beginObject(writer, GIOMonitorCrashField_CPPException);
                {
                    writer->addStringElement(writer, GIOMonitorCrashField_Name, crash->CPPException.name);
                }
                writer->endContainer(writer);
                break;
            }
            case GIOMonitorCrashMonitorTypeNSException:
            {
                writer->addStringElement(writer, GIOMonitorCrashField_Type, GIOMonitorCrashExcType_NSException);
                writer->beginObject(writer, GIOMonitorCrashField_NSException);
                {
                    writer->addStringElement(writer, GIOMonitorCrashField_Name, crash->NSException.name);
                    writer->addStringElement(writer, GIOMonitorCrashField_UserInfo, crash->NSException.userInfo);
                    writeAddressReferencedByString(writer, GIOMonitorCrashField_ReferencedObject, crash->crashReason);
                }
                writer->endContainer(writer);
                break;
            }
            case GIOMonitorCrashMonitorTypeSignal:
                writer->addStringElement(writer, GIOMonitorCrashField_Type, GIOMonitorCrashExcType_Signal);
                break;

            case GIOMonitorCrashMonitorTypeUserReported:
            {
                writer->addStringElement(writer, GIOMonitorCrashField_Type, GIOMonitorCrashExcType_User);
                writer->beginObject(writer, GIOMonitorCrashField_UserReported);
                {
                    writer->addStringElement(writer, GIOMonitorCrashField_Name, crash->userException.name);
                    if(crash->userException.language != NULL)
                    {
                        writer->addStringElement(writer, GIOMonitorCrashField_Language, crash->userException.language);
                    }
                    if(crash->userException.lineOfCode != NULL)
                    {
                        writer->addStringElement(writer, GIOMonitorCrashField_LineOfCode, crash->userException.lineOfCode);
                    }
                    if(crash->userException.customStackTrace != NULL)
                    {
                        writer->addJSONElement(writer, GIOMonitorCrashField_Backtrace, crash->userException.customStackTrace, true);
                    }
                }
                writer->endContainer(writer);
                break;
            }
            case GIOMonitorCrashMonitorTypeSystem:
            case GIOMonitorCrashMonitorTypeApplicationState:
            case GIOMonitorCrashMonitorTypeZombie:
                GIOMonitorCrashLOG_ERROR("Crash monitor type 0x%x shouldn't be able to cause events!", crash->crashType);
                break;
        }
    }
    writer->endContainer(writer);
}

/** Write information about app runtime, etc to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param monitorContext The event monitor context.
 */
static void writeAppStats(const GIOMonitorCrashReportWriter* const writer,
                          const char* const key,
                          const GIOMonitorCrash_MonitorContext* const monitorContext)
{
    writer->beginObject(writer, key);
    {
        writer->addBooleanElement(writer, GIOMonitorCrashField_AppActive, monitorContext->AppState.applicationIsActive);
        writer->addBooleanElement(writer, GIOMonitorCrashField_AppInFG, monitorContext->AppState.applicationIsInForeground);

        writer->addIntegerElement(writer, GIOMonitorCrashField_LaunchesSinceCrash, monitorContext->AppState.launchesSinceLastCrash);
        writer->addIntegerElement(writer, GIOMonitorCrashField_SessionsSinceCrash, monitorContext->AppState.sessionsSinceLastCrash);
        writer->addFloatingPointElement(writer, GIOMonitorCrashField_ActiveTimeSinceCrash, monitorContext->AppState.activeDurationSinceLastCrash);
        writer->addFloatingPointElement(writer, GIOMonitorCrashField_BGTimeSinceCrash, monitorContext->AppState.backgroundDurationSinceLastCrash);

        writer->addIntegerElement(writer, GIOMonitorCrashField_SessionsSinceLaunch, monitorContext->AppState.sessionsSinceLaunch);
        writer->addFloatingPointElement(writer, GIOMonitorCrashField_ActiveTimeSinceLaunch, monitorContext->AppState.activeDurationSinceLaunch);
        writer->addFloatingPointElement(writer, GIOMonitorCrashField_BGTimeSinceLaunch, monitorContext->AppState.backgroundDurationSinceLaunch);
    }
    writer->endContainer(writer);
}

/** Write information about this process.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 */
static void writeProcessState(const GIOMonitorCrashReportWriter* const writer,
                              const char* const key,
                              const GIOMonitorCrash_MonitorContext* const monitorContext)
{
    writer->beginObject(writer, key);
    {
        if(monitorContext->ZombieException.address != 0)
        {
            writer->beginObject(writer, GIOMonitorCrashField_LastDeallocedNSException);
            {
                writer->addUIntegerElement(writer, GIOMonitorCrashField_Address, monitorContext->ZombieException.address);
                writer->addStringElement(writer, GIOMonitorCrashField_Name, monitorContext->ZombieException.name);
                writer->addStringElement(writer, GIOMonitorCrashField_Reason, monitorContext->ZombieException.reason);
                writeAddressReferencedByString(writer, GIOMonitorCrashField_ReferencedObject, monitorContext->ZombieException.reason);
            }
            writer->endContainer(writer);
        }
    }
    writer->endContainer(writer);
}

/** Write basic report information.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param type The report type.
 *
 * @param reportID The report ID.
 */
static void writeReportInfo(const GIOMonitorCrashReportWriter* const writer,
                            const char* const key,
                            const char* const type,
                            const char* const reportID,
                            const char* const processName)
{
    writer->beginObject(writer, key);
    {
        writer->addStringElement(writer, GIOMonitorCrashField_Version, "x.x.x");
        writer->addStringElement(writer, GIOMonitorCrashField_ID, reportID);
        writer->addStringElement(writer, GIOMonitorCrashField_ProcessName, processName);
        writer->addIntegerElement(writer, GIOMonitorCrashField_Timestamp, time(NULL));
        writer->addStringElement(writer, GIOMonitorCrashField_Type, type);
    }
    writer->endContainer(writer);
}

static void writeRecrash(const GIOMonitorCrashReportWriter* const writer,
                         const char* const key,
                         const char* crashReportPath)
{
    writer->addJSONFileElement(writer, key, crashReportPath, true);
}


#pragma mark Setup

/** Prepare a report writer for use.
 *
 * @oaram writer The writer to prepare.
 *
 * @param context JSON writer contextual information.
 */
static void prepareReportWriter(GIOMonitorCrashReportWriter* const writer, GIOMonitorCrashJSONEncodeContext* const context)
{
    writer->addBooleanElement = addBooleanElement;
    writer->addFloatingPointElement = addFloatingPointElement;
    writer->addIntegerElement = addIntegerElement;
    writer->addUIntegerElement = addUIntegerElement;
    writer->addStringElement = addStringElement;
    writer->addTextFileElement = addTextFileElement;
    writer->addTextFileLinesElement = addTextLinesFromFile;
    writer->addJSONFileElement = addJSONElementFromFile;
    writer->addDataElement = addDataElement;
    writer->beginDataElement = beginDataElement;
    writer->appendDataElement = appendDataElement;
    writer->endDataElement = endDataElement;
    writer->addUUIDElement = addUUIDElement;
    writer->addJSONElement = addJSONElement;
    writer->beginObject = beginObject;
    writer->beginArray = beginArray;
    writer->endContainer = endContainer;
    writer->context = context;
}


// ============================================================================
#pragma mark - Main API -
// ============================================================================

void gioMonitorCrashReport_writeRecrashReport(const GIOMonitorCrash_MonitorContext* const monitorContext, const char* const path)
{
    char writeBuffer[1024];
    GIOMonitorCrashBufferedWriter bufferedWriter;
    static char tempPath[GIOMonitorCrashFU_MAX_PATH_LENGTH];
    strncpy(tempPath, path, sizeof(tempPath) - 10);
    strncpy(tempPath + strlen(tempPath) - 5, ".old", 5);
    GIOMonitorCrashLOG_INFO("Writing recrash report to %s", path);

    if(rename(path, tempPath) < 0)
    {
        GIOMonitorCrashLOG_ERROR("Could not rename %s to %s: %s", path, tempPath, strerror(errno));
    }
    if(!gioMonitorCrashFileUtils_openBufferedWriter(&bufferedWriter, path, writeBuffer, sizeof(writeBuffer)))
    {
        return;
    }

    gioMonitorCCD_freeze();

    GIOMonitorCrashJSONEncodeContext jsonContext;
    jsonContext.userData = &bufferedWriter;
    GIOMonitorCrashReportWriter concreteWriter;
    GIOMonitorCrashReportWriter* writer = &concreteWriter;
    prepareReportWriter(writer, &jsonContext);

    gioMonitorCrashJSON_beginEncode(getJsonContext(writer), true, addJSONData, &bufferedWriter);

    writer->beginObject(writer, GIOMonitorCrashField_Report);
    {
        writeRecrash(writer, GIOMonitorCrashField_RecrashReport, tempPath);
        gioMonitorCrashFileUtils_flushBufferedWriter(&bufferedWriter);
        if(remove(tempPath) < 0)
        {
            GIOMonitorCrashLOG_ERROR("Could not remove %s: %s", tempPath, strerror(errno));
        }
        writeReportInfo(writer,
                        GIOMonitorCrashField_Report,
                        GIOMonitorCrashReportType_Minimal,
                        monitorContext->eventID,
                        monitorContext->System.processName);
        gioMonitorCrashFileUtils_flushBufferedWriter(&bufferedWriter);

        writer->beginObject(writer, GIOMonitorCrashField_Crash);
        {
            writeError(writer, GIOMonitorCrashField_Error, monitorContext);
            gioMonitorCrashFileUtils_flushBufferedWriter(&bufferedWriter);
            int threadIndex = gioMonitorCrashMachineContext_indexOfThread(monitorContext->offendingMachineContext,
                                                 gioMonitorCrashMachineContext_getThreadFromContext(monitorContext->offendingMachineContext));
            writeThread(writer,
                        GIOMonitorCrashField_CrashedThread,
                        monitorContext,
                        monitorContext->offendingMachineContext,
                        threadIndex,
                        false);
            gioMonitorCrashFileUtils_flushBufferedWriter(&bufferedWriter);
        }
        writer->endContainer(writer);
    }
    writer->endContainer(writer);

    gioMonitorCrashJSON_endEncode(getJsonContext(writer));
    gioMonitorCrashFileUtils_closeBufferedWriter(&bufferedWriter);
    gioMonitorCCD_unfreeze();
}

static void writeSystemInfo(const GIOMonitorCrashReportWriter* const writer,
                            const char* const key,
                            const GIOMonitorCrash_MonitorContext* const monitorContext)
{
    writer->beginObject(writer, key);
    {
        writer->addStringElement(writer, GIOMonitorCrashField_SystemName, monitorContext->System.systemName);
        writer->addStringElement(writer, GIOMonitorCrashField_SystemVersion, monitorContext->System.systemVersion);
        writer->addStringElement(writer, GIOMonitorCrashField_Machine, monitorContext->System.machine);
        writer->addStringElement(writer, GIOMonitorCrashField_Model, monitorContext->System.model);
        writer->addStringElement(writer, GIOMonitorCrashField_KernelVersion, monitorContext->System.kernelVersion);
        writer->addStringElement(writer, GIOMonitorCrashField_OSVersion, monitorContext->System.osVersion);
        writer->addBooleanElement(writer, GIOMonitorCrashField_Jailbroken, monitorContext->System.isJailbroken);
        writer->addStringElement(writer, GIOMonitorCrashField_BootTime, monitorContext->System.bootTime);
        writer->addStringElement(writer, GIOMonitorCrashField_AppStartTime, monitorContext->System.appStartTime);
        writer->addStringElement(writer, GIOMonitorCrashField_ExecutablePath, monitorContext->System.executablePath);
        writer->addStringElement(writer, GIOMonitorCrashField_Executable, monitorContext->System.executableName);
        writer->addStringElement(writer, GIOMonitorCrashField_BundleID, monitorContext->System.bundleID);
        writer->addStringElement(writer, GIOMonitorCrashField_BundleName, monitorContext->System.bundleName);
        writer->addStringElement(writer, GIOMonitorCrashField_BundleVersion, monitorContext->System.bundleVersion);
        writer->addStringElement(writer, GIOMonitorCrashField_BundleShortVersion, monitorContext->System.bundleShortVersion);
        writer->addStringElement(writer, GIOMonitorCrashField_AppUUID, monitorContext->System.appID);
        writer->addStringElement(writer, GIOMonitorCrashField_CPUArch, monitorContext->System.cpuArchitecture);
        writer->addIntegerElement(writer, GIOMonitorCrashField_CPUType, monitorContext->System.cpuType);
        writer->addIntegerElement(writer, GIOMonitorCrashField_CPUSubType, monitorContext->System.cpuSubType);
        writer->addIntegerElement(writer, GIOMonitorCrashField_BinaryCPUType, monitorContext->System.binaryCPUType);
        writer->addIntegerElement(writer, GIOMonitorCrashField_BinaryCPUSubType, monitorContext->System.binaryCPUSubType);
        writer->addStringElement(writer, GIOMonitorCrashField_TimeZone, monitorContext->System.timezone);
        writer->addStringElement(writer, GIOMonitorCrashField_ProcessName, monitorContext->System.processName);
        writer->addIntegerElement(writer, GIOMonitorCrashField_ProcessID, monitorContext->System.processID);
        writer->addIntegerElement(writer, GIOMonitorCrashField_ParentProcessID, monitorContext->System.parentProcessID);
        writer->addStringElement(writer, GIOMonitorCrashField_DeviceAppHash, monitorContext->System.deviceAppHash);
        writer->addStringElement(writer, GIOMonitorCrashField_BuildType, monitorContext->System.buildType);
        writer->addIntegerElement(writer, GIOMonitorCrashField_Storage, (int64_t)monitorContext->System.storageSize);

        writeMemoryInfo(writer, GIOMonitorCrashField_Memory, monitorContext);
        writeAppStats(writer, GIOMonitorCrashField_AppStats, monitorContext);
    }
    writer->endContainer(writer);

}

static void writeDebugInfo(const GIOMonitorCrashReportWriter* const writer,
                            const char* const key,
                            const GIOMonitorCrash_MonitorContext* const monitorContext)
{
    writer->beginObject(writer, key);
    {
        if(monitorContext->consoleLogPath != NULL)
        {
            addTextLinesFromFile(writer, GIOMonitorCrashField_ConsoleLog, monitorContext->consoleLogPath);
        }
    }
    writer->endContainer(writer);

}

void gioMonitorCrashReport_writeStandardReport(const GIOMonitorCrash_MonitorContext* const monitorContext, const char* const path)
{
    GIOMonitorCrashLOG_INFO("Writing crash report to %s", path);
    char writeBuffer[1024];
    GIOMonitorCrashBufferedWriter bufferedWriter;

    if(!gioMonitorCrashFileUtils_openBufferedWriter(&bufferedWriter, path, writeBuffer, sizeof(writeBuffer)))
    {
        return;
    }

    gioMonitorCCD_freeze();

    GIOMonitorCrashJSONEncodeContext jsonContext;
    jsonContext.userData = &bufferedWriter;
    GIOMonitorCrashReportWriter concreteWriter;
    GIOMonitorCrashReportWriter* writer = &concreteWriter;
    prepareReportWriter(writer, &jsonContext);

    gioMonitorCrashJSON_beginEncode(getJsonContext(writer), true, addJSONData, &bufferedWriter);

    writer->beginObject(writer, GIOMonitorCrashField_Report);
    {
        writeReportInfo(writer,
                        GIOMonitorCrashField_Report,
                        GIOMonitorCrashReportType_Standard,
                        monitorContext->eventID,
                        monitorContext->System.processName);
        gioMonitorCrashFileUtils_flushBufferedWriter(&bufferedWriter);

        writeBinaryImages(writer, GIOMonitorCrashField_BinaryImages);
        gioMonitorCrashFileUtils_flushBufferedWriter(&bufferedWriter);

        writeProcessState(writer, GIOMonitorCrashField_ProcessState, monitorContext);
        gioMonitorCrashFileUtils_flushBufferedWriter(&bufferedWriter);

        writeSystemInfo(writer, GIOMonitorCrashField_System, monitorContext);
        gioMonitorCrashFileUtils_flushBufferedWriter(&bufferedWriter);

        writer->beginObject(writer, GIOMonitorCrashField_Crash);
        {
            writeError(writer, GIOMonitorCrashField_Error, monitorContext);
            gioMonitorCrashFileUtils_flushBufferedWriter(&bufferedWriter);
            writeAllThreads(writer,
                            GIOMonitorCrashField_Threads,
                            monitorContext,
                            g_introspectionRules.enabled);
            gioMonitorCrashFileUtils_flushBufferedWriter(&bufferedWriter);
        }
        writer->endContainer(writer);

        if(g_userInfoJSON != NULL)
        {
            addJSONElement(writer, GIOMonitorCrashField_User, g_userInfoJSON, false);
            gioMonitorCrashFileUtils_flushBufferedWriter(&bufferedWriter);
        }
        else
        {
            writer->beginObject(writer, GIOMonitorCrashField_User);
        }
        if(g_userSectionWriteCallback != NULL)
        {
            gioMonitorCrashFileUtils_flushBufferedWriter(&bufferedWriter);
            if (monitorContext->currentSnapshotUserReported == false) {
                g_userSectionWriteCallback(writer);
            }
        }
        writer->endContainer(writer);
        gioMonitorCrashFileUtils_flushBufferedWriter(&bufferedWriter);

        writeDebugInfo(writer, GIOMonitorCrashField_Debug, monitorContext);
    }
    writer->endContainer(writer);

    gioMonitorCrashJSON_endEncode(getJsonContext(writer));
    gioMonitorCrashFileUtils_closeBufferedWriter(&bufferedWriter);
    gioMonitorCCD_unfreeze();
}



void gioMonitorCrashReport_setUserInfoJSON(const char* const userInfoJSON)
{
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    GIOMonitorCrashLOG_TRACE("set userInfoJSON to %p", userInfoJSON);

    pthread_mutex_lock(&mutex);
    if(g_userInfoJSON != NULL)
    {
        free((void*)g_userInfoJSON);
    }
    if(userInfoJSON == NULL)
    {
        g_userInfoJSON = NULL;
    }
    else
    {
        g_userInfoJSON = strdup(userInfoJSON);
    }
    pthread_mutex_unlock(&mutex);
}

void gioMonitorCrashReport_setIntrospectMemory(bool shouldIntrospectMemory)
{
    g_introspectionRules.enabled = shouldIntrospectMemory;
}

void gioMonitorCrashReport_setDoNotIntrospectClasses(const char** doNotIntrospectClasses, int length)
{
    const char** oldClasses = g_introspectionRules.restrictedClasses;
    int oldClassesLength = g_introspectionRules.restrictedClassesCount;
    const char** newClasses = NULL;
    int newClassesLength = 0;

    if(doNotIntrospectClasses != NULL && length > 0)
    {
        newClassesLength = length;
        newClasses = malloc(sizeof(*newClasses) * (unsigned)newClassesLength);
        if(newClasses == NULL)
        {
            GIOMonitorCrashLOG_ERROR("Could not allocate memory");
            return;
        }

        for(int i = 0; i < newClassesLength; i++)
        {
            newClasses[i] = strdup(doNotIntrospectClasses[i]);
        }
    }

    g_introspectionRules.restrictedClasses = newClasses;
    g_introspectionRules.restrictedClassesCount = newClassesLength;

    if(oldClasses != NULL)
    {
        for(int i = 0; i < oldClassesLength; i++)
        {
            free((void*)oldClasses[i]);
        }
        free(oldClasses);
    }
}

void gioMonitorCrashReport_setUserSectionWriteCallback(const GIOMonitorCrashReportWriteCallback userSectionWriteCallback)
{
    GIOMonitorCrashLOG_TRACE("Set userSectionWriteCallback to %p", userSectionWriteCallback);
    g_userSectionWriteCallback = userSectionWriteCallback;
}

