# Testing strategy

Status: **no filesystem test has been executed against a volume.** Compile and
static layers run in CI: run 8 (`55a1236`) was green for hygiene, the Rust CLI
(format, Clippy, tests, link), Debug/Release x64 driver builds with catalog
generation and the PE check, and the engine host tests. `scripts/Test-Engine.ps1`
compiles the engine and its tests with MSVC at `/W4 /WX` and runs them: **71
checks, 0 failures** in run 8. Every image those tests use is built in memory,
so none of it proves parsing of a real `mke2fs` volume. The driver has never
been loaded, no VM exists, and there is no EXT4 image corpus.

This document defines the test ladder, the evidence required at each level, and
the boundary between what CI can prove and what only a lab can prove.

---

## 1. Principles

1. **Test at the lowest layer that can reproduce the bug.** A parser bug belongs
   in a host unit test, not a kernel debugger session.
2. **A test that cannot fail is not a test.** Assertions must check the exact
   result, not just "no crash".
3. **Corrupt input is normal input.** Every parser is tested with malformed,
   truncated, and adversarial data.
4. **Kernel tests use disposable disks only.** A real disk is never a target.
5. **Evidence is recorded.** A claim without a log, hash, and commit is not a
   result.
6. **No test is skipped silently.** If a test could not run, the report says so.

---

## 2. Test layers

| Layer | Runs on | Proves | Does not prove |
| --- | --- | --- | --- |
| L0 Repository hygiene | Any host | Structure, licensing, secrets, versions | Code correctness |
| L1 Compile and static analysis | CI | Types, links, warnings, some defects | Runtime behaviour |
| L2 Engine unit tests | Host | Parsing, bounds, checksums | Kernel integration |
| L3 Corrupt-image and fuzz | Host | Memory safety of the parser | Lifetime/IRQL correctness |
| L4 Driver load/unload | VM | Registration, teardown, Verifier | File system semantics |
| L5 Mount/read | VM | Windows integration, hashes | Write consistency |
| L6 Journal/replay | VM | Crash consistency | Performance |
| L7 Write/crash | VM | Allocation, transactions | Real hardware |
| L8 Hardware/hotplug | Lab | Surprise removal, power loss | Every device |
| L9 Games/performance | Lab | Application behaviour | General correctness |

A higher layer never substitutes for a lower one. A driver that mounts in a VM
but has an unchecked parser is not tested.

---

## 3. L0 — Repository hygiene

Command:

```powershell
.\scripts\Test-Repository.ps1
```

Checks:

- required files exist and are non-empty;
- C/H/RC/Rust sources carry `SPDX-License-Identifier: MIT`;
- code does not reference WinFsp, Dokany, or FUSE;
- no `.pfx`, `.pvk`, `.snk`, `.sys`, `.cat`, `.pdb`, `.img`, `.vhd`, `.dmp`
  files are committed;
- no PEM private key block is present;
- driver header, INF, RC, and Cargo versions agree.

It does not compile anything. It has been run in this workspace and passed.

---

## 4. L1 — Compile and static analysis

### Driver

```powershell
nuget restore .\packages.config -PackagesDirectory .\packages\
.\scripts\Build-Driver.ps1 -Configuration Debug -Platform x64
.\scripts\Build-Driver.ps1 -Configuration Release -Platform x64
```

Requirements:

- `/W4 /WX`: no warnings;
- `/analyze` (PREfast) clean or triaged with a written justification;
- link as `Subsystem=Native`;
- PE check: x64 native.

The project file enables `/W4`/`/WX` and disables exceptions. It does not yet
run `/analyze` in CI; the script accepts `-RunCodeAnalysis` but the workflow
does not pass it. Code analysis becomes blocking when the baseline is clean.

### Management

```powershell
cd management
cargo fmt --all -- --check
cargo clippy --workspace --all-targets -- -D warnings
cargo test --workspace
cargo build --workspace --release
```

The CLI has no external crates. Clippy warnings are errors.

---

## 5. L2 — Engine unit tests (host)

The portable engine (`driver/ext4`) is freestanding C. `scripts/Test-Engine.ps1`
locates MSVC through `vswhere`, compiles the engine plus `driver/ext4/tests` at
`/W4 /WX` as an ordinary host executable, and runs it. CI calls the same script
from the `Engine host tests` job, so a parser change is proven without a driver,
a partition, or a VM.

The harness today has an in-memory block device with a configurable size and a
short-read mode that exercises the read boundary. Planned additions, not yet
written: a file-backed device, injected I/O errors, torn writes, lost flushes,
and an allocator that can fail on demand.

Test categories:

