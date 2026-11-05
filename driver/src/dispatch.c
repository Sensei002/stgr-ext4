/*++
Module Name:
    dispatch.c

Abstract:
    IRP dispatch for the Milestone 1 file system skeleton.

    There are no volume device objects yet. Consequently:
      - IRP_MJ_FILE_SYSTEM_CONTROL / IRP_MN_MOUNT_VOLUME is declined with
        STATUS_UNRECOGNIZED_VOLUME and Information = 0.
      - IRP_MN_VERIFY_VOLUME cannot legitimately arrive at the FSDO; it is
        declined the same way.
      - Every other IRP is completed with STATUS_INVALID_DEVICE_REQUEST.

    Declining a volume is not a stub that returns success. It is the correct
    answer for a driver that has no EXT4 parser and no volume to expose.

Environment:
    Kernel mode, PASSIVE_LEVEL for the IRPs handled here.

License:
    SPDX-License-Identifier: MIT
--*/

#include "STGRExt4.h"

static NTSTATUS
StgrProbeMountVolume(
    _In_ PIO_STACK_LOCATION Stack
    )
{
    PDEVICE_OBJECT target;

    NT_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    target = Stack->Parameters.MountVolume.DeviceObject;

    //
    // The I/O manager always supplies both fields. If they are missing, the
    // request is malformed; still decline it rather than dereferencing NULL
    // or aborting the mount chain with an unrelated status.
    //
    if (target == NULL || Stack->Parameters.MountVolume.Vpb == NULL)
    {
        STGR_ERROR("mount probe with null target device or VPB\n");
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    ExAcquireResourceSharedLite(&StgrGlobals.VolumeLock, TRUE);

    if (StgrGlobals.Unloading)
    {
        ExReleaseResourceLite(&StgrGlobals.VolumeLock);
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    //
    // Milestone 2 replaces this with a bounded, validated read of the EXT4
    // superblock at byte offset 1024 and a check against the approved-volume
    // allowlist. Until then, do not touch the volume at all.
    //
    STGR_TRACE("declining volume %p (stack size %u): EXT4 recognition is not implemented\n",
               target,
               (ULONG)target->StackSize);

    ExReleaseResourceLite(&StgrGlobals.VolumeLock);

    return STATUS_UNRECOGNIZED_VOLUME;
}

NTSTATUS
StgrFsdDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )
/*++

Routine Description:

    Completes an IRP that has no implementation at the file system device
    object. Volume-level IRPs are handled by the volume dispatcher in a later
    milestone.

Arguments:

    DeviceObject - Target device object.
    Irp - Request being dispatched.

Return Value:

    STATUS_INVALID_DEVICE_REQUEST.

--*/
{
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS
StgrFsdFileSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )
/*++

Routine Description:

    Handles file system control IRPs at the FSDO.

    A successful mount would have to create a VDO and a VPB and complete the
    IRP with Information = 1. This routine never does that in Milestone 1.

Arguments:

    DeviceObject - File system device object.
    Irp - IRP_MJ_FILE_SYSTEM_CONTROL request.

Return Value:

    The status written to the IRP.

--*/
{
    PIO_STACK_LOCATION stack;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(DeviceObject);

    stack = IoGetCurrentIrpStackLocation(Irp);

    switch (stack->MinorFunction)
    {
    case IRP_MN_MOUNT_VOLUME:
        status = StgrProbeMountVolume(stack);
        break;

    case IRP_MN_VERIFY_VOLUME:
        //
        // No VDO exists, so a verify request is never valid. Do not return
        // STATUS_WRONG_VOLUME: that would ask the I/O manager to remount a
        // volume this driver never mounted.
        //
        status = STATUS_UNRECOGNIZED_VOLUME;
        break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return status;
}
