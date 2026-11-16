# NBA Unrivaled installer

Windows Setup is modeled on NHL Hitz Pro’s Inno wizard (welcome → folder →
source → ready → copy), with two deliberate differences:

1. **No texture-pack step or task.** This title has no hi-res pack option.
2. **ZIP / LIVE package / extracted folder instead of an Xbox ISO.** Point Setup
   at either the Xbox LIVE package tree (`…\584109D4\000D0000\FB7DFF…`, same as
   `private/package`) or an already-extracted tree (`default.xex`, … —
   `private/game`), or a ZIP of either. `nba_import` STFS-extracts the
   LIVE content only after the inner file matches a supported name, title ID,
   and SHA-256. The outer ZIP's filename and checksum do not affect acceptance.

## Build

Ship **both** ISA builds into Setup; install time picks the primary
`nbaunrivaled.exe` and also installs the unused ISA under a renamed filename.

```powershell
.\scripts\Enter-DevShell.ps1
# Generate translations from your prepared game files before the first configure.
rexglue codegen nbaunrivaled_manifest.toml

# Portable (SSE4.1 floor) -> out\build\win-amd64-release-portable\nbaunrivaled.exe
cmake --preset win-amd64-release-portable
cmake --build --preset win-amd64-release-portable --parallel 4

# AVX / Zen 1 (NBA_MARCH_ZNVER1=ON) -> out\build\win-amd64-release\nbaunrivaled.exe
# plus config.exe, nba_import.exe, rexruntime.dll, rexgpu-xenos.dll
.\scripts\Build.ps1 -Configuration Release

# Bundle Microsoft VC++ Redistributable x64 (from local VS, else aka.ms)
powershell -File installer\Prepare-Redist.ps1

& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" `
  /DBuildDir=..\out\build\win-amd64-release `
  /DPortableBuildDir=..\out\build\win-amd64-release-portable `
  /DAvxBuildDir=..\out\build\win-amd64-release `
  installer\nba_unrivaled.iss
```

Setup runs `Apply-TitleMenuPatches.ps1` at install and repair. The script
checks the hashes of the supplied title menus and applies
`scripts/title-menu-patches.json` to disable Xbox LIVE in the offline port.

Setup is **non-elevated by default** (`PrivilegesRequired=lowest`) so the wizard
does not UAC for installs under `C:\Games\NBA Unrivaled` or other user-writable
dirs. If `vcruntime140.dll` / `msvcp140.dll` are missing under System32, Setup
explains that the Visual C++ runtime is required, then launches the bundled
`vc_redist.x64.exe` with a one-shot `runas` elevation (`/passive` interactively,
`/quiet` under `/VERYSILENT`). The rest of the wizard stays non-admin.

Setup calls `IsProcessorFeaturePresent(39)` (PF_AVX):

| CPU | Primary (`nbaunrivaled.exe`) | Also installed |
| --- | --- | --- |
| AVX usable | AVX / znver1 build | `nbaunrivaled-NonAVX.exe` (portable) |
| No AVX | portable (SSE4.1) | `nbaunrivaled-AVX.exe` (znver1) |

Shortcuts target the primary only. No runtime stub.

Output: `installer\Output\NBAUnrivaled-Setup-<version><suffix>.exe`
(example: `NBAUnrivaled-Setup-1.0.0.exe`).

Regenerate the packaging checklist after a new `prepare_game.py` run:

```powershell
python installer\gen_checklist.py
```

## Validation checklist

`nba_import` requires:

| Check | Detail |
| --- | --- |
| LIVE content (if not already extracted) | File named `FB7DFF22202FF66CE4C264666214182560F1143058` under `584109D4\000D0000\` (or recursive find), LIVE/PIRS/CON magic, title `584109D4`, and a supported inner-file SHA-256 before STFS extraction |
| `default.xex` | Magic `XEX2`, size `1335296`, SHA-256 `93f1767b…cde115` |
| Marker files | `data.zip`, `data.idx`, `ArcadeInfo.xml`, `game_icon.png`, `achievement01.png`, `AUDIO/CROWD_LOOP1_V1.OGG` (exact sizes from `private/input-manifest.json`) |
| File count | At least half of the ~498 extracted files must copy |

The supplied ZIPs contain two verified variants of the same LIVE payload. Their
STFS game data is identical. Setup accepts either of these inner-file SHA-256 values.

| Supplied ZIP | Inner LIVE payload SHA-256 |
| --- | --- |
| `NBA Unrivaled (World) (XBLA).zip` | `1e210568ad0d4b22290b556cfb8ef326221ade3aac107fc596e658390814d132` |
| `NBAUnrivaled.zip` | `2b705d271b1889672b5f5d175be86025799b9a74acde80b7aabb516a60c30b98` |

Exit codes match the Hitz importer: `2` missing source, `3` not usable ZIP/folder, `4` wrong package hash / `default.xex`, `5` incomplete, `6` write failure. `--prepare` writes to a staging folder then swaps in (safe for repairs).

## Layout after install

```
C:\Games\NBA Unrivaled\
  nbaunrivaled.exe           # primary (portable or AVX, chosen at Setup)
  nbaunrivaled-AVX.exe       # present when primary is portable
  nbaunrivaled-NonAVX.exe    # present when primary is AVX
  config.exe
  rexruntime.dll
  rexgpu-xenos.dll
  Launch.cmd                 # optional helper; same args as Start Menu shortcut
  game\                      # imported content
