# Gaming compatibility

Status: **not tested. No game has been run against this driver.**

This document defines what "gaming compatibility" will mean, what is likely to
work, and what cannot be promised. It contains no performance numbers and no
per-title claims, because none have been measured.

---

## 1. The honest starting point

The driver at Milestone 1 mounts nothing. There is no read path, no write path,
no cache integration, and no drive letter. Any statement about games running on
an EXT4 volume through STGR EXT4 is currently hypothetical.

Even after read support lands, most game libraries need **write** access:

- Steam updates game files, manifests, and library metadata.
- Launchers write logs, saves, configuration, and crash reports.
- Anti-cheat and DRM components may create or modify files.
- Shader caches are written continuously.

A read-only mount is useful for inspection, recovery, copying, and verifying
hashes. It is not a playable Steam library for most titles.

---

## 2. What users are likely to attempt

| Scenario | Reality |
| --- | --- |
| Add an existing EXT4 game library to Windows Steam | Steam must be able to write; read-only mount is insufficient |
| Run a Windows game from an EXT4 volume | Requires a working write path for most titles, plus correct Windows semantics |
| Use a Proton prefix stored on EXT4 from Windows | Proton is a Linux runtime; running it from Windows is not a supported configuration |
| Dual-boot Linux and Windows sharing a game library | Possible in principle, but concurrent access and compatibility are the user's responsibility and are not safe |
| Use an external EXT4 SSD for games | Same requirements as an internal volume; USB removal adds surprise-removal risk |

The project does not recommend sharing a live library between Linux and Windows.
It may work for static files, but Steam and games update metadata continuously.

---

## 3. Technical requirements for games

Games stress a file system in ways office applications do not:

| Requirement | Why it matters | Status |
| --- | --- | --- |
| Memory-mapped I/O | Asset streaming, texture loading | Not implemented |
| Asynchronous I/O | Overlapped reads, streaming | Not implemented |
| Large files | Game archives, videos | Not implemented |
| Sparse files | Some installers and caches | Not implemented |
| Correct file locking | Launchers and anti-cheat | Not implemented |
| Oplocks | Sharing and cache coherency | Not implemented |
| Case-sensitive names | Some engines ship case-distinct assets | Not implemented; collision policy pending |
| Symlinks | Some installers and tools | Not implemented |
| Long paths | Deep install directories | Not implemented |
| Fast directory enumeration | Large asset trees | Not implemented |
| Correct timestamps | Save games, installers, DRM | Not implemented |
| Flush and fsync semantics | Saves and anti-cheat state | Not implemented |

A game that only reads a few files may work early. A game that writes saves,
streams assets, and uses anti-cheat will need most of the list above.

---

## 4. Steam specifics

Steam libraries have a `steamapps` directory containing:

- `libraryfolders.vdf` (library configuration);
- `appmanifest_<appid>.acf` (installed app metadata);
- game directories;
- `downloading` and `temp` directories;
- shader cache and compatibility data.

Steam expects to create, rename, and delete these files. It also uses file locks
and may verify content by reading large files. A read-only volume will fail
library discovery or updates in ways that look like corruption to the user.

Planned validation:

1. Create an EXT4 image with a real Steam library copied by Linux tooling.
2. Mount read-only; verify every file hash against the Linux manifest.
3. Mount read-write on a disposable image; let Steam discover the library.
4. Run a content validation.
5. Install, update, and uninstall a small game.
6. Verify with `e2fsck -fn` and content hashes after each step.

None of this has been done.

---

## 5. Proton and compatibility layers

Proton is a Wine-based compatibility layer that runs Windows games on Linux. It
is not a Windows component. A Proton prefix is a Wine filesystem tree, often on
the same EXT4 volume as the game.

Running Proton itself from Windows is not a supported use case. Running a
Windows game directly from an EXT4 volume is the relevant scenario, and it
depends on the native Windows driver, not Proton.

If a user shares a Proton prefix between Linux and Windows, the project cannot
guarantee that Wine's assumptions, case sensitivity, symlinks, and file modes
survive the round trip. This is documented as unsupported, not silently
accepted.

---

## 6. Anti-cheat and DRM

Anti-cheat systems are kernel-level or user-level security products. They may:

- inspect file system paths and volume types;
- require specific Windows APIs and semantics;
- detect unusual drivers or unsigned kernel components;
- refuse to run on a volume they do not recognize;
- flag a system with test signing enabled.

The project cannot promise compatibility with any anti-cheat product. A
per-title assessment is required, and some titles may never work. The driver
must never attempt to hide from anti-cheat or bypass a security check.

Test policy:

- test only on a disposable machine with a legitimate game license;
- record the anti-cheat version and the exact result;
- do not publish bypass instructions;
- report a title as unsupported if it refuses the volume, without calling the
  anti-cheat "wrong".

---

## 7. Case sensitivity and names

Many Windows games assume case-insensitive paths. EXT4 is case-sensitive.

Problems:

- two assets differing only by case collide on Windows;
- a game that opens `Textures/Foo.dds` when the file is `textures/foo.dds`
  works on Windows/NTFS but fails on a case-sensitive mapping;
- installers may create names Windows cannot represent.

Policy:

- detect case collisions at mount;
- refuse a write mount when a lossless mapping is impossible;
- preserve bytes and provide a documented escape for unrepresentable names;
- never silently rename or hide an asset;
- test real game trees for collisions before claiming compatibility.

A per-directory case-sensitive mode is a possible future feature, but it is not
part of the initial design.

---

## 8. Performance

No benchmark has been run. The project will not publish numbers until:

- the read path is complete and stable;
- the cache manager and fast I/O paths are integrated;
- the same hardware, game, and settings are used for NTFS and EXT4;
- results include variance and are reproducible;
- the test method and scripts are published.

Useful measurements:

- game install time;
- level load time;
- asset streaming stalls;
- save/load latency;
- CPU time in the driver;
- I/O size and queue depth;
- memory usage;
- frame-time spikes during streaming.

A single average FPS number is not a useful compatibility claim.

---

## 9. Test matrix (planned)

| Dimension | Values |
| --- | --- |
| Windows | Windows 11 x64, current supported build |
| Storage | SATA SSD, NVMe, USB SSD |
| EXT4 block size | 1 KiB, 4 KiB |
| Features | Target read set; write set after Milestone 6 |
| Games | A small set of representative titles, documented by name and version |
| Launchers | Steam, at minimum |
| Baseline | Same title on NTFS, same hardware |
| Failure injection | Surprise removal, power loss, disk full |

Per-title record:

```text
Title:
Version / build:
Launcher:
Anti-cheat:
Volume: UUID, block size, features
Mode: read-only / read-write
Result: works / works with limitations / refused / crashed / corrupted
Evidence: logs, screenshots, hashes, e2fsck output
Notes:
```

A title is not "supported" because it launched once. It is supported when the
recorded matrix passes and the limitations are documented.

---

## 10. What will not be claimed

- "Runs all Steam games."
- "Faster than NTFS."
- "Anti-cheat compatible."
- "Safe to share with Linux."
- "No performance impact."
- "Works with every game engine."
- "Read-only is enough for gaming."

These claims require evidence the project does not have.

---

## 11. Current status

| Item | Status |
| --- | --- |
| Mount | Not implemented |
| Read | Not implemented |
| Write | Not implemented |
| Steam test | Not run |
| Game test | Not run |
| Anti-cheat test | Not run |
| Benchmark | Not run |

The only accurate statement is: **gaming compatibility is unmeasured.**
