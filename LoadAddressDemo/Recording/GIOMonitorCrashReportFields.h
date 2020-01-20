//
//  GIOMonitorCrashReportFields.h
//
//  Created by Karl Stenerud on 2012-10-07.
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


#ifndef HDR_GIOMonitorCrashReportFields_h
#define HDR_GIOMonitorCrashReportFields_h


#pragma mark - Report Types -

#define GIOMonitorCrashReportType_Minimal          "minimal"
#define GIOMonitorCrashReportType_Standard         "standard"
#define GIOMonitorCrashReportType_Custom           "custom"


#pragma mark - Memory Types -

#define GIOMonitorCrashMemType_Block               "objc_block"
#define GIOMonitorCrashMemType_Class               "objc_class"
#define GIOMonitorCrashMemType_NullPointer         "null_pointer"
#define GIOMonitorCrashMemType_Object              "objc_object"
#define GIOMonitorCrashMemType_String              "string"
#define GIOMonitorCrashMemType_Unknown             "unknown"


#pragma mark - Exception Types -

#define GIOMonitorCrashExcType_CPPException        "cpp_exception"
#define GIOMonitorCrashExcType_Deadlock            "deadlock"
#define GIOMonitorCrashExcType_Mach                "mach"
#define GIOMonitorCrashExcType_NSException         "nsexception"
#define GIOMonitorCrashExcType_Signal              "signal"
#define GIOMonitorCrashExcType_User                "user"


#pragma mark - Common -

#define GIOMonitorCrashField_Address               "address"
#define GIOMonitorCrashField_Contents              "contents"
#define GIOMonitorCrashField_Exception             "exception"
#define GIOMonitorCrashField_FirstObject           "first_object"
#define GIOMonitorCrashField_Index                 "index"
#define GIOMonitorCrashField_Ivars                 "ivars"
#define GIOMonitorCrashField_Language              "language"
#define GIOMonitorCrashField_Name                  "name"
#define GIOMonitorCrashField_UserInfo              "userInfo"
#define GIOMonitorCrashField_ReferencedObject      "referenced_object"
#define GIOMonitorCrashField_Type                  "type"
#define GIOMonitorCrashField_UUID                  "uuid"
#define GIOMonitorCrashField_Value                 "value"

#define GIOMonitorCrashField_Error                 "error"
#define GIOMonitorCrashField_JSONData              "json_data"


#pragma mark - Notable Address -

#define GIOMonitorCrashField_Class                 "class"
#define GIOMonitorCrashField_LastDeallocObject     "last_deallocated_obj"


#pragma mark - Backtrace -

#define GIOMonitorCrashField_InstructionAddr       "instruction_addr"
#define GIOMonitorCrashField_LineOfCode            "line_of_code"
#define GIOMonitorCrashField_ObjectAddr            "object_addr"
#define GIOMonitorCrashField_ObjectName            "object_name"
#define GIOMonitorCrashField_SymbolAddr            "symbol_addr"
#define GIOMonitorCrashField_SymbolName            "symbol_name"


#pragma mark - Stack Dump -

#define GIOMonitorCrashField_DumpEnd               "dump_end"
#define GIOMonitorCrashField_DumpStart             "dump_start"
#define GIOMonitorCrashField_GrowDirection         "grow_direction"
#define GIOMonitorCrashField_Overflow              "overflow"
#define GIOMonitorCrashField_StackPtr              "stack_pointer"


#pragma mark - Thread Dump -

#define GIOMonitorCrashField_Backtrace             "backtrace"
#define GIOMonitorCrashField_Basic                 "basic"
#define GIOMonitorCrashField_Crashed               "crashed"
#define GIOMonitorCrashField_CurrentThread         "current_thread"
#define GIOMonitorCrashField_DispatchQueue         "dispatch_queue"
#define GIOMonitorCrashField_NotableAddresses      "notable_addresses"
#define GIOMonitorCrashField_Registers             "registers"
#define GIOMonitorCrashField_Skipped               "skipped"
#define GIOMonitorCrashField_Stack                 "stack"


#pragma mark - Binary Image -

#define GIOMonitorCrashField_CPUSubType            "cpu_subtype"
#define GIOMonitorCrashField_CPUType               "cpu_type"
#define GIOMonitorCrashField_ImageAddress          "image_addr"
#define GIOMonitorCrashField_ImageVmAddress        "image_vmaddr"
#define GIOMonitorCrashField_ImageSize             "image_size"
#define GIOMonitorCrashField_ImageMajorVersion     "major_version"
#define GIOMonitorCrashField_ImageMinorVersion     "minor_version"
#define GIOMonitorCrashField_ImageRevisionVersion  "revision_version"


#pragma mark - Memory -

#define GIOMonitorCrashField_Free                  "free"
#define GIOMonitorCrashField_Usable                "usable"


#pragma mark - Error -

