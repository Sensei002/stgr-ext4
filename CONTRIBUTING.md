# Contributing to STGR EXT4

STGR EXT4 is a kernel-mode file system. A mistake can corrupt a disk or
bugcheck a machine. Contributions are welcome, but the review bar is higher
than for an ordinary application, and "it builds" is not sufficient evidence.

## Before you start

1. Read [`docs/FEASIBILITY.md`](docs/FEASIBILITY.md),
   [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), and
   [`docs/DATA-INTEGRITY.md`](docs/DATA-INTEGRITY.md).
2. Read [`docs/WINDOWS-IFS.md`](docs/WINDOWS-IFS.md) if you are touching the
   Windows front end, or [`docs/EXT4-FORMAT.md`](docs/EXT4-FORMAT.md) if you are
   touching the engine.
3. Check [`ROADMAP.md`](ROADMAP.md) for the current milestone. Work outside the
   current milestone is usually deferred, especially write support.
4. Open an issue before a large change. Parser changes, mount changes, and
   anything that writes to disk need design review first.

## Non-negotiable rules

- **No user-mode file system dependency.** WinFsp, Dokany, FUSE, a proxy
  process, and a Linux VM are not acceptable in the data path.
- **No copied GPL kernel code.** Linux `fs/ext4` and `fs/jbd2` are GPL-2.0. The
  engine is written from the public on-disk specification. Do not paste kernel
  source, comments, or tables.
- **No WinBtrfs code.** WinBtrfs is LGPL-3.0 and is an architectural reference
  only.
