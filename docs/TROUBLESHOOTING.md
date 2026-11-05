# Troubleshooting

Status: the driver has not been built or loaded, so no real failure has been
diagnosed with these steps. Treat this as a runbook, not a record.

> **Stop immediately if a volume contains data you care about.**
> Do not run a repair tool, do not format, do not accept a Windows "you need to
> format this disk" prompt, and do not run `chkdsk` on an EXT4 volume. Make an
> image of the device before experimenting.

---

## 1. First questions

1. Which build is this? Record the commit, version, and SHA-256.
2. Is it a test VM or a real machine?
3. Is the driver unsigned, test-signed, or production-signed?
4. Is Secure Boot on? Is test signing on?
5. Is the volume approved by UUID?
6. Is the volume clean, and does it need journal replay?
7. What exactly did you run, and what was the exact error?

If the answer to question 2 is "real machine", stop and move to a VM.

---

## 2. Build failures

### `The pinned WDK NuGet package is missing`

`Build-Driver.ps1` cannot find the WDK content.

```powershell
nuget restore .\packages.config -PackagesDirectory .\packages\
```

Confirm the directory exists:

```text
packages\Microsoft.Windows.WDK.x64.10.0.26100.6584\build\native\
```

If restore fails, check network access, the NuGet source, and the exact versions
in `packages.config`. Do not edit `Directory.Build.props` to point at a
different version without updating `packages.config` and the docs.

### `msbuild.exe is not on PATH`

- In CI, `microsoft/setup-msbuild` must run before the build step.
- Locally, use a Developer Command Prompt for VS 2022, or add MSBuild to
  `PATH`.

### `Inf2Cat.exe was not found under packages\`

