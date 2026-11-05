# Security policy

STGR EXT4 is a Windows kernel-mode file system driver. It parses untrusted
on-disk data and runs with kernel privilege. Security defects can cause
bugchecks, memory corruption, privilege escalation, or data loss.

## Project status and support

The project is at Milestone 1. There is no supported release.

| Version | Supported | Notes |
| --- | --- | --- |
| `0.1.x` | No | Development skeleton. Never built or loaded in this workspace. |
| Older | No | Does not exist. |

Do not deploy any build from this repository on a production system, a
dual-boot workstation, or a machine that holds data you cannot lose. There is
no security support commitment until a signed, tested release exists.

## Reporting a vulnerability

Once the repository is published, use **GitHub private vulnerability
reporting** (Security → Advisories → Report a vulnerability). Do not open a
public issue for a suspected vulnerability.

If private reporting is unavailable, contact the maintainers through the
repository owner's published security contact. Do not send:

- private keys or certificates;
- personal data;
- full disk images containing third-party data;
- exploit code against systems you do not own.

A useful report includes:

- affected version/commit and how it was built;
- a description of the bug and its security impact;
- the smallest synthetic EXT4 image or input that reproduces it;
- the Windows version, build, and test-signing state;
- a crash dump or Verifier report if one exists;
- whether the issue is reachable from an unprivileged user, from a malicious
  image, or only from an administrator.

We will acknowledge receipt, investigate, and coordinate disclosure. No bug
bounty is offered. Do not test on other people's systems or data.

## Threat model

### Assets

- User data on the EXT4 volume.
- Integrity of the Windows kernel.
- Integrity of the machine's other disks and boot configuration.
- Credentials and secrets that may be present in memory or on disk.

### Adversaries and untrusted inputs

1. **A malicious or corrupted EXT4 image.** Mounted from a USB disk, a
   downloaded image, or a virtual disk. Every field is attacker-controlled.
2. **A malicious local user.** May be able to trigger mount attempts or feed
   crafted metadata, but is not assumed to have administrator rights.
3. **A compromised or buggy storage device.** May return short reads, stale
   data, torn writes, or errors at any time.
4. **Concurrent access from Linux.** The same volume may be mounted or
   hibernated by another operating system.
5. **Surprise removal.** The device may disappear mid-operation.

### Security properties the design intends to provide

- Bounds-checked parsing with no direct structure casts from disk buffers.
- Integer-overflow checks on every size and offset computation.
- Explicit allocation caps and depth caps.
- No execution of data from the volume.
- No writing to a volume that is not explicitly approved.
- No writing to a dirty volume or one that needs journal replay.
- No silent repair, format, resize, or partition-table modification.
- Kernel allocations use pool tags and are freed on every error path.
- IRP completion is single-shot; cancellation and unload are handled.
- Failure returns an error status. It never fabricates success.

### Out of scope

- Physical access attacks (cold boot, DMA, disk theft).
- A compromised Windows kernel or a malicious administrator.
- Attacks that require the attacker to already have kernel code execution.
- Denial of service caused by intentionally mounting a corrupt disk when the
  user has been warned; a bugcheck is still a bug, but it is not a privilege
  boundary violation.
- Data recovery from a disk that is already damaged.
- Encryption of the volume (EXT4 `encrypt` is refused).
- Antivirus/EDR compatibility.
- Secure Boot bypass. The project never asks users to disable Secure Boot for
  a production install.

## Security-relevant architecture decisions

| Decision | Security reason |
| --- | --- |
| Native kernel driver, no user-mode proxy | Removes an IPC attack surface and a second failure domain, and is required for correct cache/paging semantics. |
| Portable engine with callback block I/O | The parser can be fuzzed in user mode with ASan/UBSan before it sees kernel memory. |
| No direct casts of disk structures | Prevents alignment, endianness, and over-read bugs. |
| Explicit little-endian decoders | Makes every read auditable. |
| Feature allowlist | Unknown features are refused instead of misinterpreted. |
| Approved-volume allowlist | Prevents automatic mounting of an attacker-supplied volume. |
| Read-only first | Removes the write path from the initial attack surface. |
| Metadata checksum verification | Detects corruption and some tampering before structures are trusted. |
| No automatic repair | A repair is a write; an unverified repair is a corruption vector. |
| Unsigned artifacts are labelled | Prevents users from mistaking a development build for a trusted one. |

## Hardening requirements for code review

Any change that parses disk data must answer:

1. What is the maximum size of the input?
2. Can any offset, length, count, or multiplication overflow?
3. Is every block and inode number checked against the volume geometry?
4. Is every pointer derived from a validated offset?
5. Is the structure's checksum verified before use?
6. What happens on a short read, a torn read, or an I/O error?
7. What happens if the same structure is mutated concurrently?
8. Is there a depth, count, or allocation cap?
9. Does every error path free its allocations and release its locks?
10. Does the failure return an error rather than a partial success?

A "no" or "not sure" is a blocker.

## Driver Verifier and fuzzing

Before any release:

- Driver Verifier with Special Pool, Pool Tracking, I/O Verification, IRQL
  Checking, Deadlock Detection, and Force IRQL Checking.
- A structure-aware fuzzer over superblock, group descriptors, inodes, extent
  trees, directories, xattrs, and journal blocks.
- A corrupt-image corpus that must be rejected without a bugcheck.
- A crash-injection matrix for the write path, with `e2fsck -fn` after every
  run.

These have **not** been performed. The absence of a report is not evidence of
security.

## Supply chain

- GitHub Actions are pinned to full commit SHAs.
- WDK/SDK NuGet versions are pinned in `packages.config`.
- The Rust toolchain is pinned in `rust-toolchain.toml`; the CLI has no
  external crates.
- No signing keys are stored in the repository. Release signing, when it
  exists, uses a protected CI environment with short-lived credentials.
- `scripts/Test-Repository.ps1` rejects private key material, committed
  binaries, and user-mode filesystem dependencies.
- NuGet package hashes are not yet verified after restore. This is a known
  supply-chain gap recorded in the feasibility report.

## Disclosure

Please allow time for a fix and a coordinated release before publishing
details. Reporters who follow this policy will be credited unless they ask not
to be. The project will publish an advisory that states the affected versions,
the impact, the fix, and any mitigation. It will not describe an unverified
fix as verified.