- **No fake success.** A function that is not implemented returns a documented
  failure (`STATUS_NOT_IMPLEMENTED`, `STATUS_INVALID_DEVICE_REQUEST`, or the
  CLI's exit code 70). It never returns success.
- **No silent data modification.** Never add a path that formats, repairs,
  resizes, rewrites a partition table, or "fixes" a file system automatically.
- **No unapproved mount.** Volume approval by UUID is mandatory. Do not add a
  wildcard or a "mount everything" debug shortcut that can reach a release
  build.
- **No private keys or binaries in the repository.** Certificates, `.pfx`,
  `.pvk`, `.snk`, `.sys`, `.cat`, and test images stay out. The hygiene script
  enforces this.
- **No uncompilable claims.** Do not state in a document, commit, or PR that a
  build, test, signature, or CI run succeeded unless it did.

## Development workflow

1. Fork or branch from `main`.
2. Make a focused change. Keep unrelated reformatting out of the diff.
3. Add or update tests at the same layer as the change.
4. Run the checks you can run.
5. Open a pull request describing:
   - what changed and why;
   - the failure mode if the change is wrong;
   - what you ran, with exact commands;
   - what you could not run and why.

There is no requirement to compile the driver locally. GitHub Actions is the
supported build environment. However, a PR that touches kernel code and has
never been compiled anywhere will be reviewed as unverified source, not as a
working change.

## Build and test commands

### Repository hygiene (no compiler)

```powershell
.\scripts\Test-Repository.ps1
```

### Driver (Windows with VS 2022 + WDK toolset)

```powershell
nuget restore .\packages.config -PackagesDirectory .\packages\
.\scripts\Build-Driver.ps1 -Configuration Debug -Platform x64
.\scripts\Invoke-Inf2Cat.ps1 -PackageDir .\artifacts\package\x64\Debug
.\scripts\Test-DriverImage.ps1 -DriverPath .\artifacts\package\x64\Debug\STGRExt4.sys
```

### Management tool

```powershell
cd management
cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo test
```

The CI workflows run the same commands. See
[`docs/GITHUB-ACTIONS.md`](docs/GITHUB-ACTIONS.md).

## Coding standards

### C driver

- C, not C++. Compile cleanly at `/W4 /WX`.
- No exceptions, no C++ runtime, no floating point, no VLAs, no recursion over
  untrusted depth.
- Kernel APIs only. Allocate with pool tags; check every allocation.
- Declare IRQL assumptions and assert them where practical.
- Prefer explicit status returns. Never swallow an error.
- No direct casts of untrusted on-disk buffers to structures. Decode fields
  explicitly with little-endian helpers.
- Check every addition and multiplication for overflow.
- Keep functions short enough to reason about, but do not split error handling
  in a way that hides a leaked reference.
- Every source file carries `SPDX-License-Identifier: MIT`.
- Follow the existing comment style: a short module header, then focused
  comments that explain *why*, not *what*.

### Rust management tool

- Edition 2021, Rust 1.83.0, no external crates without prior discussion.
- `cargo fmt` and `cargo clippy --all-targets -- -D warnings` must pass.
- No `unwrap()` or `expect()` on fallible runtime input in production paths.
- Errors are reported to stderr with a non-zero exit code. Do not print a
  success message when an operation failed.
- Exit codes are part of the interface: 0 success, 64 usage, 69 unavailable,
  70 not implemented.

### Documentation

- Documentation is part of the product. If behaviour changes, update the
  relevant file in `docs/` in the same PR.
- State what is implemented, what is not, and what was actually tested.
- Avoid unqualified words such as "secure", "production-ready", "fast", or
  "compatible". Qualify them or remove them.
- Use fenced code blocks with a language tag.
- Keep line endings and encoding as defined by `.editorconfig` and
  `.gitattributes`.

## Commit and pull request conventions

- Use an imperative subject line, at most 72 characters, for example:
  - `driver: decline mount probes without a volume parser`
  - `engine: bound superblock reads to one block`
  - `docs: record journal replay policy`
- Prefixes in use: `driver:`, `engine:`, `management:`, `build:`, `ci:`,
  `docs:`, `test:`, `security:`.
- Explain the safety impact in the body when the change affects parsing,
  mounting, allocation, or write ordering.
- Reference the issue or milestone.
- Keep commits reviewable. A 4,000-line parser drop with no tests will be sent
  back.

## Pull request checklist

- [ ] The change is within the current milestone or explicitly approved.
- [ ] No user-mode filesystem dependency was introduced.
- [ ] No GPL/LGPL source was copied.
- [ ] No private key, certificate, or binary was added.
- [ ] New failure paths return a truthful error, not success.
- [ ] Bounds, overflow, and lifetime reasoning is documented where relevant.
- [ ] Tests were added at the correct layer.
- [ ] `Test-Repository.ps1` passes.
- [ ] `cargo fmt --check`, Clippy, and tests pass if Rust changed.
- [ ] The driver was compiled if kernel code changed (CI is acceptable).
- [ ] Documentation was updated.
- [ ] The PR states what was **not** run.

## Testing expectations

| Change | Minimum evidence |
| --- | --- |
| Documentation only | Hygiene script; links resolve |
| Rust CLI | fmt, Clippy, unit and process tests |
| Driver build metadata | CI driver build, `dumpbin` check |
| Superblock/parser | Host unit tests, corrupt-image corpus |
| Mount/unmount | Disposable VM, Driver Verifier, logs |
| Journal or write path | Crash-injection matrix plus `e2fsck -fn` |
| Security-sensitive parsing | Fuzz target and review |

Host tests for the portable engine do not exist yet. When they are added, they
must run on every PR. Kernel tests run only on a dedicated self-hosted runner
or a local lab VM; GitHub-hosted runners must never be used to install or load
the driver.

## Reporting bugs

Use the issue tracker. For security-sensitive reports, follow
[`SECURITY.md`](SECURITY.md) instead of opening a public issue.

A useful bug report includes:

- the exact build identifier and how it was produced;
- Windows version, build number, and Secure Boot/test-signing state;
- the EXT4 feature set (`dumpe2fs -h` output from Linux, with the UUID
  redacted if desired);
- whether the volume is read-only, clean, and approved;
- the relevant Event Viewer, WPP/ETW, or crash-dump excerpt;
- the exact steps and whether the issue reproduces on a disposable image.

**Do not attach a disk image that contains personal data.** Reproduce on a
synthetic image instead.

## License of contributions

Original contributions are accepted under the MIT License (see `LICENSE`). By
submitting a pull request you confirm that you have the right to license the
work under those terms and that it contains no code you cannot license this way.

If you believe a contribution needs a different license (for example, because
it derives from LGPL or GPL code), stop and open an issue before submitting.
The project license would have to be revisited first.

## Code of conduct

Be direct, technical, and respectful. Critique code and evidence, not people.
Bad-faith claims of testing, harassment, and pressure to enable unsafe write
support are grounds for removal from the project.
