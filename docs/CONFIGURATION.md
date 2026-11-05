# Configuration and volume approval

Status: **design target. Not implemented.** The Milestone 1 driver reads no
configuration and mounts nothing.

The central safety rule of this project is that no EXT4 volume is mounted unless
it has been explicitly approved. This document defines how approval is intended
to work. The schema may change before implementation, but the security
properties must not.

---

## 1. Principles

1. **Deny by default.** An empty or missing allowlist approves nothing.
2. **Approve by UUID, not by path or disk number.** Drive letters and disk
   numbers change. The EXT4 UUID is stable and travels with the file system.
3. **No wildcard and no "mount all".** There is no debug switch that reaches a
   release build.
4. **The kernel validates; the service stores.** The service writes the
   allowlist, but the driver re-validates every entry at mount time.
5. **Fail closed.** A malformed entry, a missing key, or an unreadable
   configuration means "not approved", not "allow".
6. **Read-only by default.** Write permission is a separate, explicit grant.
7. **No silent re-approval.** Replacing a disk with a cloned UUID does not
   automatically become trusted; the user approves it.
8. **No secrets in the configuration.** The file contains volume identifiers
   and policy, not keys or passwords.

---

## 2. Where configuration lives

### 2.1 Driver parameters (kernel-readable)

```text
HKLM\SYSTEM\CurrentControlSet\Services\STGRExt4\Parameters
```

| Value | Type | Meaning |
| --- | --- | --- |
| `ApprovedVolumes` | `REG_MULTI_SZ` | Canonical UUID strings, one per volume |
| `AllowWrite` | `REG_DWORD` | 0 = read-only (default), 1 = write allowed if all other gates pass |
| `RequireCleanJournal` | `REG_DWORD` | 1 = refuse dirty volumes (default) |
| `RequireSupportedFeatures` | `REG_DWORD` | 1 = refuse unknown features (default) |
| `DiagnosticsLevel` | `REG_DWORD` | 0 = errors, 1 = info, 2 = verbose |
| `ConfigVersion` | `REG_DWORD` | Schema version for the driver view |

The driver reads these values during mount. It does not watch the key for live
changes in the first implementation; a change takes effect on the next mount or
after an explicit reload request through the control interface.

### 2.2 Management configuration (user-mode)

```text
%ProgramData%\STGRExt4\config.json
```

The service owns this file. It is the human-editable, auditable source of
truth. The service validates it, then writes the kernel-readable view above.
The driver never parses JSON.

Default ACL: Administrators and SYSTEM full control; Users read-only. A
standard user cannot approve a volume.

---

## 3. JSON schema (design target)

```json
{
  "schema": 1,
  "policy": {
    "read_only_default": true,
    "allow_write": false,
    "require_clean_journal": true,
    "require_supported_features": true,
    "auto_mount_approved": false,
    "diagnostics_level": 1
  },
  "volumes": [
    {
      "uuid": "6f1c2a44-9b3e-4f7a-8c21-0d5e6f708192",
      "label": "Game Library",
      "approved": true,
      "read_only": true,
      "auto_mount": false,
      "preferred_drive_letter": "G",
      "comment": "External SSD, ext4, approved 2026-11-05",
      "approved_at": "2026-11-05T12:00:00Z"
    }
  ]
}
```

### Field rules

| Field | Rule |
| --- | --- |
| `schema` | Must be `1`. Unknown versions are refused. |
| `uuid` | Canonical lowercase `8-4-4-4-12` hex. Compare case-insensitively after normalisation. |
| `label` | Display only. Never used for matching. |
| `approved` | Must be `true` for the volume to be considered. |
| `read_only` | Per-volume override. A global `allow_write: false` always wins. |
| `auto_mount` | If false, the user must mount explicitly. Default false. |
| `preferred_drive_letter` | Optional hint; the Mount Manager decides the final letter. |
| `comment` | Free text for the administrator. |
| `approved_at` | Audit timestamp. |

Duplicate UUIDs are a configuration error. A volume entry that fails validation
is ignored and logged; the rest of the file does not cause an implicit allow.

---

## 4. Approval workflow (planned)

1. The user runs `stgr-ext4 scan` (future). The CLI enumerates volumes and
   reads only the superblock: magic, UUID, label, feature summary, state.
   Scanning never mounts and never writes.
2. The CLI prints a table. It does **not** approve anything automatically.
3. The user runs `stgr-ext4 approve <uuid>` (future). The service verifies the
   UUID exists, asks for confirmation, and writes the entry with
   `read_only: true`.
