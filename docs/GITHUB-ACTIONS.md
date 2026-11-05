# GitHub Actions

Status: **CI executing.** `ci.yml` run 8 on `main` completed green: five jobs,
including the `Engine host tests` job that compiles the portable parser and runs
its unit tests (71 checks). The driver builds for Debug and Release x64,
produces an INF catalog, and passes a native x64 PE check. **No workflow loads
the driver, mounts a volume, or runs a VM test**, and nothing is signed.

The project is designed so that the supported build runs in GitHub Actions.
Contributors do not need MSVC, the WDK, or Rust locally.

---

## 0. Recorded run evidence

| Field | Value |
| --- | --- |
| Repository | `github.com/Sensei002/stgr-ext4`, branch `main` |
| CI run | [#10 / 42061325376](https://github.com/Sensei002/stgr-ext4/actions/runs/42061325376), `push`, 2026-11-05T16:07Z, attempt 1 |
| Commit | `8dbbbf3` — "Corpus: pin bigalloc to its real refusal reason and stop asserting the engine accepts it" |
| Jobs | `Repository hygiene`; `EXT4 image corpus`; `Engine host tests`; `Management / Rust 1.83.0`; `Driver / Debug x64`; `Driver / Release x64` — all `success` |
| Engine job | `Compile and run engine host tests` passed: `71 checks, 0 failure(s)`; corpus: 9 images parsed, 4 accepted |
| Corpus job | `EXT4 image corpus` passed on `ubuntu-latest`: nine `mke2fs` images, SHA-256 manifest verified by the engine job before parsing |
| Previous green run | [#9 / 42022394239](https://github.com/Sensei002/stgr-ext4/actions/runs/42022394239), `push`, 2026-11-05T11:52Z, commit `e08904d` — first green engine run, but `--corpus` was not passed (silent skip) |
| Driver steps | `Record tool versions`, `Record resolved WDK tool paths`, `Build driver`, `Generate unsigned catalog`, `Verify native x64 PE image`, artifact uploads — all `success` |
| Artifacts | `stgr-ext4-driver-Debug`, `stgr-ext4-driver-Release`, `stgr-ext4-symbols-Debug`, `stgr-ext4-symbols-Release`, `stgr-ext4-cli`, `stgr-ext4-corpus` (14-day retention) |
| Security run | [#7 / 41990893045](https://github.com/Sensei002/stgr-ext4/actions/runs/41990893045), `success` |
| Release workflow | Never run; no `v*.*.*` tag exists |

Runs 1–6 on this branch failed while the build was brought up; run 6 failed
only at `Verify native x64 PE image`, so compilation, catalog generation, and
packaging were already working. Run 7 corrected the expected dumpbin spelling.
Those earlier failures are superseded, not hidden.

Toolset versions printed by the `Record tool versions` and `Record resolved WDK
tool paths` steps of that run: PowerShell `7.6.9`, MSBuild `17.14.60.43110`,
NuGet `7.9.0.83`, MSVC `14.44.35207` (Visual Studio 2022 Enterprise), platform
`10.0.26100.0`, WDK/SDK content `10.0.26100.6584`, Rust `1.83.0`. §2 records
them with their sources.

---

## 1. Workflow map

| Workflow | Trigger | Purpose |
| --- | --- | --- |
| `ci.yml` | push to `main`, pull request, manual | Hygiene, then driver and management builds |
| `driver-build.yml` | reusable, manual | Debug and Release x64 driver packages, catalog, PE check |
| `management-build.yml` | reusable, manual | Rust format, Clippy, tests, release CLI |
| `security.yml` | push to `main`, PR, weekly schedule, manual | Hygiene, dependency review, Rust advisory audit |
| `release.yml` | tag `v*.*.*` | Builds and publishes an **unsigned** pre-release |

```text
ci.yml
  ├── hygiene (Test-Repository.ps1)
  ├── corpus (New-Ext4Corpus.sh on ubuntu-latest: mke2fs images + SHA-256)
  ├── engine (Test-Engine.ps1: host build of driver/ext4, corpus hash check,
  │           unit tests over synthetic and real superblocks)
  ├── driver  -> driver-build.yml (matrix: Debug, Release)
  └── management -> management-build.yml

release.yml (tag)
  ├── driver  -> driver-build.yml
  ├── management -> management-build.yml
  └── publish (downloads artifacts, SHA-256, unsigned pre-release)

security.yml
  ├── hygiene
  ├── dependency-review (pull requests only)
  └── rust-audit (Cargo advisory database)
```

All workflows have `contents: read` by default. Only the release publish job
has `contents: write`. No workflow has access to a signing key.

---

## 2. Runner and toolchain

| Component | Value | Source |
| --- | --- | --- |
| Runner (driver) | `windows-2022` | GitHub-hosted |
| Runner (hygiene, Rust) | `windows-2025` | GitHub-hosted |
| Visual Studio (driver) | 2022 Enterprise, 17.14 | `windows-2022` image |
| MSVC toolset | `14.44.35207` | `Record tool versions` (run 7 log) |
| MSBuild | `17.14.60.43110` via `microsoft/setup-msbuild` | `Record tool versions` (run 7 log) |
| PowerShell | `7.6.9` | Runner image; `Record tool versions` (run 7 log) |
| Platform version | `10.0.26100.0` (`TargetPlatformVersion`) | `Record resolved WDK tool paths` (run 7 log) |
| WDK toolset | `WindowsKernelModeDriver10.0` | WDK VS extension in runner image |
| WDK content | `Microsoft.Windows.WDK.x64` 10.0.26100.6584 | NuGet |
| SDK content | `Microsoft.Windows.SDK.CPP` / `.CPP.x64` 10.0.26100.6584 | NuGet |
| Rust | 1.83.0 | `rust-toolchain.toml` |
| NuGet | `7.9.0.83` | Runner image; `Record tool versions` (run 7 log) |

The `windows-2022` image supplies the WDK **Visual Studio extension** (the
platform toolset) but not the standalone WDK content. The project restores the
content from NuGet, which is Microsoft's documented CI approach. The versions
are pinned in [`packages.config`](../packages.config), and
[`Directory.Build.props`](../Directory.Build.props) imports them.

Driver builds stay on `windows-2022` because the pinned 10.0.26100 WDK package
ships `Microsoft.DriverKit.Build.Tasks.17.0.dll`. The `windows-2025` label now
resolves to Visual Studio 2026 (18.x), whose MSBuild requests the `18.0`
assembly and fails with `MSB4062`. Hygiene, security, management, and release
jobs do not load WDK MSBuild tasks and remain on `windows-2025`.

---

## 3. Pinned actions

Every action is pinned to a full commit SHA. A tag can move; a SHA cannot.

| Action | Version | SHA |
| --- | --- | --- |
| `actions/checkout` | v7.0.1 | `3d3c42e5aac5ba805825da76410c181273ba90b1` |
| `actions/upload-artifact` | v7.0.2 | `cf430e030ddbb5b0abf93d22962f4752f3646cd9` |
| `actions/download-artifact` | v8.0.2 | `9000827ccba6bdab643e8b6fd33ac0654aef8333` |
| `microsoft/setup-msbuild` | v3.0.0 | `30375c66a4eea26614e0d39710365f22f8b0af57` |
| `actions/dependency-review-action` | v5.0.0 | `a1d282b36b6f3519aa1f3fc636f609c47dddb294` |
| `rustsec/audit-check` | v2.0.0 | `69366f33c96575abad1ee0dba8212993eecbe998` |

To update an action:

1. Read the release notes and the diff between the old and new SHA.
2. Verify the new SHA belongs to the expected repository.
3. Update the workflow and this table in the same commit.
4. Treat a major version change as a security review, not a chore.

---

## 4. `ci.yml` in detail

### Trigger

- push to `main`;
- any pull request;
- manual dispatch.

Concurrency cancels an older run on the same ref, so a superseded push does not
consume a runner.

### Jobs

1. **hygiene** — `windows-2025`, runs
   `.\scripts\Test-Repository.ps1`.
2. **corpus** — `ubuntu-latest`, runs `scripts/New-Ext4Corpus.sh`. Builds nine
   EXT4 images with `mke2fs`, writes `expectations.txt`, `manifest.sha256`,
   `toolchain.txt` and an `e2fsck -fn` baseline, and uploads them as
   `stgr-ext4-corpus`. Linux is required because no Windows tool creates EXT4.
3. **engine** — `windows-2022`, runs `.\scripts\Test-Engine.ps1 -CorpusDir ...`
   after downloading the corpus. Verifies every image hash, compiles the
   freestanding engine and its host tests with MSVC at `/W4 /WX`, and runs them
   over both synthetic superblocks and the real images. No WDK, no driver
   install, no VM.
4. **driver** — calls `driver-build.yml` after hygiene passes; its build job
   runs on `windows-2022` so the pinned 26100 WDK task assemblies load.
5. **management** — calls `management-build.yml` after hygiene passes.

The engine, driver, and management jobs run in parallel once hygiene is green.

### What it proves

- the repository structure is complete;
- source files carry the SPDX identifier;
- no forbidden user-mode file system dependency is referenced in code;
- no private key or committed binary is present;
- versions agree across the header, INF, RC, and Cargo workspace;
- the driver compiles for Debug and Release x64 (if the job succeeds);
- the INF produces a catalog;
- the PE image is x64 native;
- the Rust CLI formats, lints, tests, and builds;
- the engine parses and validates a superblock on the host, and refuses bad
  magic, bad geometry, bad sizes, unclean volumes, and refused features with
  the exact status each test asserts;
- the corpus images are byte-identical to what Linux generated (every SHA-256
  is checked with `Get-FileHash` before the parse), and the engine accepts or
  refuses each one for the expected reason.

### What it does not prove

- the driver loads;
- the driver unloads;
- a volume is recognized;
- a file is read or written;
- the driver is safe;
- the release is signed.

---

## 5. `driver-build.yml` in detail

Matrix: `Debug` and `Release`, `x64`, `fail-fast: false`.

Steps:

1. Checkout.
2. `microsoft/setup-msbuild`.
3. `nuget restore .\packages.config -PackagesDirectory .\packages\`.
4. Record PowerShell, MSBuild, and NuGet versions in the log.
5. `.\scripts\Build-Driver.ps1 -Configuration <cfg> -Platform x64`.
6. `.\scripts\Invoke-Inf2Cat.ps1 -PackageDir .\artifacts\package\x64\<cfg>`.
7. `.\scripts\Test-DriverImage.ps1 -DriverPath ...\STGRExt4.sys`.
8. Upload the package as `stgr-ext4-driver-<cfg>`.
9. Upload PDBs as `stgr-ext4-symbols-<cfg>` (`warn` if absent).

The package contains `STGRExt4.sys`, `STGRExt4.inf`, `STGRExt4.cat` (unsigned),
and `README-UNSIGNED.txt`. The catalog proves the INF is structurally valid; it
does not make the package installable.

The PE check uses `dumpbin /headers` and requires:

- `machine (x64)`;
- `subsystem (Native)` or `subsystem (Windows native)`.

dumpbin labels the native subsystem `subsystem (Native)`; older MSVC toolchains
printed `Native Windows`. The check accepts either spelling so a correct kernel
image is not rejected, and it still fails on `Windows CUI`, `Windows GUI`, or
any other user-mode subsystem. The project sets `<SubSystem>Native</SubSystem>`
and `DriverType` `WDM`.

It catches a user-mode DLL or an x86 image, not a logic bug.

### Re-running a single configuration

Use **Actions → Driver build → Run workflow** and inspect the matrix job. There
is no input for configuration; the matrix always builds both. To build one
locally, pass `-Configuration Debug` or `-Configuration Release`.

---

## 6. `management-build.yml` in detail

Steps:

1. Checkout.
2. `rustup show` — honours `rust-toolchain.toml` and installs 1.83.0 with
   `rustfmt` and `clippy`.
3. `cargo fmt --all -- --check`.
4. `cargo clippy --workspace --all-targets -- -D warnings`.
5. `cargo test --workspace`.
6. `cargo build --workspace --release`.
7. Upload `management/target/release/stgr-ext4.exe` as `stgr-ext4-cli`.

The CLI has no external crates, so the build is fast. A `Cargo.lock` is not
committed; it is generated by Cargo in CI. For reproducible releases, commit a
lockfile once the dependency policy allows it.

---

## 7. `security.yml` in detail

### Hygiene

Runs the same repository checks as CI, including the private-key scan.

### Dependency review

Runs only on pull requests. It compares dependency changes and fails on high or
critical severity advisories. With no Rust dependencies, most changes have
nothing to review; the job remains useful for future tooling and Actions
updates.

### Rust audit

1. `cargo generate-lockfile` in `management`.
2. `rustsec/audit-check` with `working-directory: management`.

The action creates a check run on pushes and pull requests, and can create an
issue on a scheduled run. It needs `checks: write` and `issues: write`; the job
requests exactly those permissions. Forked pull requests may not be able to
create checks, in which case the action prints the result to the log.

A scheduled run executes weekly (Monday 04:17 UTC) to catch advisories published
after the last commit.

---

## 8. `release.yml` in detail

### Trigger

A tag matching `v*.*.*`.

### Version gate

The tag must equal `v<major>.<minor>.<patch>` from
`driver/include/STGRExt4.h`. For version 0.1.0 the tag is `v0.1.0`. The fourth
component (`0.1.0.0`) is the INF file version and is not part of the tag.

A mismatch fails the release before publishing.

### Jobs

1. `driver` — calls `driver-build.yml`.
2. `management` — calls `management-build.yml`.
3. `publish`:
   - downloads the Release and Debug driver packages and the CLI;
   - writes `RELEASE-NOTES.md` stating that the build is unsigned and untested;
   - generates `SHA256SUMS.txt` over every downloaded file;
   - creates a GitHub **pre-release** with `gh release create --prerelease`.

The release contains no signature. The notes must not be edited to imply one.
The workflow does not attach a `.pfx`, a certificate, or a signing log.

### Why Debug is published

The Debug package is useful for symbol-rich diagnosis in a test VM. It is not a
production artifact. The release notes say so.

---

## 9. Artifacts

| Artifact | Contents | Retention |
| --- | --- | --- |
| `stgr-ext4-driver-Debug` | `.sys`, `.inf`, `.cat` (unsigned), notice | 14 days |
| `stgr-ext4-driver-Release` | same, optimized build | 14 days |
| `stgr-ext4-symbols-Debug` | PDB, if produced | 14 days |
| `stgr-ext4-symbols-Release` | PDB, if produced | 14 days |
| `stgr-ext4-cli` | `stgr-ext4.exe` | 14 days |
| `stgr-ext4-corpus` | Nine EXT4 images, `expectations.txt`, SHA-256 manifest, toolchain record, `e2fsck` baseline | 14 days |
| Release assets | all of the above plus notes and SHA-256 | release lifetime |

Artifacts from a workflow run are not a release. They expire. A release is a
deliberate tag with notes and checksums.

---

## 10. What hosted CI cannot do

GitHub-hosted Windows runners are ephemeral and are not an appropriate place to
install an experimental kernel driver, enable test signing, reboot, and run
Driver Verifier.

Therefore:

- no workflow loads the driver;
- no workflow mounts a volume;
- no workflow runs a VM test;
- no workflow signs with a production certificate;
- no workflow claims kernel integration success.

Kernel testing requires a dedicated lab VM or a self-hosted runner labelled for
it (see [`TESTING.md`](TESTING.md)). Until that exists, no VM result may be
claimed.

Other limitations:

- The `windows-2022` (driver) and `windows-2025` (Rust/hygiene) labels are
  moving images. WDK/SDK content is pinned; MSVC is not. The run 7 values are
  in §2 — a snapshot, not a pin. Re-record them when the images change.
- NuGet package hashes are not verified after restore. A future improvement is
  to pin checksums.
- A green YAML parse is not a green build. Only a run is evidence.

---

## 11. First push and first run

The first push, the first green run, and the toolset transcription have all
happened (§0, §2). What remains from that checklist:

1. Download the artifacts only to a disposable VM.
2. Do not create a `v0.1.0` tag until CI is green and the version gate is
   understood. A tag publishes an unsigned pre-release.

### Recommended repository settings

- Protect `main`: require the CI checks, no force pushes, no direct pushes.
- Require review for changes to `driver/`, `.github/workflows/`, and `scripts/`.
- Enable private vulnerability reporting (see [`SECURITY.md`](../SECURITY.md)).
- Restrict who can create tags and releases.
- Do not add signing secrets until a protected environment exists.

### Secrets and variables

| Name | Required | Purpose |
| --- | --- | --- |
| `GITHUB_TOKEN` | automatic | Checkout, artifacts, release |
| Signing certificate / key | no | Not configured; signing is future work |
| `STGR_TEST_SIGNING` | no | Reserved for a future self-hosted test job |

No repository secret is needed for the current build.

---

## 12. Troubleshooting CI

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| `nuget` not found | Runner image changed | Install NuGet explicitly or use `nuget/setup-nuget` with a pinned SHA |
| WDK props missing | `nuget restore` failed or path changed | Check the restore step and `Directory.Build.props` |
| `msbuild` not found | `setup-msbuild` did not run | Check the action SHA and job step order |
| `Inf2Cat.exe` not found | WDK package not restored | Restore to `packages\` and inspect the package layout |
| `dumpbin` check fails | Wrong subsystem or architecture | Inspect the linker settings; do not weaken the check |
| `cargo fmt` fails | Formatting drift | Run `cargo fmt` locally or in a container |
| Clippy fails | A warning is treated as an error | Fix the warning; do not add `#[allow]` without review |
| Rust audit fails | Advisory or missing lockfile | Inspect the advisory; regenerate the lockfile |
| Artifact upload fails | Missing file | Check the build output path; do not set `if-no-files-found: ignore` |
| Release tag check fails | Tag does not match header | Retag from the correct commit |
| Release creation forbidden | Missing `contents: write` | Check job permissions and tag protection |

If a workflow is wrong, fix the workflow and re-run it. Do not merge a change
that makes a failing check optional in order to get a green badge.
