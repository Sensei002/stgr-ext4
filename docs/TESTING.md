# Testing strategy

Status: **no filesystem test has been executed.** Static checks that have run in
this workspace are limited to `scripts/Test-Repository.ps1` (all required-file,
SPDX, dependency, secret, and version checks passed) and the Rust workspace's
`cargo fmt --all -- --check` and
`cargo clippy --workspace --all-targets -- -D warnings` (clean on Rust 1.83.0).
The Rust unit and process tests were **not** executed: no linker is installed,
so no test binary could be produced. There is no driver build, no VM, and no
EXT4 image corpus.

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

## 5. L2 — Engine unit tests (future)

The portable engine (`driver/ext4`) is compiled into a host binary with:

- an in-memory block device;
- a file-backed block device;
- a fault-injecting device (short read, EIO, torn write, lost flush);
- an allocator that can fail on demand.

Test categories:

| Area | Examples |
| --- | --- |
| Endian helpers | Known byte sequences, unaligned input |
| Overflow | Counts and offsets near `SIZE_MAX`, `UINT32_MAX` |
| Superblock | Valid images, bad magic, bad geometry, bad checksum |
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

A future CI job or lab script creates images with a pinned `e2fsprogs`:

```bash
mke2fs -t ext4 -b 4096 -O extents,64bit,flex_bg,dir_index,filetype,sparse_super,large_file,metadata_csum ...
mke2fs -t ext4 -b 1024 ...
mke2fs -t ext4 -O ^metadata_csum ...
mke2fs -t ext4 -O bigalloc ...
mke2fs -t ext4 -O inline_data ...
mke2fs -t ext4 -O encrypt ...
```

Then:

- populate trees with `debugfs` or a loop mount;
- create sparse files, symlinks, hard links, xattrs, long names, non-ASCII
  names;
- dirty the journal with `debugfs` or an interrupted write;
- mutate bytes at every structure boundary;
- truncate at every 512-byte boundary;
- run `e2fsck -fn` on the unmutated originals to establish a baseline.

Images are **not committed**. They are artifacts with recorded hashes. The
`e2fsprogs` version, command line, and hash go in a manifest.

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
| Engine unit tests | Yes (future) | Yes |
| Fuzzing | Short run (future) | Long run |
| Corrupt corpus | Yes (future) | Yes |
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
| Hygiene script | Never run |
| Driver compile | Never run |
| Code analysis | Never run |
| PE check | Never run |
| CLI fmt/clippy/test | Never run |
| Engine unit tests | Do not exist |
| Corrupt corpus | Does not exist |
| Fuzzing | Never run |
| Driver load/unload | Never run |
| Mount/read | Impossible (not implemented) |
| Journal/replay | Impossible (not implemented) |
| Write/crash | Impossible (not implemented) |
| Hardware/hotplug | Never run |
| Game/performance | Never run |

The correct conclusion is that the project has source code and no test evidence.
