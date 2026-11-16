[CmdletBinding()]
param(
    [ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
    [string]$Configuration = 'Release',
    [ValidateSet('Keyboard', 'Controller')]
    [string]$InputMode = 'Keyboard',
    [ValidateSet('720p', '1080p')]
    [string]$Resolution = '720p',
    [bool]$CrowdFlickerFix = $true,
    # Pause/SwitchToThread only (no Sleep). Default off: Present is
    # guest-thread bound and a yielded poll misses the 16.7 ms slot.
    [bool]$GpuPollBackoff = $false,
    [bool]$AccurateGamma = $true,
    # Guest GPU vsync paces Present ring-wait. At tip-off that wait is
    # ~80% of Present (~14 ms); disabling guest vsync clears locked-60 without
    # cutting MSAA/gamma/crowd. Host may tear; pass -Vsync:$true to restore.
    [bool]$Vsync = $false,
    # Guest VdQueryVideoMode refresh. 120 avoids 60 Hz 16/33 ms Present doubles
    # at tip-off without the quality cuts; 60 restores stock pacing.
    [ValidateRange(30, 240)]
    [int]$GuestRefreshRate = 120,
    # XBLA World package boots as trial unless full license bits are forced.
    [bool]$ForceFullLicense = $true,
    # If the adapter supports ROV, explicit path keeps EDRAM HLE off the RTV
    # ownership-transfer path. Use 'rtv' on GPUs without ROV (e.g. 560M).
    [ValidateSet('auto', 'rov', 'rtv')]
    [string]$RenderTargetPath = 'rov',
    # Fast memexport readback + earlier D3D12 submits cut Present ring-wait
    # stalls without changing draw quality settings.
    [bool]$FastMemexport = $true,
    [bool]$SubmitOnPrimaryEnd = $true,
    [ValidateSet('Normal', 'AboveNormal', 'High')]
    [string]$ProcessPriority = 'High',
    # Host pacer for guest Present (high-res waitable timer). 0 disables.
    [ValidateRange(0, 240)]
    [int]$FrameLimit = 60,
    # Adaptive fallback cap (e.g. 30) used while frames keep missing FrameLimit.
    [ValidateRange(0, 240)]
    [int]$FrameLimitLow = 0,
    # Weak-charger / power-capped preset (see docs/perf-notes.md).
    [switch]$LowPower,
    # Test-build FPS / frame-time / CPU% / GPU% overlay (F3 toggles in game).
    [switch]$PerfHud,
    [switch]$FrameTimings,
    [switch]$Diagnostics,
    [switch]$DryRun,
    # Extra SDK cvars appended after launcher defaults (e.g. '--vsync false').
    [string[]]$ExtraArgs = @()
)
$ErrorActionPreference = 'Stop'
if ($Diagnostics -and $Configuration -eq 'Release') {
    throw 'The pinned Release SDK disables performance counters. Use -Configuration RelWithDebInfo -Diagnostics to capture CSV counters.'
}
$projectRoot = Split-Path $PSScriptRoot -Parent
$buildRoot = Join-Path $projectRoot ('out/build/win-amd64-' + $Configuration.ToLowerInvariant())
$executable = Join-Path $buildRoot 'nbaunrivaled.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Build this configuration first: ./scripts/Build.ps1 -Configuration $Configuration"
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$cacheDirectory = if ($CrowdFlickerFix) { 'native-cache-crowd-alpha' } else { 'native-cache-mnk' }
if (-not $AccurateGamma) { $cacheDirectory += '-gamma8' }
if ($RenderTargetPath -eq 'rov') { $cacheDirectory += '-rov' }
elseif ($RenderTargetPath -eq 'rtv') { $cacheDirectory += '-rtv' }
$mappings = Join-Path $projectRoot 'tools/controller-mappings/gamecontrollerdb.txt'
if (-not (Test-Path -LiteralPath $mappings)) {
    throw 'Missing controller mappings. Run ./scripts/Setup-Tools.ps1 first.'
}
$gameRoot = Join-Path $projectRoot 'private/game'
if (-not (Test-Path -LiteralPath $gameRoot)) {
    throw "Missing extracted game data at $gameRoot"
}
# Title-menu: copy patched FE_MAINMENU*.XUR (LIVE Enabled=false, Challenge
# DefaultFocus/top-stop, Quit Game) + locale XUS. live_menu.cpp sign-in hook
# is defense-in-depth; do not drop EN-EN XUS (crashes XUI loader).
$applyTitleMenu = Join-Path $PSScriptRoot 'Apply-TitleMenuPatches.ps1'
if (Test-Path -LiteralPath $applyTitleMenu) {
    & $applyTitleMenu -GameRoot $gameRoot
}
# Title probes d:\langoverride.txt every Present; a missing file logs a kernel
# warning on the guest thread and adds measurable host I/O to the 18 ms floor.
# Empty file avoids the miss path; InstallLangOverrideLogFilter one-shots any residual.
$langOverride = Join-Path $gameRoot 'langoverride.txt'
if (-not (Test-Path -LiteralPath $langOverride)) {
    New-Item -ItemType File -Force -Path $langOverride | Out-Null
}
# Persist roots so license/unlock and metadata survive relaunches.
foreach ($rootName in @('native-user-xenos', 'native-metadata-xenos', $cacheDirectory)) {
    $rootPath = Join-Path $projectRoot ("private/" + $rootName)
    if (-not (Test-Path -LiteralPath $rootPath)) {
        New-Item -ItemType Directory -Force -Path $rootPath | Out-Null
    }
}
$arguments = @(
    '--game_data_root', "`"$projectRoot/private/game`"",
    '--user_data_root', "`"$projectRoot/private/native-user-xenos`"",
    '--cache_root', "`"$projectRoot/private/$cacheDirectory`"",
    '--metadata_root', "`"$projectRoot/private/native-metadata-xenos`"",
    '--gpu_plugin', 'xenos', '--resolution', $Resolution,
    '--hid_mappings_file', "`"$mappings`"",
    '--use_fuzzy_alpha_epsilon', $CrowdFlickerFix.ToString().ToLowerInvariant(),
    # Equals-form bools: space-separated "false" was observed to stick as true
    # for title cvars via GetFlagByName / CLI parse.
    "--nba_gpu_poll_backoff=$($GpuPollBackoff.ToString().ToLowerInvariant())",
    '--gamma_render_target_as_unorm16', $AccurateGamma.ToString().ToLowerInvariant(),
    '--video_mode_refresh_rate', "$GuestRefreshRate",
    "--nba_force_full_license=$($ForceFullLicense.ToString().ToLowerInvariant())",
    # Offline ship: keep Xbox LIVE Game faded + skipped by title up/down nav.
    '--nba_disable_live_menu=true',
    # Belt-and-suspenders for SDK content queries (hook is authoritative).
    '--license_mask=0xFFFFFFFF',
    "--readback_memexport_fast=$($FastMemexport.ToString().ToLowerInvariant())",
    "--d3d12_submit_on_primary_buffer_end=$($SubmitOnPrimaryEnd.ToString().ToLowerInvariant())",
    "--vsync=$($Vsync.ToString().ToLowerInvariant())",
    # Equals-form: space-separated "false" sticks true (same lesson as
    # nba_gpu_poll_backoff / vsync). Stuck mnk_mouse=true makes HardFocus
    # clicks fight keyboard A confirms on title/menus.
    "--mnk_mode=$(($InputMode -eq 'Keyboard').ToString().ToLowerInvariant())",
    '--mnk_mouse=false',
    '--mnk_passthrough=false',
    "--nba_frame_limit=$FrameLimit",
    "--nba_frame_limit_low=$FrameLimitLow",
    "--nba_perf_hud=$(([bool]$PerfHud).ToString().ToLowerInvariant())",
    '--log_file', "`"$projectRoot/private/game-$stamp.log`""
)
if (-not $Vsync) {
    $arguments += @('--d3d12_allow_variable_refresh_rate_and_tearing=true')
}
if ($RenderTargetPath -ne 'auto') {
    $arguments += @('--render_target_path_d3d12', $RenderTargetPath)
}
if ($InputMode -eq 'Keyboard') {
    # Arrows and WASD drive the left stick. Keep the familiar SDK
    # action buttons, and move right-stick input to the numeric keypad.
    #
    # Title "Start Game" is A-confirm (Space), NOT GAMEPAD_START. On-court
    # pause reads Start. Prefer equals-form (same sticky-bool lesson) and keep
    # SDK letter-X plus digit-2: Return/Enter is easy for host/ImGui to
    # swallow; face-X is L so letter-X is free.
    #
    # Comma binds tighter than + in PowerShell: @('--'+$name, $val) packs into
    # one argv and breaks ExtraArgs first-wins filtering. Emit equals tokens.
    $bindings = [ordered]@{
        keybind_a = 'Space,Semicolon'; keybind_b = 'Backspace,Quote'
        keybind_x = 'L'; keybind_y = 'P'
        keybind_start = '2,X,Return'; keybind_back = 'Z,Tab'
        keybind_left_trigger = 'Q,I'; keybind_right_trigger = 'E,O'
        keybind_left_shoulder = '1'; keybind_right_shoulder = '3'
        keybind_lstick_up = 'W,Up'; keybind_lstick_down = 'S,Down'
        keybind_lstick_left = 'A,Left'; keybind_lstick_right = 'D,Right'
        keybind_lstick_press = 'F'; keybind_rstick_press = 'K'
        keybind_dpad_up = 'Shift+Up'; keybind_dpad_down = 'Shift+Down'
        keybind_dpad_left = 'Shift+Left'; keybind_dpad_right = 'Shift+Right'
        keybind_rstick_up = 'Numpad8'; keybind_rstick_down = 'Numpad2'
        keybind_rstick_left = 'Numpad4'; keybind_rstick_right = 'Numpad6'
    }
    foreach ($binding in $bindings.GetEnumerator()) {
        $arguments += @("--$($binding.Key)=$($binding.Value)")
    }
}
if ($Diagnostics) {
    $arguments += @('--perf_log_csv', "`"$projectRoot/private/perf-$stamp.csv`"")
}
if ($FrameTimings) {
    $arguments += @('--nba_frame_log', "`"$projectRoot/private/present-$stamp.csv`"")
}
# Optional player settings from config.exe / F1 overlay.
# %LOCALAPPDATA%\NBAUnrivaled\settings.ini [display] + [keybinds] → CLI overrides.
$settingsIni = Join-Path $env:LOCALAPPDATA 'NBAUnrivaled\settings.ini'
$settingsArgs = [System.Collections.Generic.List[string]]::new()
if (Test-Path -LiteralPath $settingsIni) {
    $section = $null
    foreach ($line in Get-Content -LiteralPath $settingsIni) {
        $trim = $line.Trim()
        if ($trim.Length -eq 0 -or $trim.StartsWith(';') -or $trim.StartsWith('#')) { continue }
        if ($trim.StartsWith('[') -and $trim.EndsWith(']')) {
            $section = $trim.Substring(1, $trim.Length - 2)
            continue
        }
        if ($section -ne 'display' -and $section -ne 'keybinds') { continue }
        $eq = $trim.IndexOf('=')
        if ($eq -lt 1) { continue }
        $key = $trim.Substring(0, $eq).Trim()
        $val = $trim.Substring($eq + 1).Trim()
        if ($key.Length -eq 0) { continue }
        # Empty value = 'not set' (F1 overlay writes e.g. render_target_path_d3d12=);
        # keep the launcher default instead of passing an empty cvar.
        if ($val.Length -eq 0) { continue }
        if ($section -eq 'display' -and $key -eq 'display_mode') {
            # One dropdown → fullscreen + fullscreen_exclusive cvars.
            switch -Regex ($val) {
                '^(?i)exclusive$' {
                    $settingsArgs.Add('--fullscreen=true')
                    $settingsArgs.Add('--fullscreen_exclusive=true')
                }
                '^(?i)borderless$' {
                    $settingsArgs.Add('--fullscreen=true')
                    $settingsArgs.Add('--fullscreen_exclusive=false')
                }
                default {
                    $settingsArgs.Add('--fullscreen=false')
                    $settingsArgs.Add('--fullscreen_exclusive=false')
                }
            }
            continue
        }
        # Newer config.exe / F1 virtual keys expand into several cvars.
        $handled = $false
        if ($section -eq 'display') {
            switch -Regex ($key) {
                '^schema$' { $handled = $true }
                '^frame_rate$' {
                    switch -Regex ($val) {
                        '^30$' { $settingsArgs.Add('--nba_frame_limit=30'); $settingsArgs.Add('--video_mode_refresh_rate=30') }
                        '^(?i)unlimited$' { $settingsArgs.Add('--nba_frame_limit=0'); $settingsArgs.Add('--video_mode_refresh_rate=120') }
                        default { $settingsArgs.Add('--nba_frame_limit=60'); $settingsArgs.Add('--video_mode_refresh_rate=60') }
                    }
                    $handled = $true
                }
                '^anti_aliasing$' {
                    switch -Regex ($val) {
                        '^(?i)fxaa$' { $settingsArgs.Add('--swap_post_effect=fxaa'); $settingsArgs.Add('--native_2x_msaa=false') }
                        '^(?i)fxaa_extreme$' { $settingsArgs.Add('--swap_post_effect=fxaa_extreme'); $settingsArgs.Add('--native_2x_msaa=false') }
                        '^(?i)msaa2x$' { $settingsArgs.Add('--swap_post_effect=none'); $settingsArgs.Add('--native_2x_msaa=true') }
                        default { $settingsArgs.Add('--swap_post_effect=none'); $settingsArgs.Add('--native_2x_msaa=false') }
                    }
                    $handled = $true
                }
                '^resolution_scale$' {
                    $settingsArgs.Add("--resolution_scale=$val")
                    if ([int]$val -gt 1) {
                        # Automatic scale fixes above Low (unless set by hand in the ini).
                        $settingsArgs.Add('--draw_resolution_scaled_texture_offsets=true')
                        $settingsArgs.Add('--resolve_resolution_scale_fill_half_pixel_offset=true')
                    }
                    $handled = $true
                }
            }
            if ($handled) { continue }
        }
        if ($section -eq 'keybinds' -and $val -eq '(none)') { continue }  # cleared input: keep launcher default
        $settingsArgs.Add("--$key=$val")
    }
}
$combinedExtra = @()
# Explicit -ExtraArgs beat settings.ini: drop ini flags that ExtraArgs redefine.
# Duplicate flags with different values (e.g. --vsync=false + --vsync=true)
# make the SDK CLI parse fail ("--game_data_root was not provided").
$extraNames = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($token in $ExtraArgs) { if ($token -like '--*') { [void]$extraNames.Add(($token -split '=', 2)[0]) } }
$settingsArgs = @($settingsArgs | Where-Object { -not $extraNames.Contains(($_ -split '=', 2)[0]) } |
    Select-Object -Unique)
if ($settingsArgs.Count -gt 0) { $combinedExtra += @($settingsArgs) }
if ($ExtraArgs.Count -gt 0) { $combinedExtra += $ExtraArgs }
if ($combinedExtra.Count -gt 0) {
    # ReXGlue cvars are first-wins on the CLI. Drop earlier --flag entries that
    # settings.ini / ExtraArgs redefine so player choices actually apply.
    $overrideFlags = [System.Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    for ($i = 0; $i -lt $combinedExtra.Count; $i++) {
        $token = $combinedExtra[$i]
        if ($token -like '--*') {
            $name = ($token -split '=', 2)[0]
            [void]$overrideFlags.Add($name)
            if ($token -notmatch '=' -and ($i + 1) -lt $combinedExtra.Count -and
                $combinedExtra[$i + 1] -notlike '--*') {
                $i++
            }
        }
    }
    if ($overrideFlags.Count -gt 0) {
        $filtered = [System.Collections.Generic.List[string]]::new()
        for ($i = 0; $i -lt $arguments.Count; $i++) {
            $token = $arguments[$i]
            if ($token -like '--*') {
                $name = ($token -split '=', 2)[0]
                if ($overrideFlags.Contains($name)) {
                    if ($token -notmatch '=' -and ($i + 1) -lt $arguments.Count -and
                        $arguments[$i + 1] -notlike '--*') {
                        $i++
                    }
                    continue
                }
            }
            $filtered.Add($token)
        }
        # Keep a true string[] so later += ExtraArgs cannot reintroduce
        # filtered flags via List/array coercion quirks.
        $arguments = @($filtered)
    }
    $arguments += $combinedExtra
}
if ($DryRun) {
    [pscustomobject]@{ Executable = $executable; Arguments = $arguments -join ' ' }
    return
}
$started = Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $buildRoot -WindowStyle Hidden -PassThru
if ($ProcessPriority -ne 'Normal' -and $null -ne $started -and -not $started.HasExited) {
    try {
        $started.PriorityClass = $ProcessPriority
    } catch {
        Write-Warning "Could not set process priority to $ProcessPriority : $_"
    }
}
$started
