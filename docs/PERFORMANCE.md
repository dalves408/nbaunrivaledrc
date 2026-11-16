# Performance investigation

## Reproduced bottlenecks

Release, Lakers versus Magic, GTX 560M / i7-2860QM, 720p, Xenos RTV,
accurate gamma and the candidate alpha workaround enabled.

The clean baseline (`private/usage-gameplay-clean.json`) sampled 15 times over
21.340 seconds without screenshots, a debugger, or builds during capture:

| Metric | Baseline | GPU-poll backoff |
| --- | ---: | ---: |
| Occupied logical CPU cores, sample mean | 1.422 | 0.555 |
| 3D GPU utilization, sample mean | 95.33% | 96.20% |
| Sample duration | 21.340 s | 21.268 s |

The second capture is `private/usage-gameplay-backoff.json`. These are separate
gameplay runs in the same matchup, not deterministic frame-for-frame replays.
Observed process CPU use fell about 61%. This is not a measured FPS gain.
An earlier transition/court capture used about 354 MiB of dedicated GPU memory;
the evidence does not point to exhausting the GPU's 1.5 GiB capacity.
`usage-gameplay-baseline.json` overlapped debugger attachment and is excluded
from performance comparisons.

## CPU evidence and change

Forty debugger samples of three threads are saved in
`private/stack-samples.json`. The busiest guest worker had 15/40 top PCs in
`0x822341B0`, 8/40 in `0x8223AA18`, 4/40 in `0x82227120`, and another 9/40
in register-save/restore helpers. Host PCs were matched against this exact
executable's `PPCFuncMappings` table, then checked against generated bodies.

`0x8223AA18` waits for space in the GPU ring. It repeatedly calls
`0x822341B0`, which checks GPU progress and timeout/recovery state. Its Xbox
delay instructions (`db16cyc`) emit no host operation. Thus a stalled ring
leaves a host thread repeatedly saving/restoring registers and checking the
same pointer at full CPU utilization. The main guest thread was usually
sleeping. The GPU command thread was waiting on a D3D12 submission fence in
27/40 samples; its sampled return address is RVA `0xAEC71` in this SDK DLL.

`src/gpu_poll_backoff.cpp` overrides the public weak helper entry with a
native keep-waiting path for `0x822341B0` (empty `db16cyc` + PPC save/restore
removed on the common path). Timeout still calls the complete original
`__imp__sub_822341B0` so recovery stays intact. Optional
`nba_gpu_poll_backoff` (default **false**) may yield after a long
unchanged streak; it never `Sleep`s. The tip-off Present CSV shows the
`0x8223AA18` ring wait is ~80% of Present; guest `--vsync=false` (launcher
default) clears the remaining pad over 16.7 ms without quality-setting cuts.

Launch with `-GpuPollBackoff:$true` or `-Vsync:$true` to compare. Gameplay
continued through possession and tip-off with the hooks. No full-match or
every-mode validation is claimed.

## Vector and shader findings

- Release already uses `-O3 -msse4.1`. A compiled generated-code object
  (`nbaunrivaled_recomp.0.cpp.obj`) contains native `pshufb`, `shufps`, and
  `blendps` instructions. Portable SIMDe source spelling does not mean these
  operations are scalar-emulated on this PC.
- The sampled hot CPU path is GPU-progress polling rather than a vector-math
  kernel. Some software floating-point library calls appeared, but there is
  no measured basis for changing guest floating-point semantics or enabling
  unsupported AVX2/FMA instructions on this Sandy Bridge CPU.
- Shader compilation workers were idle in the initial snapshot. Warm gameplay
  is dominated by GPU execution/fence waiting, not proven sustained shader
  compilation. Cold shader creation may still cause transient stalls.
- This hardware uses RTV because it lacks rasterizer-ordered views. Do not
  force ROV. Do not disable `native_2x_msaa` as a presumed quality reduction:
  the pinned backend may emulate 2x with more expensive 4x MSAA when it is off.
- GPU memexport readback and page coherency remain enabled. Disabling them
  without title-specific evidence could introduce the glitches being fixed.

## Further measurement and GPU experiment

`scripts/Sample-Performance.ps1` records process CPU, per-thread CPU deltas,
Windows per-process GPU engine counters, and GPU memory. CPU 1.0 means one
occupied logical processor; GPU engines are reported separately, not summed.

PresentMon 2.6.0 is pinned with its license and verified against the official
release asset's SHA-256. Windows denied starting its ETW session: administrative
privileges or membership in Performance Log Users is required. No system group
or permission changes were made.

`-FrameTimings` provides an alternative in Release: it records completions of
the original guest D3D Present routine `0x82237030`, which contains the only
`VdSwap` call site. This measures guest Present calls, not displayed frames or
GPU execution duration. Recording is opt-in and incurs a small file-write cost.
The CSV uses `frame_time_us`, so `scripts/summarize_perf.py` can select and
summarize a gameplay-only range.

The final Release startup log `private/game-20261107-235947-109.log` confirms
backoff and accurate gamma are enabled. `private/present-20261107-235947-109.csv`
is successfully populated by the Release hook. A 120-row startup window parsed
successfully; its timing is not reported as gameplay FPS. Both Release and
RelWithDebInfo compile and link with the hooks. The mapped wait-helper host
entry changed to the project hook, verified against `PPCFuncMappings` and native
disassembly. Original input content remains untouched.

`-AccurateGamma:$false` is an opt-in GPU bandwidth experiment. It replaces
64-bit gamma render-target storage with the SDK's 32-bit path and uses a
separate cache. It can change color/blending accuracy; it is not the default
and has no established speedup until measured against the same scene.

```powershell
./scripts/Launch-Game.ps1 -FrameTimings
./scripts/Sample-Performance.ps1 -GameProcessId PROCESS_ID -Samples 15 -OutputPath private/usage.json
./scripts/Launch-Game.ps1 -FrameTimings -AccurateGamma:$false
```

Next priorities are measured render-target bandwidth/MSAA cost, expensive crowd
passes and overdraw, and GPU upload/submission efficiency. A per-draw GPU capture
and timestamp breakdown are still needed before modifying individual shaders.