4. Write permission requires a second, explicit command and a warning that the
   volume must not be mounted by Linux at the same time.
5. The driver re-reads the allowlist at mount. If the UUID is absent, the mount
   is refused with `STATUS_UNRECOGNIZED_VOLUME`.
6. Revocation removes the entry and dismounts the volume if it is mounted.

There is no path by which merely plugging in a disk mounts it.

---

## 5. Mount-time checks

The driver performs all of these before creating a VDO:

1. The target is a volume device, not a raw disk with an unparsed partition
   table.
2. The superblock magic and geometry are valid.
3. The feature policy returns `READ_OK` or `READ_WRITE_OK`.
4. The UUID is in `ApprovedVolumes`.
5. If write is requested: global `AllowWrite = 1`, per-volume read-only is
   false, the journal is clean or replayed, no error state, no orphan work, no
   quota/unsupported feature, and the volume is not marked read-only on disk.
6. The requested access mode does not exceed the granted mode. A read-only
   approval can never be upgraded in memory.
7. The volume is not already mounted by this driver.

Any failure completes the mount IRP with an error and `Information = 0`.

---

## 6. Security requirements

- The configuration file and registry key are writable only by Administrators
  and SYSTEM.
- The service validates UUID syntax and rejects path traversal, UNC paths, and
  device paths in fields that are not paths.
- The driver does not trust the service's JSON; it trusts only the registry
  values it can read and validate.
- The driver does not accept an approval request from an arbitrary user-mode
  process. A future control interface must use a device object with an ACL that
  grants access only to the service identity.
- Logging never prints a full volume path or user data. The UUID and label are
  acceptable.
- A cloned UUID is not a security boundary. Approval is an administrative
  decision, not an authentication mechanism.

---

## 7. CLI surface (planned)

| Command | Behaviour when implemented |
| --- | --- |
| `scan` | List candidate volumes and their EXT4 metadata; never mount |
| `list` | Show approved volumes and policy |
| `approve <uuid>` | Add a read-only approval after confirmation |
| `approve --write <uuid>` | Add a write approval after an explicit warning |
| `revoke <uuid>` | Remove approval; dismount if mounted |
| `mount <uuid>` | Mount an approved volume |
| `unmount <uuid>` | Flush and dismount |
| `configure` | Edit policy through validated prompts |
| `diagnostics` | Collect logs and state; no telemetry |

Current implementation: only `version`, `help`, and `status` work. `scan`,
`list`, `mount`, `unmount`, `configure`, and `diagnostics` exit with code 70 and
print that they are not implemented.

---

## 8. Example: read-only approval

```text
> stgr-ext4 scan
UUID                                  LABEL          SIZE    STATE   FEATURES
6f1c2a44-9b3e-4f7a-8c21-0d5e6f708192  Game Library   1.8 TB  clean   extents,64bit,metadata_csum,...
(scan is not implemented in 0.1.0)

> stgr-ext4 approve 6f1c2a44-9b3e-4f7a-8c21-0d5e6f708192
This will allow STGR EXT4 to mount the volume read-only.
The volume must not be mounted by Linux at the same time.
Type the UUID to confirm: ...
(approve is not implemented in 0.1.0)
```

---

## 9. Failure messages

| Condition | User-visible message | Driver status |
| --- | --- | --- |
| UUID not approved | "Volume is not approved." | `STATUS_UNRECOGNIZED_VOLUME` |
| Unsupported feature | "Volume uses an unsupported EXT4 feature: <name>." | `STATUS_UNRECOGNIZED_VOLUME` |
| Dirty journal | "Volume needs journal replay; mount refused." | `STATUS_UNRECOGNIZED_VOLUME` or `STATUS_FILE_CORRUPT_ERROR` |
| Checksum failure | "Volume metadata checksum failed; refusing to mount." | `STATUS_FILE_CORRUPT_ERROR` |
| Write not granted | "Volume is approved read-only." | `STATUS_MEDIA_WRITE_PROTECTED` |
| Already mounted | "Volume is already mounted." | `STATUS_DEVICE_BUSY` |
| Config invalid | "Configuration is invalid; no volume is approved." | Refusal |

Messages must not suggest that the user can bypass the check by editing a
different field.

---

## 10. Current status

| Item | Status |
| --- | --- |
| Registry parameters | Not implemented |
| JSON configuration | Not implemented |
| Service | Not implemented |
| Approval CLI | Not implemented |
| Driver allowlist check | Not implemented |
| Control interface | Not implemented |

Until these exist, the only honest statement is: **the driver mounts nothing,
and no volume is approved.**