#define GIOMonitorCrashField_Backtrace             "backtrace"
#define GIOMonitorCrashField_Code                  "code"
#define GIOMonitorCrashField_CodeName              "code_name"
#define GIOMonitorCrashField_CPPException          "cpp_exception"
#define GIOMonitorCrashField_ExceptionName         "exception_name"
#define GIOMonitorCrashField_Mach                  "mach"
#define GIOMonitorCrashField_NSException           "nsexception"
#define GIOMonitorCrashField_Reason                "reason"
#define GIOMonitorCrashField_Signal                "signal"
#define GIOMonitorCrashField_Subcode               "subcode"
#define GIOMonitorCrashField_UserReported          "user_reported"


#pragma mark - Process State -

#define GIOMonitorCrashField_LastDeallocedNSException "last_dealloced_nsexception"
#define GIOMonitorCrashField_ProcessState             "process"


#pragma mark - App Stats -

#define GIOMonitorCrashField_ActiveTimeSinceCrash  "active_time_since_last_crash"
#define GIOMonitorCrashField_ActiveTimeSinceLaunch "active_time_since_launch"
#define GIOMonitorCrashField_AppActive             "application_active"
#define GIOMonitorCrashField_AppInFG               "application_in_foreground"
#define GIOMonitorCrashField_BGTimeSinceCrash      "background_time_since_last_crash"
#define GIOMonitorCrashField_BGTimeSinceLaunch     "background_time_since_launch"
#define GIOMonitorCrashField_LaunchesSinceCrash    "launches_since_last_crash"
#define GIOMonitorCrashField_SessionsSinceCrash    "sessions_since_last_crash"
#define GIOMonitorCrashField_SessionsSinceLaunch   "sessions_since_launch"


#pragma mark - Report -

#define GIOMonitorCrashField_Crash                 "crash"
#define GIOMonitorCrashField_Debug                 "debug"
#define GIOMonitorCrashField_Diagnosis             "diagnosis"
#define GIOMonitorCrashField_ID                    "id"
#define GIOMonitorCrashField_ProcessName           "process_name"
#define GIOMonitorCrashField_Report                "report"
#define GIOMonitorCrashField_Timestamp             "timestamp"
#define GIOMonitorCrashField_Version               "version"

#pragma mark Minimal
#define GIOMonitorCrashField_CrashedThread         "crashed_thread"

#pragma mark Standard
#define GIOMonitorCrashField_AppStats              "application_stats"
#define GIOMonitorCrashField_BinaryImages          "binary_images"
#define GIOMonitorCrashField_System                "system"
#define GIOMonitorCrashField_Memory                "memory"
#define GIOMonitorCrashField_Threads               "threads"
#define GIOMonitorCrashField_User                  "user"
#define GIOMonitorCrashField_ConsoleLog            "console_log"

#pragma mark Incomplete
#define GIOMonitorCrashField_Incomplete            "incomplete"
#define GIOMonitorCrashField_RecrashReport         "recrash_report"

#pragma mark System
#define GIOMonitorCrashField_AppStartTime          "app_start_time"
#define GIOMonitorCrashField_AppUUID               "app_uuid"
#define GIOMonitorCrashField_BootTime              "boot_time"
#define GIOMonitorCrashField_BundleID              "CFBundleIdentifier"
#define GIOMonitorCrashField_BundleName            "CFBundleName"
#define GIOMonitorCrashField_BundleShortVersion    "CFBundleShortVersionString"
#define GIOMonitorCrashField_BundleVersion         "CFBundleVersion"
#define GIOMonitorCrashField_CPUArch               "cpu_arch"
#define GIOMonitorCrashField_CPUType               "cpu_type"
#define GIOMonitorCrashField_CPUSubType            "cpu_subtype"
#define GIOMonitorCrashField_BinaryCPUType         "binary_cpu_type"
#define GIOMonitorCrashField_BinaryCPUSubType      "binary_cpu_subtype"
#define GIOMonitorCrashField_DeviceAppHash         "device_app_hash"
#define GIOMonitorCrashField_Executable            "CFBundleExecutable"
#define GIOMonitorCrashField_ExecutablePath        "CFBundleExecutablePath"
#define GIOMonitorCrashField_Jailbroken            "jailbroken"
#define GIOMonitorCrashField_KernelVersion         "kernel_version"
#define GIOMonitorCrashField_Machine               "machine"
#define GIOMonitorCrashField_Model                 "model"
#define GIOMonitorCrashField_OSVersion             "os_version"
#define GIOMonitorCrashField_ParentProcessID       "parent_process_id"
#define GIOMonitorCrashField_ProcessID             "process_id"
#define GIOMonitorCrashField_ProcessName           "process_name"
#define GIOMonitorCrashField_Size                  "size"
#define GIOMonitorCrashField_Storage               "storage"
#define GIOMonitorCrashField_SystemName            "system_name"
#define GIOMonitorCrashField_SystemVersion         "system_version"
#define GIOMonitorCrashField_TimeZone              "time_zone"
#define GIOMonitorCrashField_BuildType             "build_type"

#endif
