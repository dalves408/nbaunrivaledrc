# NBA Unrivaled Recompiled

An unofficial native Windows port of the Xbox 360 XBLA version of **NBA Unrivaled**, built through static PowerPC recompilation with ReXGlue.

<a href="https://recomplauncher.com"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

## Play 1.0

Run `NBAUnrivaled-Setup-1.0.0.exe` from the GitHub release and select your own Xbox 360 XBLA game files. Setup accepts a supported Xbox LIVE package, a ZIP containing it, or an extracted game folder. The default install folder is `C:\Games\NBA Unrivaled`. Run Setup again to update or repair an existing install. Save data, settings, and logs are under `%LOCALAPPDATA%\NBAUnrivaled`.

Use the `NBAUnrivaled-Recompiled-v1.0.0-Windows-x64.zip` release asset with RecompLauncher. It contains the Level 3 profile, importer, and Windows runtime files. RecompLauncher can prepare the game from your copy, edit the same settings as F1, and find the game logs for reports.

## Current progress

- Verified extraction of 498 game files and 45,072 STFS file-data blocks against their stored hashes.
- Resolved the original analysis branches and two runtime-discovered indirect targets using documented instruction evidence.
- Reached menus and Lakers-versus-Magic gameplay with the Xenos graphics backend.
- Added GPU polling backoff: observed process CPU use fell from 1.42 to 0.55 occupied logical cores in separate gameplay captures. GPU utilization remained about 96%; **an FPS gain is not established**.
- Added pinned controller mappings, explicit keyboard bindings, diagnostic capture tools, and opt-in Release guest Present timing.
- Added a candidate NVIDIA alpha-test workaround for crowd flicker; visual effectiveness is not yet confirmed.

See the [performance notes](docs/PERFORMANCE.md) and the [recompilation roadmap](docs/ROADMAP.md) for development context.

## Requirements

- Windows x64.
- Visual Studio 2022 C++ Build Tools with x64/x86 MSVC libraries and a Windows 10/11 SDK. The tested SDK version is 10.0.26100.0.
- Git, Python 3.12+, CMake 3.25+, and Ninja.
- Your own compatible game content. **No game archive, XEX, assets, generated translations, or playable binary is included in this repository.**

Project-local tools are restored from [config/tools.lock.json](config/tools.lock.json) with pinned versions and SHA-256 checksums. They include ReXGlue 0.10.1, LLVM 21.1.8, Ghidra/XEXLoaderWV, Java, Gradle, Xenia, controller mappings, PresentMon, and the isolated Python runtime needed by LLDB.

## Setup and build

Run PowerShell from the checkout root:

```powershell
# Initial tool download is approximately 1.3 GB.
.\scripts\Setup-Tools.ps1

# Supply your archive at the root using this exact filename:
# NBA Unrivaled (World) (XBLA).zip
python .\scripts\prepare_game.py

# Strict codegen and native build; use -Fresh on a new checkout or machine.
.\scripts\Build.ps1 -Configuration Release -Fresh
.\scripts\Launch-Game.ps1
```

The build script imports the Visual Studio developer environment and local LLVM/ReXGlue paths. Its default configuration is RelWithDebInfo; select **Release** explicitly for performance testing. The launcher defaults to Release and 720p. Build outputs are under `out/build/win-amd64-release/`.

The extraction wrapper supports this title's contiguous STFS layout; it is not a general package extractor. Preserve the original archive unchanged. The expected extracted `default.xex` SHA-256 is:

```text
93f1767b20bdf009bd091ecbf10e72197325ae7ce193c71b8ecbfaa0a4cde115
```

## Controls and launch options

```powershell
# Physical controller, with keyboard pad emulation disabled.
.\scripts\Launch-Game.ps1 -InputMode Controller

# Guest Present timing in Release.
.\scripts\Launch-Game.ps1 -FrameTimings

# Compare original polling or disable the candidate crowd workaround.
.\scripts\Launch-Game.ps1 -GpuPollBackoff:$false
.\scripts\Launch-Game.ps1 -CrowdFlickerFix:$false

# Experimental lower-bandwidth gamma targets; may affect visual accuracy.
.\scripts\Launch-Game.ps1 -AccurateGamma:$false
```

