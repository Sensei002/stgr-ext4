# GitHub Actions

Status: **workflows authored, never executed.** This document describes what
the workflows are intended to do. It is not a report of a green build.

The project is designed so that the supported build runs in GitHub Actions.
Contributors do not need MSVC, the WDK, or Rust locally. The workspace used to
author this repository had none of those tools and no Git remote, so no run has
been observed.

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
| Visual Studio (driver) | 2022 (17.x) | `windows-2022` image |
| MSBuild | via `microsoft/setup-msbuild` | Action |
| WDK toolset | `WindowsKernelModeDriver10.0` | WDK VS extension in runner image |
| WDK content | `Microsoft.Windows.WDK.x64` 10.0.26100.6584 | NuGet |
| SDK content | `Microsoft.Windows.SDK.CPP` / `.CPP.x64` 10.0.26100.6584 | NuGet |
| Rust | 1.83.0 | `rust-toolchain.toml` |
| NuGet | runner-provided `nuget.exe` | Runner image |

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
2. **driver** — calls `driver-build.yml` after hygiene passes; its build job
   runs on `windows-2022` so the pinned 26100 WDK task assemblies load.
3. **management** — calls `management-build.yml` after hygiene passes.

The driver and management jobs run in parallel once hygiene is green.

### What it proves

- the repository structure is complete;
- source files carry the SPDX identifier;
- no forbidden user-mode file system dependency is referenced in code;
- no private key or committed binary is present;
- versions agree across the header, INF, RC, and Cargo workspace;
- the driver compiles for Debug and Release x64 (if the job succeeds);
- the INF produces a catalog;
- the PE image is x64 native;
- the Rust CLI formats, lints, tests, and builds.

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
- `subsystem (Windows native)`.

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
  moving images. WDK/SDK content is pinned; MSVC is not. Record the compiler
  version from the log.
- NuGet package hashes are not verified after restore. A future improvement is
  to pin checksums.
- A green YAML parse is not a green build. Only a run is evidence.

---

## 11. First push and first run

This repository was authored without Git installed. The following steps are for
a machine that has Git and a GitHub account:

```powershell
git init
git add .
git commit -m "Initial Milestone 0/1 foundation"
git branch -M main
git remote add origin https://github.com/<owner>/<repo>.git
git push -u origin main
```

Then:

1. Open **Actions** and select **CI**.
2. Wait for the `hygiene` job.
3. Inspect `driver` and `management`.
4. Open the **Driver build** logs and record the MSVC, WDK, SDK, and NuGet
   versions.
5. Download the artifacts only to a disposable VM.
6. Do not create a `v0.1.0` tag until CI is green and the version gate is
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
