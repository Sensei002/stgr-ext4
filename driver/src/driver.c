/*++
Module Name:
    driver.c

Abstract:
    DriverEntry, file system registration, and unload for STGR EXT4.

    Milestone 1 invariants:
      - The driver creates exactly one file system device object (FSDO).
      - No volume device object exists yet, so no volume can be mounted.
      - Every volume probe is declined with STATUS_UNRECOGNIZED_VOLUME.
      - Unload unregisters the FSDO before deleting it.

Environment:
    Kernel mode, PASSIVE_LEVEL for entry and unload.

License:
    SPDX-License-Identifier: MIT
--*/

#include "STGRExt4.h"

STGR_DRIVER_GLOBALS StgrGlobals;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, StgrDriverUnload)
#endif

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
/*++

Routine Description:

    Creates and registers the file system device object.

    This routine intentionally does not read configuration, parse EXT4, or
    create volume device objects. Those arrive in later milestones.

Arguments:

    DriverObject - Driver object created by the I/O manager.
    RegistryPath - Service registry path. Unused in Milestone 1.

Return Value:

    STATUS_SUCCESS on success, otherwise the failing NTSTATUS. On failure the
    driver object is left with no device objects and no dispatch routines that
    can be invoked.

--*/
{
    NTSTATUS status;
    PDEVICE_OBJECT fsdo = NULL;
    PSTGR_DEVICE_HEADER header;
    UNICODE_STRING deviceName;
    ULONG index;

    UNREFERENCED_PARAMETER(RegistryPath);

    RtlZeroMemory(&StgrGlobals, sizeof(StgrGlobals));
    StgrGlobals.DriverObject = DriverObject;

    STGR_INFO("STGR EXT4 %u.%u.%u initializing (filesystem mount not implemented)\n",
              (ULONG)STGR_DRIVER_VERSION_MAJOR,
              (ULONG)STGR_DRIVER_VERSION_MINOR,
              (ULONG)STGR_DRIVER_VERSION_PATCH);

    //
    // Initialize the resource before any dispatch routine can be reached.
    // ExInitializeResourceLite can fail only on allocation failure.
    //
    status = ExInitializeResourceLite(&StgrGlobals.VolumeLock);
    if (!NT_SUCCESS(status))
    {
        STGR_ERROR("ExInitializeResourceLite failed: 0x%08X\n", (ULONG)status);
        return status;
    }
    StgrGlobals.VolumeLockInitialized = TRUE;
    InitializeListHead(&StgrGlobals.VolumeList);

    //
    // A file system device object is a disk file system, not a PnP device.
    // Do not set DO_BUFFERED_IO or DO_DIRECT_IO: a file system performs its
    // own buffering and caching decisions, as FastFAT does.
    //
    RtlInitUnicodeString(&deviceName, STGR_FS_DEVICE_NAME);
    status = IoCreateDevice(DriverObject,
                            sizeof(STGR_DEVICE_HEADER),
                            &deviceName,
                            FILE_DEVICE_DISK_FILE_SYSTEM,
                            0,
                            FALSE,
                            &fsdo);
    if (!NT_SUCCESS(status))
    {
        STGR_ERROR("IoCreateDevice failed: 0x%08X\n", (ULONG)status);
        goto Exit;
    }

    header = (PSTGR_DEVICE_HEADER)fsdo->DeviceExtension;
    RtlZeroMemory(header, sizeof(*header));
    header->Magic = STGR_DEVICE_MAGIC;
    header->Kind = StgrDeviceFileSystem;
    header->Self = fsdo;
    header->DriverObject = DriverObject;

    StgrGlobals.FileSystemDeviceObject = fsdo;

    //
    // Install the default dispatcher first, then override the one IRP that
    // must be handled by every registered file system driver.
    //
    for (index = 0; index <= IRP_MJ_MAXIMUM_FUNCTION; index++)
    {
        DriverObject->MajorFunction[index] = StgrFsdDispatch;
    }
    DriverObject->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL] = StgrFsdFileSystemControl;
    DriverObject->DriverUnload = StgrDriverUnload;

    //
    // Registration makes the driver eligible for IRP_MN_MOUNT_VOLUME probes.
    // The extra object reference keeps the FSDO alive while it is registered,
    // regardless of what the I/O manager does with the registration list.
    //
    IoRegisterFileSystem(fsdo);
    if (!ObReferenceObjectSafe(fsdo))
    {
        IoUnregisterFileSystem(fsdo);
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    STGR_INFO("registered file system device %wZ; no volumes will be mounted\n",
              &deviceName);

    return STATUS_SUCCESS;

Exit:
    if (fsdo != NULL)
    {
        StgrGlobals.FileSystemDeviceObject = NULL;
        IoDeleteDevice(fsdo);
    }

    if (StgrGlobals.VolumeLockInitialized)
    {
        ExDeleteResourceLite(&StgrGlobals.VolumeLock);
        StgrGlobals.VolumeLockInitialized = FALSE;
    }

    return status;
}

VOID
StgrDriverUnload(
    _In_ PDRIVER_OBJECT DriverObject
    )
/*++

Routine Description:

    Unregisters and tears down the file system device object.

    The I/O manager only calls this routine when the driver object has no
    outstanding open handles. The volume resource serialises unload against
    in-flight mount probes. A future volume teardown loop belongs here, before
    the file system is unregistered.

Arguments:

    DriverObject - Driver object being unloaded.

Return Value:

    None.

--*/
{
    PDEVICE_OBJECT fsdo;

    UNREFERENCED_PARAMETER(DriverObject);

    STGR_INFO("unloading\n");

    if (StgrGlobals.VolumeLockInitialized)
    {
        ExAcquireResourceExclusiveLite(&StgrGlobals.VolumeLock, TRUE);
        StgrGlobals.Unloading = TRUE;
        ExReleaseResourceLite(&StgrGlobals.VolumeLock);
    }
    else
    {
        StgrGlobals.Unloading = TRUE;
    }

    fsdo = StgrGlobals.FileSystemDeviceObject;
    if (fsdo != NULL)
    {
        //
        // Stop new mount probes, drop the registration reference, then drop
        // the creation reference. IoDeleteDevice must be the last owner.
        //
        IoUnregisterFileSystem(fsdo);
        ObDereferenceObject(fsdo);
        IoDeleteDevice(fsdo);
        StgrGlobals.FileSystemDeviceObject = NULL;
    }

    if (StgrGlobals.VolumeLockInitialized)
    {
        ExDeleteResourceLite(&StgrGlobals.VolumeLock);
        StgrGlobals.VolumeLockInitialized = FALSE;
    }
}