| Area | Examples |
| --- | --- |
| Endian helpers | Known byte sequences, unaligned input |
| Overflow | Counts and offsets near `SIZE_MAX`, `UINT32_MAX` |
| Superblock | Valid images, bad magic, bad geometry, refused features, checksum gate |
| Group descriptors | 32/64-byte, sparse, flex_bg, checksum |
| Bitmaps | Last partial byte, uninitialised groups |
| Inodes | 128/256-byte, extra_isize, size high/low, checksum |
| Extents | Leaf/index, unwritten, depth 5, cycles, overlap |
| Directories | Linear, rec_len edge cases, HTREE, collisions |
| Xattrs | In-inode, external, value bounds, duplicate names |
| Journal | Superblock, descriptor, commit, revoke, checksums |
| Allocation | No double allocation, no leak, group counters |

A unit test must assert exact values and status codes. A test that only checks
"did not crash" is a smoke test, not a correctness test.

---

## 6. L3 — Corrupt images and fuzzing

### Corpus generation (Linux)

`scripts/New-Ext4Corpus.sh` runs in the `EXT4 image corpus` CI job on
`ubuntu-latest` with `e2fsprogs` from apt. It creates:

| Image | Expected result |
| --- | --- |
| `accept-1k`, `accept-2k`, `accept-4k` | accepted; block size 1024, 2048, 4096 |
| `accept-4k-32byte-desc` | accepted; `^64bit`, so descriptors are 32 bytes |
| `refuse-metadata-csum` | `StgrStatusChecksumNotVerified` |
| `refuse-bigalloc` | `StgrStatusBadGeometry` — mke2fs writes `blocks_per_group = 524288` (cluster ceiling), over the engine's `blockSize*8` group bitmap limit, and the image has no extents/filetype |
| `refuse-inline-data`, `refuse-encrypt` | `StgrStatusUnsupportedFeature` |
| `refuse-magic` | `StgrStatusBadMagic` |

The job writes `expectations.txt` (name, expected status, expected block size),
`manifest.sha256`, `toolchain.txt` (the `mke2fs` version and every command
line) and `baseline.txt` (`e2fsck -fn` on the images that must be accepted).
The Windows `Engine host tests` job downloads the artifact, verifies every
SHA-256 with `Get-FileHash`, and only then runs the test binary with `--corpus`.

Images are **not committed**. `e2fsprogs` is recorded, not pinned: the apt
version is whatever the runner image carries, and `toolchain.txt` is the record
of it.

Still to build: populated trees via `debugfs`, dirty journals, byte mutations
at every structure boundary, truncation at every 512-byte boundary, and the
fuzzers.

### Fuzzing

- Coverage-guided fuzzing of `stgr_superblock_parse`, `stgr_inode_parse`,
  `stgr_extent_walk`, `stgr_dir_iterate`, `stgr_xattr_parse`, and
  `stgr_journal_scan`.
- ASan and UBSan on the host.
- A fixed seed corpus plus generated mutations.
- Every crash is minimized and added to the regression corpus.
- Fuzzing does not run in the kernel. The kernel gets the fixed code.

A parser that has not been fuzzed is not ready for a kernel release.

---

## 7. L4 — Driver load and unload (VM)

Topology:

```text
Host (dedicated test machine)
  Hyper-V / VMware
    Guest: Windows 11 x64, clean checkpoint
      - Secure Boot off for test signing (development only)
      - testsigning on
      - Driver Verifier enabled for STGRExt4.sys
      - kernel debugger attached
      - system disk + disposable data disk
```

Procedure:

1. Restore a clean checkpoint.
2. Install the test-signed package (see
   [`DRIVER-INSTALLATION.md`](DRIVER-INSTALLATION.md)).
3. Start the service.
4. Confirm the FSDO is registered.
5. Confirm no volume is mounted and no drive letter appears.
6. Stop and delete the service.
7. Confirm unload without a bugcheck or leaked reference.
8. Check the Verifier log and the debugger output.

Expected for Milestone 1:

- service starts;
- `\Device\STGRExt4` exists;
- registration succeeds;
- every mount probe is declined;
- unload is clean.

A failure at any step blocks the milestone.

---

## 8. L5 — Mount and read (future)

Prerequisites: read path, VDO/VPB, allowlist, cache integration.

Test matrix:

| Dimension | Values |
| --- | --- |
| Block size | 1 KiB, 2 KiB, 4 KiB |
| Features | Each supported combination |
| Tree | Deep, wide, long names, non-ASCII, symlinks, hard links |
| Files | Tiny, 4 GiB+, sparse, many small, one huge |
| Concurrency | Single reader, many readers, mmap |
| State | Clean, dirty (must refuse), corrupt (must refuse) |
| Approval | Approved, unapproved (must refuse) |