```

Start Menu / desktop shortcuts and the optional post-install **Launch** action
target `{app}\Launch.cmd`, which passes `--game_data_root="{app}\game"` plus
standalone runtime defaults. The app also defaults `game_data_root` to `{exe}\game`
when that folder contains `default.xex`, so Explorer double-click works too.
Settings stay under `%LOCALAPPDATA%\NBAUnrivaled\` (`[display]` +
`[keybinds]`), same as F1 / config.exe / `Launch-Game.ps1`.

## Update / repair

Setup defaults to `C:\Games\NBA Unrivaled` on first install and remembers the
folder of a registered install. Setup checks the `game` subfolder under the
destination you select. It can reuse files even if the destination has no
uninstall entry.

| Situation | UX | ZIP / game-dir page |
| --- | --- | --- |
| Uninstall entry + same `DisplayVersion` + valid `{app}\game` markers | **Repair** | Skipped |
| Uninstall entry + older version + valid markers | **Update** | Skipped |
| Uninstall entry + missing/corrupt `{app}\game` | **Repair** (re-import) | Shown (LIVE preferred) |
| No uninstall entry, but valid game already under selected dir | **Repair** using existing content | Skipped |

Setup checks the supported `default.xex` hash, the sizes in the import checklist,
and the `FRONT_END` directory. A repair reuses the existing game folder and
refreshes the title-menu patch. Incomplete or unsupported files require a
supported source for reimport.

## Icon

Xbox marketplace motion-ball mark (`resources/nba_unrivaled.ico`, option-02)
is embedded in `nbaunrivaled.exe` and `config.exe`, used as `SetupIconFile`, and
referenced by Start Menu / desktop shortcuts via `IconFilename`.

## Silent source

`Setup.exe /SOURCE="D:\path\to\game-or.zip"` pre-fills the source page.

## RecompLauncher SDK

Setup installs `recomplauncher.json` beside `nbaunrivaled.exe`. Use that folder
for a local build in RecompLauncher Developer Tools. The manifest defines
launch, settings, verified copies, and the Level 3 prepare command. The
prepare command can import either supported ZIP, the inner LIVE file, or a
supported extracted folder. It uses the installed importer and menu patch,
then marks the game as ready. A standalone Setup install writes the same
marker after patching, so the launcher can start an already prepared copy.

The seven video settings use `%LOCALAPPDATA%\NBAUnrivaled\settings.ini` in
RecompLauncher, the F1 overlay, and `config.exe`. A clean profile opens in
borderless fullscreen. Existing display settings carry over. Launching with
`RECOMPLAUNCHER_MODE=bigpicture` forces borderless fullscreen for that session
while preserving the saved display preference for the next desktop launch.

Hold **Back / View** and press **Start / Menu** to open the same settings menu
as F1. Use the D-pad to navigate, A to confirm, and B to close. Select **Quit
game** and confirm to exit with the controller.

When you close Settings after changing a setting that needs a restart,
**Continue playing** saves the change for your next launch. **Restart now**
ends the current match and reopens the game with the saved settings.

The game writes logs to `%LOCALAPPDATA%\NBAUnrivaled\logs`. RecompLauncher
can open that folder from the settings page and look there for a log when a
player submits a crash report. Keep the manifest with the distributed build.
See the [RecompLauncher SDK guide](https://recomplauncher.com/sdk-files/guide.md)
for local testing and catalog setup.

The startup log identifies the build version, Git revision, configure time and
CPU variant. Each file log line includes a short build ID. Set the same
`-DNBA_BUILD_VERSION=...` label on both CMake builds when packaging an installer.
Errors flush immediately, and other messages flush once a second. At launch,
the SDK prunes older default logs toward a 32 MiB directory budget. Frame timing
capture remains opt-in.