| Keyboard | Xbox 360 input |
| --- | --- |
| WASD or arrows | Left stick |
| Space / Backspace | A / B |
| L / P | X / Y |
| Enter / Tab | Start / Back |
| Q or I / E or O | Left / right trigger |
| 1 / 3 | Left / right shoulder |
| F / K | Left / right stick press |
| Numeric keypad 8, 2, 4, 6 | Right stick |
| Shift + arrows | D-pad |

**Settings.** Press F1 or hold **Back / View** and press **Start / Menu** on the controller. Use the arrow keys, WASD, or the D-pad to navigate. Closing Settings after a restart-only change offers **Continue playing** or **Restart now**. Esc opens the quit prompt, and F4 opens the ReXGlue settings overlay.

**Settings program:** build `config.exe` (`nbaunrivaled_config`) for Video + **Controls** (keymapper) + About. `Launch-Game.ps1` reads that INI and overrides matching launcher defaults, including sticky `keybind_*` values.

**Installer:** see [installer/Installer.md](installer/Installer.md). Inno Setup wizard (Hitz-style UX, **no texture pack**) imports a player-supplied ZIP or extracted folder via `nba_import.exe` into `{app}\game`.

Consult the game's controls screen for offense/defense action meanings. Reconnects, multiplayer assignment, and comprehensive input behavior remain untested. The SDK matches modifiers exactly, so extra Shift/Ctrl/Alt keys can suppress ordinary bindings.

## Diagnostics

```powershell
.\scripts\Build.ps1 -Configuration RelWithDebInfo
.\scripts\Launch-Game.ps1 -Configuration RelWithDebInfo -Diagnostics

# Select a gameplay-only range after capturing.
python .\scripts\summarize_perf.py private\perf-TIMESTAMP.csv --start-frame 1000 --frames 300

# Replace PROCESS_ID with the running game's process ID.
.\scripts\Sample-Performance.ps1 -GameProcessId PROCESS_ID -Samples 15 -OutputPath private\usage.json

# Optional reverse-engineering tools.
.\scripts\Build-GhidraLoader.ps1
.\scripts\Launch-Ghidra.ps1
```

Release disables SDK profiling counters; use `-FrameTimings` for guest Present timing or RelWithDebInfo for SDK counter CSVs. Guest Present timing is not displayed-frame FPS. PresentMon ETW capture requires elevated rights or Performance Log Users membership. Avoid builds and screenshots during comparative captures.

The prepared local Ghidra import logged an LZX decoding error and is not a trusted source for all instructions. Read the roadmap before relying on it or adding overrides. Make lasting fixes in source, manifest entries, or documented SDK patches; code generation replaces translated C++.

## Repository layout

| Path | Purpose |
| --- | --- |
| `src/` | Host application, polling optimization, timing hooks |
| `nbaunrivaled_manifest.toml` | Recompilation inputs and analysis overrides |
| `CMakeLists.txt`, `CMakePresets.json` | Native build configuration |
| `generated/rexglue.cmake` | Tracked SDK integration boilerplate |
| `config/tools.lock.json` | Pinned tool downloads and source revisions |
| `scripts/` | Setup, extraction, build, launch, diagnostics |
| `docs/` | Performance findings and recompilation notes |
| `private/`, `tools/`, `out/` | Ignored local data, tools, diagnostics, outputs |

The checkout contains the port source, build scripts, and installer resources. Setup patches the menus in the game files you supply.

## Tested configuration

Quick Play reached 60 guest Presents per second in a 32-second Lakers vs. Magic capture on an RTX 3070 with Medium (2×) internal resolution and 2× MSAA. This test covered one arena. Full-match stability, controller reconnects, and the minimum hardware configuration are still unverified.

## Upstream projects

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk)
- [Xenia Canary](https://github.com/xenia-canary/xenia-canary-releases)
- [Ghidra](https://github.com/NationalSecurityAgency/ghidra)
- [XEXLoaderWV](https://github.com/zeroKilo/XEXLoaderWV)
- [SDL GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB)
- [PresentMon](https://github.com/GameTechDev/PresentMon)

Upstream tools retain their respective licenses. This project is not affiliated with or endorsed by the original game's publishers, developers, the NBA, Microsoft, or Xbox.
