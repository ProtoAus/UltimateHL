# Playtest and regression checklist

## 3.7.1 precision and public add-on regression

Run from the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
py -m unittest discover -s tests -p "test_*.py" -v
py tools/test_native_loader.py
```

`precision_mouse_tests` loads the real supported stock client locally and replaces
cursor/engine callbacks with fakes. No game is launched, no desktop input is
injected, and the desktop pointer/settings are not changed. It verifies both X/Y
signs at 0.01, 0.1, 0.5, 0.99, 1 and 3, immediate first-count response, filtered
half-counts, HUD/zoom sensitivity, zero sensitivity, low-FPS accumulation, stock
strafing/pitch clamps, relocated code validation, instruction restoration, and
rejection of another tool's edits. The unpatched control reproduces the drop.
Supply `UHL_STOCK_CLIENT` at CMake configure time if your licensed client is not
in the default adjacent local workspace. Real-client tests skip if it is absent.

Manual checks before treating the build as suitable for your own play:
- At sensitivity 0.1 and m_filter 0, move slowly in both directions, then repeat
  with m_filter 1, zoom, ordinary FPS and 4 FPS. Verify no delayed whole-count steps.
- Open menus, console and settings; Alt-Tab and re-enter with the console open.
  The pointer must remain usable, and no queued movement should turn the view.
- Save/load and cross a trigger changelevel with movement held; test the load
  mouse-freeze option both ways and confirm the saved view is not overwritten.
- Check OBS Display Capture while focused on your actual driver/GPU.

Public packaging uses `tools/package_addon.py`, NOT the historical full-game
`package_distribution.py`. Never upload a local installed game, development
reports, personal configs/saves, or the old full-game dist folders. Historical
notes below refer to private local paths/reports, not public download contents.

Use separate directories for the 1998 control, original 3248, and this prototype.
Do not load an old-version save in a new engine as a way to create a control;
create equivalent saves independently. Record exact engine version, game dir,
BXT DLL hash, resolution, render mode, focus state, mouse settings, bindings,
and all compatibility cvars for each trial.

## Current cleaned AG distribution

`dist/UltimateHL-v3.6-AG` is the current runtime handoff. AG art is installed by
default, with scene 2x, HUD 1x and floor 0. Personal configs/saves, captures,
logs, traces, updater backups, server executables and Linux libraries are not
packaged. Alternate art and actual game/UI resources remain. The package tool
writes a hash list and keeps its cleanup report outside the runtime folder.
`resources/ABOUT AND FEATURES.txt` is the human-readable architecture/feature
list shipped with the build. Existing playtest copies are not pruned in place.

Rebuild/repackage (choose NEW destination/archive/report paths):

```text
cmake --build UltimateHL/build --config Release
python UltimateHL/tools/package_distribution.py --archive UltimateHL/dist/UltimateHL-v3.6-AG.zip --report UltimateHL/reports/distribution-cleanup.json
```

## V3.6 playtest (historical 640-art default)

Use **`playtest-v3.6/START HERE.bat`**. See `reports/V36-FINDINGS.md`.
- Defaults: original 640 art, 2x scene, supplied Tankfird BXT, normal DXGI
  Display Capture path, load FPS floor 0. Only two launcher BATs remain.
- In-game art list: original 640 / compact 320 / Big AG; restart for art.
  Independent HUD 1x / 1.5x / 2x applies live and saves locally.
- Real HUD matrix: 1920x1200 output; virtual 1280x800 and 960x600 for scaled
  HUD; zero fallbacks; BXT speedometer centered; alpha/additive coverage tested.
- Native-scale default launch resolves scene 3840x2400 -> 1920x1200 without
  an explicit scale override. Game-menu/mouse coordinates stay physical.
- Same-map sv_cheats/impulse test verifies inventory 0 -> 8000ffde and suit
  0 -> 1. Opt-out control remains inventory/suit 0. Both cached permissions
  follow 0/1/0; already granted inventory is intentionally not confiscated.
- Tankfird feature registration is observed. Groundentity selection is a cvar.
  Exercise route-specific commands manually; registration is not physics proof.
- 10 native groups, 21 Python tests, relocated loader/mouse guard and owned
  settings persistence/AG selector tests pass. Older physical input/gauss and
  category-neutrality caveats remain. No desktop input/focus injection in QA.

## V3.5 (historical)

Use **`playtest-v3.5/START HERE.bat`**. See `reports/V35-FINDINGS.md`.
- User confirmed v3.4 changelevel cursor protection and default OBS Display
  Capture; retain those paths rather than returning to OBS-safe mode.
- Load FPS floor defaults 0. Real local crossfire save-load: entry/return 4,
  all nine observed UI-tail cap samples 4. The old c1a0 failed-save fixture is
  NOT successful-load evidence.
- Original v3.1 compact 320 definitions (12x16 digits) plus nearest sampling.
  Current v3.1 active files were switched to stock; compare its compact preset.
- GPU raster regression covers compact/stock digits, reticle and icon sizes;
  source texels exact, one-pixel border clear (no extra atlas rows/columns).
- Immediate local-SP sv_cheats latch: requested 0/1/0 follows 0/1/0 in one map;
  god/noclip ON/OFF verified without a new map. Opt-out retains old latch 0.
  No remote/multiplayer/coop mutation; unit policy exercises all rejected cases.
- 9 native groups, 20 Python tests and relocated loader guard are the current
  regression set. Physical HUD appearance and category neutrality still need
  manual/user confirmation. Turn god/noclip off before sv_cheats 0.

## V3.4 (historical)

Use **`playtest-v3.4/START HERE.bat`**. See `reports/V34-FINDINGS.md`.
- Default HUD: original v2-compatible assets/lookups and blended sprite path.
- Optional nearest: corrected .5-pixel quad-origin bug, not UV-only adjustment.
- GPU sprite test: reproduce 240 mismatches in previous geometry, zero corrected.
- Full-resolution GPU-only windowed DXGI output; exact two-frame interop test.
- Foreground Windows Desktop Duplication test at 1920x1200 reads only two known
  32x32 game markers. No OBS recording or exact legacy-driver cause is claimed.
- Real original `c1a0 -> c1a0d` touch-trigger lifecycle at 4 FPS. A focused trace
  records freeze=1 and clip=1x1; background run correctly releases. No console
  load/changelevel command is used in that test.
- Native panel has no 4/100 FPS buttons. Apply/persistence remains tested.

Manual priorities: same-position crosshair/health/armor/ammo/BXT speedo comparison
against v2; actual OBS Display Capture preview/recording while focused; physical
low-FPS cursor stress and held-input chords; first-resume/gauss timing. The DXGI
framebuffer is single-sampled; 2x scene supplies AA, legacy MSAA requires sample
buffers. Unsupported interop or a video-mode change retains/falls back to legacy
GL. Never submit runs as certified solely on these tests.

## V3.3 (historical)

Use **`playtest-v3.3/START HERE.bat`** or its normal 2x launcher, with other games closed.
See `reports/V33-FINDINGS.md` for current evidence. V3.3 has corrected real-game
SSAA, MSAA-output-compatible presentation, focused cursor confinement/load-delta
freeze and a native settings panel. 19 Python tests and 6 native groups pass.
The staged game really resolves 3840x2400 -> 1920x1200 with no diagnostic GL
errors. Owned settings Apply/persistence is tested without desktop input/focus
injection. Physical 4-FPS cursor stress, exact HUD appearance, first-resume gauss
timing and actual OBS capture remain manual checks. Do not submit as certified.

## V3.2 (historical)

Use **`playtest-v3.2/START HERE.bat`**, with v3.1 closed. See
`reports/V32-FINDINGS.md` for implementation, evidence boundaries and focused
manual steps. Later sections below are historical milestones, not current defaults.

Automated: 17 Python tests, five native groups (including hidden-WGL 2x resolve
and state restoration), Windows mock-loader/mouse-preference guard all passed.
The live v3.2 QA launch was skipped because an `hl.exe` was already running;
no other game was closed, hot-patched or injected with test input.

Priority: literal `uhl_fps 4` / `uhl_fps 100` binds during a floor-100 load;
no automatic main menu; cap restoration at late-clear/active resume; exact
first-resume/gauss timing; W+A/second-key physical continuity; quicksave W/S
pitch; atlas lines and scene-only 2x vs native HUD; full-resolution OBS WGC.
Cap values alone do not establish achieved FPS. Floor 0 and `uhl_load_menu 1`
are the stock-behavior comparison controls. Do not submit runs as verified.

## V3.1 evidence (historical)

Use **`playtest-v3.1/UltimateHL.exe`**. See `reports/V31-FINDINGS.md`.

- [x] Four C++ groups, 17 Python tests and relocated mock-loader/mouse-settings test.
- [x] Real 3248/BXT GPU upload/readback: legacy 48x8 -> 64x8; new base 48x8,
      every green/black texel exact, final mip 1x1, GL error zero.
- [x] Real normal borderless client/render/viewport 1920x1200; capture-safe mode
      1920x1080 at (0,60), no scaling, display capture exclusion zero.
- [x] `pausable 0`, binding-table validation and all 20 stock action-state layouts.
- [x] Reversible 320/640 HUD list presets; compact digits 12x16, original 20x24;
      reticle/zoom/autoaim definitions preserved. BXT uses the same HUD list.
- [x] User traces analyzed: initial key-downs during post-load UI have no client
      down callback by first active command; client clears follow inactive samples.
- [ ] Focused fresh-press-during-load/chord/release/gauss verification. The guarded
      helper refused injection because its game was not foreground; no pass claimed.
- [ ] User screenshot comparison at the same computer panel/camera/HUD state.
- [ ] OBS focused preview/recording with full-resolution WGC and workaround mode.

## Historical v3 evidence

Previous binaries are preserved in `playtest-v3/`. See `reports/V3-FINDINGS.md`.

- [x] Real 3248/BXT startup, map load and exit; launcher verified the native client.
- [x] `uhl_help`, `uhl_status`, `uhl_diagnostics`, `uhl_trace_start`, `uhl_trace_stop`
      and `uhl_fps` verified as registered, not just documented names.
- [x] Borderless WINDOWED client/render/GL viewport agree at 1920x1200; early DPI
      awareness active, window capture exclusion off. This is NOT an OBS recording.
- [x] Requested sprite/nearest-mipmap/round-down/picmip defaults verified live.
- [x] Windows acceleration/EPP and pointer-speed preferences equal before/after
      a real map/start/exit test; six client writes blocked, including shutdown.
      No mouse-settings restore was attempted.
- [x] Fake-API test blocks writes even during original initialization and after
      original shutdown; reads/unrelated settings pass. No real OS setters used.
- [ ] Focused W+A/other chords, first-frame velocity and gauss load behavior.
- [ ] Clean OBS Display Capture recording with the game focused.

## Historical v2 evidence

V2 binaries are retained; current tests use v3.1. See `reports/V2-FINDINGS.md`.

- [x] Real 3248 startup with early manifest DPI awareness; physical cursor minus
      client origin agrees exactly with engine mouse coordinates at 800x600.
- [x] Original bundled BXT startup/shutdown with the v2 adapter/load bridge ready.
- [x] Missing-save FPS test: 4 -> temporary 100 -> restored 4.
- [x] Real local-save load: explicit selection of 100 during the load survives
      return instead of incorrectly restoring 4.
- [x] Read-only asset comparison: 13 identical HUD sprite bitmaps, 121 identical
      shared rect definitions and five identical WAD files.
- [x] Save-backup regression reproduced for the unique `uhl_probe` slot in v2:
      two different saves, no numbered `01` backup. This is NOT fixed yet.

The user confirmed the v2 mouse/menu offset was gone and UI scaling looked fixed.
Raw feel, held/buffered input and full BXT gameplay still need verification. V3
uses desktop-sized borderless windowed mode. Test helpers restore only shared
video preferences; they never set/restore Windows mouse preferences.

## Completed automatically

- [x] Win32 Release build (`UHLCompat.dll`), `/W4 /WX` for native code.
- [x] Loading-FPS decision tests, including disabled/invalid settings, untouched
      restoration, an external change and an explicit selection equal to the floor.
- [x] Raw count sum, single consumption, inactive suppression, transition clear,
      repeated-state polling, explicit clear and overflow-bound tests.
- [x] Held-key transition and stock button-owner/impulse-state tests, including
      two independent actions, fresh presses without callbacks, two resumed commands,
      persistent repaired-owner release and refusing to overwrite two other owners.
- [x] NPOT dimension gates, safe odd/one-wide mipmaps and HUD-coordinate controls.
- [x] Native-art HUD selection/rollback and isolated display-only WGC collection
      generation; saved OBS sources/profiles are not edited.
- [x] Eight Python PE regressions on the actual supported client plus six updater
      tests (unknown hashes, v2 refusal, multi-file failure rollback and user data).
- [x] Original client `.text`, `.data`, old `.reloc`, existing IAT bytes and
      non-adapted exports remain unchanged on disk.
- [x] Added import jumps have base relocations; new sections are not RWX.
- [x] Unknown, in-place and double-patch operations are refused.
- [x] Native Windows loader test with a relocated MOCK client: nine callback
      adapters, native command registration, unmodified out-of-scope cursor path,
      restoration of a preceding IAT hook on shutdown, and manifest DPI awareness
      established before client loading.
- [x] Staging checks original engine/server/config hashes and preserves originals.

These are structural/component checks, **not gameplay verification**.

## Raw-input playtest — all still pending

First run `playtest-v3.1/Play - native raw input.bat`. Steam must already be running.
Open the console: `uhl_status`. It should report adapter installed and raw mouse
registration ready. Do not run RInput. `uhl_rawinput 0` is a stock-input fallback;
`uhl_rawinput 1` enables the prototype. Watch for conflict/absolute-device warnings.

| Test | Required observation | Result |
| --- | --- | --- |
| Slow/fast equal-count mouse sweep | Same yaw magnitude without OS pointer acceleration; stock sensitivity still applies | pending |
| `m_filter 0` and `1` | Existing stock filtering choice still respected | pending |
| 4, 20, 60 and 100 FPS look | No duplicated samples, large jump, extra look drift, or missed raw counts | pending |
| Open/close main menu | Cursor clicks the point displayed; no background camera motion | v2 offset fixed per user; v3 regression pending |
| Click buttons, sliders and resolution choices | Normal coordinate alignment throughout the UI | pending |
| Console open/close | No accumulated-look jump when closing it | pending |
| Alt-tab while moving/holding attack | No queued camera jump on return; no new button behavior introduced | pending |
| Windowed/fullscreen + resolution change | Registration reattaches if window is recreated; no stale HWND | pending |
| Quicksave/load while moving the mouse | No new first-frame turn or native-module crash | pending |
| `uhl_rawinput 0` then `1` repeatedly | Stock fallback and raw reactivation, no stale movement | pending |
| Exit, restart, repeat | Clean startup/shutdown; existing Windows EPP and speed untouched, including cached restores | real map/start/exit passed; manual repeated/focused test pending |
| Repeat with `Play - native raw input + BXT.bat` | Correct BXT HUD/input, no unexpected pattern/hook warnings or crashes | startup passed; gameplay pending |

If BXT fails, capture `playtest-v3.1/bxt-playtest.log`; include exact commands and
whether the problem happens without BXT. The prototype keeps original movement
instructions but does change callback entry points, so BXT coexistence cannot
be certified by byte comparisons alone.

## Save-backup baseline — do this BEFORE implementing changes

In each control installation:

1. Start a map, move to state A, run `save quick`.
2. Record names, sizes, modification times and SHA-256 of `SAVE/*.sav`.
3. Move to a clearly different state B, run `save quick` again.
4. Verify the backup's bytes equal the successful A snapshot, not B.
5. Load `quick` and `quick01` separately and verify the expected state.
6. Repeat for `save testslot` (`testslot01.sav` if numbered backups are used),
   the UI save entry point, and the very first save in an empty test SAVE dir.
7. With disposable saves only, test a rejected save (dead/intermission), disk
   write failure and interrupted save: a valid old save/backup must not be lost.

Do not treat the existence of `quick01.sav` as proof of correct rotation in every
case. The 1998 folder has `quick01`/`HLSave01` artifacts. A v2 disposable-slot overwrite
was observed NOT to create `01`; the complete rotation/failure matrix is still pending.

## Input/load matrix — capture the WON behavior first

In v3.1, first test with the default `uhl_preserve_load_keys 1`, then repeat with
`uhl_preserve_load_keys 0` as a stock-behavior control. Use your existing binds
and a disposable save slot. Run `uhl_trace_start`, close the console, reproduce
the sequence, then run `uhl_trace_stop`. This produces a local `uhl-input-*.tsv` with delivered
window messages (including queued key-message age), client callbacks/clear-state
calls, load/FPS boundaries and usercmd button samples. Logs contain key codes,
not binding strings/entered text; stop tracing after the intended test. Do not
benchmark or submit runs while tracing. Physical held-state reconciliation is
now enabled for recognized stock actions, not arbitrary aliases or full tap
buffering. An automated physical-input attempt was refused because the game
was not foreground; no successful focused chord test is claimed.

Repeat each case using keyboard movement, duck, jump, MOUSE1, MOUSE2/gauss charge,
and a keyboard binding for the same attack. Run at 100 FPS and then 4 FPS. Where
possible record physical event timestamps, `Key_Event`, `+/-` commands and the
first resumed usercmd/buttons, not merely a video of the visible outcome.

| Sequence | Questions to answer |
| --- | --- |
| Down before load; held through resume | Does it stay held exactly once? Does gauss charge survive? |
| Down before load; up during load | Is it released, with no stuck action? |
| Up before load; down during load; held | Is it applied on the first eligible resumed frame? |
| Down and up entirely during load | Does WON preserve a tap? Decide explicitly whether the target should match it. |
| Two distinct taps during load | Are ordering and edges preserved without inventing extra simulation frames? |
| Held key rebound while loading | Which binding owns the eventual release? |
| Focus lost/menu opened during load | Is input discarded or transferred safely? No background action should be synthesized. |
| Failed/missing save load | Are input state and FPS unaffected by the failed transition? |
| Consecutive loads and map change | No replay of stale edges from the previous transition |

Use both direct binds and aliases with `wait`. Check BXT recording/playback,
shared RNG-sensitive behavior, first-frame velocity, duck state and weapon state.
Do not approve a change just because it fixes the single held-forward case.

## FPS and loading fade

V3.1 retains the opt-in v2 guard: `uhl_load_fps_min 100` (0 disables). Use `uhl_fps 4`
and `uhl_fps 100` in test bindings so explicit same-as-floor selections can be
recognized. Both missing-save restoration and one real in-load selection have
been checked. General arbitrary binding delivery, fatal Host_Error/longjmp
recovery and fade changes are not certified. Restart after such a fatal error.

- Reproduce `fps_max 4`, start a load, then press the FPS-switch binding at early,
  middle and late loading stages. Log the physical message, command execution,
  UI frame times and first accepted physics frame. Repeat from the console/menu.
- Distinguish a slow loading-panel fade from an actual game `ScreenFade`. Test a
  map-triggered fade too; a loading-only fix must not remove it.
- Compare timing with BXT timer/demo boundaries, not only wall-clock impressions.
- Leave `clockwindow 0`/`fps_max 100` reference config intact for control trials;
  restore the selected test FPS after each experiment.

## Texture and stock HUD comparison

Use the same resolution, aspect ratio, display scaling, viewsize, camera and HUD
state. Record effective sprite assets and fallback PAK source. Compare known
power-of-two and non-power-of-two textures, source/upload dimensions, texcoords,
`gl_picmip`, `gl_round_down`, maximum texture size and filtering. Capture lossless
screenshots. Measure stock HUD pixels separately from BXT overlays and menu UI;
do not assume a universal 20% multiplier.

In v3.1, compare compact and stock HUD presets with the game closed. Both should
render their own art at native size, not a resized 640 bitmap. `uhl_hud_texels`
controls cropped UVs, NOT the asset preset. Run `uhl_render_test` after loading a
map and record its result. Restart when comparing `uhl_native_textures 0/1`;
already uploaded textures do not change retroactively. Keep `gl_max_size 512`.
Do not judge one-pixel color patterns from a scaled OBS preview or lossy 4:2:0
recording; use lossless game/framebuffer evidence.

For OBS, import the separate WGC test collection; check its monitor, keep just
the one display source, and compare focused/unfocused behavior. The existing
saved Color Key filter was disabled, so do not treat it as an established cause.
The separate OBS-safe launcher uses supported native-size letterboxing, not a
one-pixel shrink (3248 rejects arbitrary sizes). Confirm alt-tab immediately hides
its matte/demotes its taskbar cover and releases all held actions. Do not change
existing OBS collections or global driver settings to make this test pass.

## Rollback

Launch the original `Half-Life 2005 WON` folder. Nothing in it was patched.
The playtest also contains `client.stock.dll`; do not delete its backup or swap
DLLs while the game is running. A newly rebuilt adapter should be staged into
a NEW directory for a changed profile. `tools/update_native.py` can update a
known v3 DLL/launcher, with hash-named backups, without replacing saves/configs.