Verification:

- compare every file hash with the Linux manifest;
- compare directory listings, including metadata;
- check timestamps within the documented tolerance;
- check symlink targets;
- check error codes for unsupported ADS and special files;
- run for hours with concurrent readers.

A volume that enumerates but returns wrong bytes is a failure.

---

## 9. L6 — Journal replay (future)

Cases:

- clean journal (`s_start = 0`);
- committed transaction needing replay;
- descriptor without commit (must not replay);
- revoke before/after data;
- checksum failure (must refuse);
- 64-bit block numbers;
- sequence wrap;
- external journal (must refuse);
- `orphan_present` (must refuse write).

Procedure:

1. Generate an unclean image on Linux.
2. Copy it; keep the original untouched.
3. Replay on the copy.
4. Run `e2fsck -fn`.
5. Compare contents with the expected manifest.
6. Repeat with the replayed image.
7. Record the exact commands and output.

Replay on the original device is not allowed until the copy path passes.

---

## 10. L7 — Write and crash consistency (future)

This is the highest-risk layer.

Test operations:

- create, write, append, truncate, delete;
- rename within and across directories;
- link, unlink, mkdir, rmdir;
- set timestamps, size, and metadata;
- allocate until full, then free;
- many small files and one huge file;
- concurrent writers.

Failure injection:

- power loss at every journal phase;
- torn writes;
- lost flushes;
- reordered writes;
- EIO at every I/O;
- surprise removal;
- disk full;
- allocation failure.

After every run:

1. Restore the snapshot or use a fresh copy.
2. Replay if needed.
3. `e2fsck -fn` — must be clean.
4. Compare content hashes.
5. Check for leaked blocks and inodes.
6. Record the result.

Any unexplained corruption blocks the release.

---

## 11. L8 — Hardware and hotplug (future)

- SATA, NVMe, USB SSD.
- Unplug during read, write, and flush.
- Power loss on the lab machine.
- Reconnect and remount.
- Long soak with concurrent applications.
- Driver Verifier with all standard options.

No real user disk is used. The lab machine is dedicated and disposable.

---

## 12. L9 — Games and performance (future)

See [`GAMING-COMPATIBILITY.md`](GAMING-COMPATIBILITY.md). No title has been
tested, and no benchmark has been run.

---

## 13. CI versus lab

| Test | GitHub-hosted CI | Self-hosted lab |
| --- | --- | --- |
| Hygiene | Yes | Yes |
| Compile | Yes | Yes |
| Clippy/tests | Yes | Yes |
| Engine unit tests | Yes | Yes |
| Fuzzing | Short run (future) | Long run |
| Corrupt corpus | Yes (generated and parsed in CI) | Yes |
| Driver load/unload | **No** | Yes |
| Mount/read | **No** | Yes |
| Journal/replay | **No** | Yes |
| Write/crash | **No** | Yes |
| Hardware/hotplug | **No** | Yes |

A hosted runner is not an appropriate place to install an experimental kernel
driver, enable test signing, reboot, and run Verifier. It must never be used for
that.

---

## 14. Evidence record

Every test run records:

```text
commit:
workflow run / local command:
Windows build:
driver version and SHA-256:
WDK/SDK/MSVC versions:
Rust version:
e2fsprogs version:
image manifest and SHA-256:
feature set:
test case:
expected result:
actual result:
logs / dump path:
verdict:
```

A result without this record is an anecdote. A screenshot of Explorer is not a
substitute for a hash comparison and `e2fsck -fn`.

---

## 15. Current status

| Test | Status |
| --- | --- |
| Hygiene script | Passes locally and in CI runs 7 and 8 |
| Driver compile | Green in CI runs 7 and 8 (Debug and Release x64) |
| Code analysis | Never run (`/analyze` is not enabled yet) |
| PE check | Green in CI runs 7 and 8 |
| CLI fmt/clippy/test | Green in CI runs 7 and 8 |
| Engine unit tests | Green in CI run 10: 71 checks, 0 failures; corpus parsed (9 images, 4 accepted, 5 refused for the documented reasons) |
| Corrupt corpus | Nine `mke2fs` images generated in CI; hashes verified before parsing; corpus pins each refusal reason |
| Fuzzing | Never run |
| Driver load/unload | Never run |
| Mount/read | Impossible (not implemented) |
| Journal/replay | Impossible (not implemented) |
| Write/crash | Impossible (not implemented) |
| Hardware/hotplug | Never run |
| Game/performance | Never run |

The correct conclusion is that the project has compiled source code, host-level
parser tests over synthetic images and over a hash-verified nine-image mke2fs
corpus (run 10), and no test evidence at any layer that touches a volume.