- The WDK package was not restored.
- The package layout changed. Inspect
  `packages\Microsoft.Windows.WDK.x64.*\c\bin\`.
- The script expects `*\x86\Inf2Cat.exe`.

### `The image is not an x64 PE`

`dumpbin` did not report `machine (x64)`. Check:

- `Platform` is `x64`, not `Win32`;
- the linker is not producing a user-mode DLL;
- the project was not built with a stale intermediate directory.

Do not weaken `Test-DriverImage.ps1`. The check exists because a wrong image can
look like a successful build.

### `The image is not a native kernel image`

`dumpbin` did not report `subsystem (Windows native)`. The linker must use
`/SUBSYSTEM:NATIVE`. The project sets `<SubSystem>Native</SubSystem>`; check that
the property is not overridden.

### Code analysis warnings

Milestone 1 treats `/analyze` as advisory. Fix real defects; do not suppress a
warning globally. If a warning is a false positive, document why in the code and
in the PR.

---

## 3. Repository hygiene failures

Run:

```powershell
.\scripts\Test-Repository.ps1
```

| Message | Fix |
| --- | --- |
| `Missing required file` | Add the file or update the required list if the layout intentionally changed |
| `Missing SPDX-License-Identifier: MIT` | Add the header to the source file |
| `Forbidden user-mode filesystem dependency referenced` | Remove the dependency from code; docs may discuss it |
| `Private key container must not be committed` | Remove the file and rotate the key |
| `Private key material found` | Remove the key, rotate it, and treat it as compromised |
| `Build/test binary must not be committed` | Add it to `.gitignore` and remove it from the index |
| `version ... does not match` | Update the header, INF, RC, and Cargo workspace together |

The script is a guard, not a complete security audit.

---

## 4. CLI problems

The current CLI only implements `version`, `help`, and `status`.

| Exit code | Meaning |
| --- | --- |
| 0 | Success |
| 64 | Usage error |
| 69 | Unavailable (wrong OS or missing tool) |
| 70 | Command not implemented in this build |

### `scan`, `mount`, etc. exit 70

Expected. The commands are not implemented. The CLI prints an explanation and
does not fake success.

### `status` says "not installed (Windows error 1060)"

The driver service is not installed. That is expected on a clean machine. See
[`DRIVER-INSTALLATION.md`](DRIVER-INSTALLATION.md).

### `status` requires Windows

The `status` command uses `sc.exe`. On Linux/macOS it exits 69. The other
commands are not platform-dependent, but the CLI is a Windows management tool.

---

## 5. Driver install and load failures

### `sc start` returns error 577

Windows rejected the driver signature.

Check:

```powershell
bcdedit /enum "{current}"
```

Confirm `testsigning Yes`. Confirm Secure Boot is off **in the VM** (not on the
host). Confirm the certificate is in `LocalMachine\Root` and
`LocalMachine\TrustedPublisher`. Verify:

```powershell
signtool verify /v /kp .\STGRExt4.sys
signtool verify /v /pa /c .\STGRExt4.cat
```

Do not disable signature enforcement by unsupported means. If the signature is
wrong, fix the signing procedure or use a properly signed package.

### Error 1275 or "driver blocked"

The driver is unsigned or the certificate is not trusted. See the signing
document. A production Windows 11 machine with Secure Boot will not load an
unsigned driver, and that is correct behaviour.

### Service starts and immediately stops

`DriverEntry` failed, or the service configuration is wrong.

- Check `sc query STGRExt4`.
- Check the System event log.
- Check the debug output for the failure status.
- Confirm `StartType = 1` and `LoadOrderGroup = "File System"` if installed
  manually.

Common causes: `IoCreateDevice` failure (name collision), resource
initialization failure, or a missing dependency.

### `pnputil` ignores the INF

A non-PnP file system driver may not install through the PnP path on every
build. Use the `sc create` fallback in
[`DRIVER-INSTALLATION.md`](DRIVER-INSTALLATION.md), and inspect
`%SystemRoot%\INF\setupapi.dev.log`.

### Service is running but no drive letter appears

Expected. Milestone 1 does not create a VDO, VPB, or drive letter. It declines
every volume. A drive letter would be a bug.

### Service is running and a volume is mounted

This build cannot mount anything. You are running a different binary, another
file system driver, or a build from a later milestone. Stop and identify the
binary by hash before touching the volume.

---

## 6. Expected refusals

These are not bugs in Milestone 1:

| Observation | Meaning |
| --- | --- |
| Every volume is refused | The mount handler returns `STATUS_UNRECOGNIZED_VOLUME` |
| No drive letter | No VDO/VPB exists |
| No files visible | No read path exists |
| `mount` CLI exits 70 | Not implemented |
| Catalog exists but install fails | The catalog is unsigned |

A refusal is the correct safe behaviour until the parser and mount path exist.
Do not "fix" it by making the mount handler return success.

---

## 7. Kernel debugging

### Checked build and DbgPrint

Debug builds use `DbgPrint`. To see output:

- attach a kernel debugger (WinDbg) to the VM;
- or use Sysinternals DebugView with kernel capture enabled.

Free builds compile logging out. The absence of a log line is not proof that a
path was not reached.

### Driver Verifier

Enable it in the **VM only**:

```powershell
verifier /standard /driver STGRExt4.sys
verifier /querysettings
```

Reboot after changing settings. Reproduce the issue. Then:

```powershell
verifier /reset
```

Reset before restoring a checkpoint or uninstalling, or the next boot may
bugcheck on a missing driver.

Verifier options to use for a serious run:

- Special Pool;
- Pool Tracking;
- I/O Verification;
- IRQL Checking;
- Deadlock Detection;
- Force IRQL Checking;
- Low Resources Simulation (later, for allocation failures).

### Crash dump

Configure the VM for a kernel memory dump or a complete memory dump. After a
bugcheck, open the dump in WinDbg:

```text
!analyze -v
!verifier 3 STGRExt4.sys
lm
```

Record the bugcheck code, the stack, and the driver version. Do not attach a
dump that contains user data to a public issue.

### Debugging a hang

- Capture a dump of the hung guest.
- Check for a lock held across a blocking I/O.
- Check the lock order: volume → FCB list → FCB → cache.
- Check for an IRP completed twice or never completed.
- Check paging I/O for a blocking wait.

A hang is as serious as a bugcheck.

---

## 8. Status codes

| Status | Meaning in this project |
| --- | --- |
| `STATUS_UNRECOGNIZED_VOLUME` | Refusal: not EXT4, unsupported, unapproved, or not implemented |
| `STATUS_INVALID_DEVICE_REQUEST` | No handler for this IRP at this device |
| `STATUS_NOT_IMPLEMENTED` | Deliberately not implemented |
| `STATUS_NOT_SUPPORTED` | Valid request, unsupported semantics (for example ADS) |
| `STATUS_MEDIA_WRITE_PROTECTED` | Volume is read-only or write was not approved |
| `STATUS_FILE_CORRUPT_ERROR` | Metadata/checksum inconsistency |
| `STATUS_DEVICE_NOT_CONNECTED` | Surprise removal or dead device |
| `STATUS_DEVICE_BUSY` | Volume already mounted or in use |
| `STATUS_DISK_FULL` | Allocation failed |
| `STATUS_ACCESS_DENIED` | Security or approval check failed |
| `STATUS_INSUFFICIENT_RESOURCES` | Allocation or object creation failed |

The exact mapping is implemented in one place in the front end. If a message
and a status disagree, the status is authoritative.

---

## 9. Event Viewer and logs

Useful logs:

- **System** — service start/stop, driver load failures, bugchecks.
- **Application** — management CLI/service errors.
- `%SystemRoot%\INF\setupapi.dev.log` — INF installation.
- `%SystemRoot%\LiveKernelReports\` — live dumps.
- `%SystemRoot%\Minidump\` — crash dumps.

Collect:

```powershell
Get-WinEvent -LogName System -MaxEvents 200 |
    Where-Object { $_.Message -match "STGRExt4|STGR EXT4|BugCheck" } |
    Format-List TimeCreated, Id, LevelDisplayName, Message
