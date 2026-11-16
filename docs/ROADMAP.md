# NBA Unrivaled Windows port milestones

Quick Play reaches the court. The title-menu handoff below records an earlier
stage of the recompilation. See [PERFORMANCE.md](PERFORMANCE.md) for profiling notes.

## 0. Reproducible inputs and tools

Prepared November 7, 2026 (America/Los_Angeles).

- Preserve the supplied ZIP; SHA-256 fingerprints are in `private/input-manifest.json`.
- Title ID: `584109D4`; media ID: `634F9F4D`; executable version: `0.0.1.3`.
- Executable timestamp reported by ReXGlue: October 26, 2009, 22:13:01 UTC.
- Extracted 498 files and 15 directories. Verified 45,072 file data blocks against their stored STFS SHA-1 hashes. This checks extracted content integrity, not publisher signatures or ownership.
- `default.xex` SHA-256: `93f1767b20bdf009bd091ecbf10e72197325ae7ce193c71b8ecbfaa0a4cde115`.
- Working native compiler and ReXGlue SDK: CMake configure, compilation, and link succeeded in RelWithDebInfo with provisional generated code.

## 1. Establish executable analysis

Ghidra's XEX loader was built successfully against Ghidra 12.2. A headless import without automatic analysis succeeded and saved `private/ghidra-projects/NBAUnrivaled.gpr`, including 6,060 function symbols and 198 import thunks. The loader also logged `out of input bytes` from its LZX decompressor before loading the PE sections. Its decompressor catches errors and returns partial output, so import success alone does not establish that the entire image was decoded correctly. Cross-check imported sections against an independent decoded image before trusting the disassembly. The original XEX passed extraction checks and ReXGlue loaded it successfully. Import diagnostics are in `private/ghidra-import-console.log`.

Strict ReXGlue analysis initially failed on these branches:

| Target | Source instruction |
| --- | --- |
| `0x82184CD0` | `0x82186594` |
| `0x8216EE10` | `0x8216EE1C` |
| `0x82184CD0` | `0x821845DC` |

The branch targets are now explicit entrypoints under `[entrypoint.functions]` in
`nbaunrivaled_manifest.toml`. ReXGlue 0.10.1's `FunctionConfig` treats these as
CONFIG functions; because neither has a hard-coded size, discovery determines
their natural boundaries. Do not replace these with empty stubs.

The prepared Ghidra import is not trustworthy for these addresses. XEXLoaderWV
logged `out of input bytes` during LZX decoding, and a fresh headless inspection
of the imported memory had no instruction at any of the three branch sites or
either target. By contrast, ReXGlue independently decoded the XEX and reported
all three `b` destinations. A strict codegen run (`--ignore-stamp`, without
`--force`) using a temporary manifest with the two targets explicitly declared
completed successfully and generated these target bodies:

| Entry | ReXGlue-decoded instructions | Interpretation |
| --- | --- | --- |
| `0x82184CD0` | `b 0x82184108` | One-instruction tail trampoline |
| `0x8216EE10` | `addi r3,r3,8`; `b 0x8216D6A8` | Adjust argument, then tail-call |

The source/target pairs are consistent with PowerPC relative branch encodings:
`0x82186594` to `0x82184CD0` has displacement `-0x18C4` (word `0x4BFFE73C`);
`0x8216EE1C` to `0x8216EE10` has displacement `-0xC` (word `0x4BFFFFF4`);
and `0x821845DC` to `0x82184CD0` has displacement `+0x6F4` (word
`0x480006F4`). These are the encodings implied by ReXGlue's decoded branch
addresses, not bytes recovered from the incomplete Ghidra import. The three
branches resolve as tail calls to the two generated entries. The permanent
manifest entries are in place, and strict codegen plus the fresh native build
now pass. Runtime progress and the current menu state are recorded in section 2.

Initial diagnostic files: `private/initial-codegen-console.log`, `private/initial-codegen.log`.
Provisional codegen used `--force` only to exercise the compiler toolchain. Its success does not resolve these errors. The normal build script always reruns strict analysis with `--ignore-stamp` so the provisional cache cannot hide them.

## 2. Reference run and first native boot

- A fresh RelWithDebInfo build completed after strict codegen passed. The first
  launch stayed black because the optional GPU plugin was not selected. The
  SDK's runtime option is `--gpu_plugin xenos`; CMake had staged
  `rexgpu-xenosrd.dll` beside the executable.
