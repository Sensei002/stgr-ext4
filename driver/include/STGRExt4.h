/*++
Module Name:
    STGRExt4.h

Abstract:
    Shared definitions for the STGR EXT4 native Windows file system driver.

    This is the Milestone 1 skeleton. The driver registers itself with the
    I/O manager as a disk file system and safely declines every volume. It
    does not parse EXT4 and it does not mount anything yet.

Environment:
    Kernel mode.

License:
    SPDX-License-Identifier: MIT
--*/

#pragma once

//
// ntifs.h is the file system driver header. It is required for
// IoRegisterFileSystem/IoUnregisterFileSystem, which ntddk.h does not declare,
// and it includes the rest of the kernel interfaces used here.
//
#include <ntifs.h>

//
// The device name is placed in the \Device namespace by IoCreateDevice and
// the file system is exposed in the \FileSystem namespace by
// IoRegisterFileSystem. Uniqueness matters: filters and diagnostics may
// identify a file system by this name.
//
#define STGR_FS_DEVICE_NAME L"\\STGRExt4"

#define STGR_DRIVER_VERSION_MAJOR 0
#define STGR_DRIVER_VERSION_MINOR 1
#define STGR_DRIVER_VERSION_PATCH 0
#define STGR_DRIVER_VERSION_BUILD 0

//
// Identifies our device extension. A future volume device object (VDO) will
// use the same header with Kind = StgrDeviceVolume and additional fields.
//
#define STGR_DEVICE_MAGIC 0x53475444 /* 'SGTD' */

typedef enum _STGR_DEVICE_KIND
{
    StgrDeviceUnknown = 0,
    StgrDeviceFileSystem = 1,
    StgrDeviceVolume = 2
} STGR_DEVICE_KIND;

typedef struct _STGR_DEVICE_HEADER
{
    ULONG Magic;
    STGR_DEVICE_KIND Kind;
    PDEVICE_OBJECT Self;
    PDRIVER_OBJECT DriverObject;
} STGR_DEVICE_HEADER, *PSTGR_DEVICE_HEADER;

typedef struct _STGR_DRIVER_GLOBALS
{
    PDRIVER_OBJECT DriverObject;
    PDEVICE_OBJECT FileSystemDeviceObject;

    //
    // Protects the mounted-volume list (empty until Milestone 3) and the
    // Unloading flag. Mount probes take it shared; unload takes it exclusive
    // and waits for in-flight probes before unregistering the file system.
    //
    ERESOURCE VolumeLock;
    LIST_ENTRY VolumeList;
    BOOLEAN VolumeLockInitialized;
    BOOLEAN Unloading;
} STGR_DRIVER_GLOBALS, *PSTGR_DRIVER_GLOBALS;

extern STGR_DRIVER_GLOBALS StgrGlobals;

//
// Logging is compiled out of free builds. Checked builds use DbgPrint so
// that no debug-filter component ID has to be invented.
//
#define STGR_LOG_PREFIX "STGREXT4: "

#if DBG
#define STGR_TRACE(...) DbgPrint(STGR_LOG_PREFIX __VA_ARGS__)
#define STGR_INFO(...) DbgPrint(STGR_LOG_PREFIX __VA_ARGS__)
#define STGR_ERROR(...) DbgPrint(STGR_LOG_PREFIX __VA_ARGS__)
#else
#define STGR_TRACE(...) ((VOID)0)
#define STGR_INFO(...) ((VOID)0)
#define STGR_ERROR(...) ((VOID)0)
#endif

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD StgrDriverUnload;

//
// Generic dispatcher. Every IRP that has no specific handler reaches this
// routine and is completed with STATUS_INVALID_DEVICE_REQUEST. This is a
// deliberate, truthful answer: Milestone 1 has no volume objects.
//
NTSTATUS
StgrFsdDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    );

//
// IRP_MJ_FILE_SYSTEM_CONTROL. Handles mount/verify probes and declines them
// safely until the EXT4 engine exists.
//
NTSTATUS
StgrFsdFileSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    );