```

Redact paths, UUIDs, and user names before sharing.

---

## 10. If a volume may be corrupted

1. Stop using the driver.
2. Do not write to the volume.
3. If possible, image the device at the block level.
4. Work on a copy, never the only copy.
5. Run `e2fsck -fn` on the copy and save the output.
6. Do not run `e2fsck -fy`, `chkdsk`, or a partition tool on the original.
7. Record the build hash, feature set, journal state, and exact sequence.
8. Report the issue with a synthetic reproduction if possible.

`e2fsck -fn` is a check, not a repair. The project does not repair volumes
automatically and does not recommend repair as a first response.

---

## 11. Removing the driver

```powershell
sc.exe stop STGRExt4
sc.exe delete STGRExt4
Remove-Item "$env:SystemRoot\System32\drivers\STGRExt4.sys" -Force
```

If installed via `pnputil`:

```powershell
pnputil /enum-drivers
pnputil /delete-driver oemXX.inf /uninstall /force
```

Then:

```powershell
bcdedit /set testsigning off
```

Reboot. Restore the VM checkpoint if anything looks wrong. Removing the driver
must never touch the data volume.

---

## 12. What not to do

- Do not install on a production machine or a dual-boot workstation.
- Do not attach a physical disk to a test VM.
- Do not run `chkdsk` on an EXT4 volume.
- Do not accept a Windows prompt to format an unrecognized volume.
- Do not disable Secure Boot permanently to load an unsigned build.
- Do not enable `nointegritychecks` or patch the kernel.
- Do not edit the mount handler to return success to "see if it works".
- Do not write to a volume with a dirty journal.
- Do not claim a test passed because the machine booted.
- Do not publish a private key or a dump containing personal data.

---

## 13. Getting help

Include:

- commit and artifact SHA-256;
- Windows build and VM/host;
- Secure Boot and test-signing state;
- exact commands and output;
- feature set and `dumpe2fs -h` output (redact the UUID if desired);
- logs, dump, and `e2fsck -fn` output;
- what you already tried.

Do not include a disk image with personal data. Reproduce on a synthetic image.

For security issues, follow [`SECURITY.md`](../SECURITY.md), not a public issue.