- Relaunching with the Xenos plugin initialized Direct3D 12 on the NVIDIA
  GeForce GTX 560M, input/audio, shader storage, and front-end content loading.
  It then stopped at `[FATAL] Call to invalid or unregistered function at
  guest address 0x82200EA8`. A strict temporary-manifest run added that
  address as a size-unspecified function and generated:

  ```text
  lwz r11,0(r3)
  lwz r10,0(r4)
  lwz r11,0(r11)
  lwz r10,0(r10)
  subf r3,r10,r11
  blr
  ```

  This is executable indirect-target code, not an empty stub, and ReXGlue
  strict codegen completed without `--force`. The entry is now in the
  permanent manifest. A fresh strict build passed; the next launch rendered
  the Tecmo splash logo before another fatal unregistered target,
  `0x8214C970`.
- The second target is the first instruction after the eight-instruction
  indirect dispatch at `0x8214C950`, and before the existing entry at
  `0x8214C990`. A strict temporary-manifest test generated this body:

  ```text
  lwz r3,332(r3)
  mr r5,r4
  li r6,1
  li r4,9
  lwz r11,0(r3)
  lwz r11,36(r11)
  mtctr r11
  bctr
  ```

  This is a real vtable-dispatch routine, not a stub; it is now an explicit
  manifest entry. The subsequent fresh strict build succeeded. With this
  target registered, the native run advanced beyond the Tecmo logo and
  displayed the game's title menu. Computer Use verified the 1920x1080
  rendered menu with `Quick Play` selected and these visible choices:
  `Xbox LIVE Game`, `Challenge Game`, `Leaderboards`, `Achievements`,
  `Help & Options`, and `Return to Game Library`. The bottom prompt reads
  `X UNLOCK FULL GAME`; this is observed title behavior, not a determination
  of licensing status. The runtime remained responsive and the current log
  contains no fatal entries.
- That boot also logged missing front-end `.dds` files, including
  `fe_playerstats_bgcylinders.dds`, `fe_teamselect_bgcylinders.dds`, and
  `fe_modaldialog_maincrystal.dds`, as well as VFX textures
  `fx_coloured_confetti.dds` and `fx_red_flaresmall.dds`. The front-end
  textures are not present in the 70-file extracted `FRONT_END` directory;
  they did not prevent title-menu rendering, though later effects are unknown.
  The runtime also repeatedly logs an absent `langoverride.txt`.
- Xenia Canary was opened, but no reference screen was captured: attempts to
  navigate its UI were interrupted by active user input.
- Run the extracted executable in the pinned Xenia build and record behavior, graphics, controls, frame pacing, audio, and save/load. Reference playability is untested.
- Establish whether the supplied XBLA content represents trial or licensed full-game behavior; extraction alone does not establish that. Preserve the supplied licensing state during baseline work.
- Strict branch analysis is resolved by the manifest entries above, and a
  fresh strict build with both runtime-discovered entries succeeded. Continue
  from the rendered title menu: verify menu navigation and Quick Play, then
  record the next screen or runtime failure. Do not assume a successful link
  establishes gameplay correctness.
- Inventory kernel/XAM imports and missing behavior; implement only the functionality supported by observed evidence.
- Keep game-dependent paths and user data under `private/` while developing.

## 3. Reach the menus and a complete match

Fix startup, threading, filesystem assumptions, timing, input, audio, and rendering in small documented steps. Use the SDK Xenos backend first; evaluate a custom native renderer and XenosRecomp only after establishing correct behavior. Test all teams, arenas, game modes, and local controllers.

## 4. Windows release work

Validate save persistence, installation from user-supplied content, deterministic input validation, controller reconnects, window modes, supported GPUs, performance, and error reporting. Package original project code and permitted dependencies with their licenses. Keep original game content and generated translations out of source-control and public release artifacts until distribution requirements have been reviewed.

## Tool selection

ReXGlue supplies the static recompiler plus a runtime derived from Xenia. Standalone XenonRecomp supplies CPU translation but requires a separate runtime. Ghidra supports human-led analysis and decompilation; recompilation output does not reconstruct the game's original C++ source. Xenia supplies a useful reference implementation; it is separate from the native port.

Sources:
- https://github.com/rexglue/rexglue-sdk
- https://github.com/rexglue/rexglue-sdk/releases/tag/v0.10.1
- https://github.com/hedge-dev/XenonRecomp
- https://github.com/hedge-dev/XenosRecomp
- https://github.com/NationalSecurityAgency/ghidra
- https://github.com/zeroKilo/XEXLoaderWV
- https://github.com/xenia-canary/xenia-canary-releases
