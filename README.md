# UltimateHL

A Windows add-on for the **Half-Life 2005 build 3248 / `valve_WON` layout**, combining an unmodified stock `client.dll`, **UHLCompat.dll**, and a rebuilt **Tankfird Bunnymod XT (BXT)**.

**New in 3.7.1:** slow mouse movement is no longer rounded away at low sensitivity. A single mouse count at `sensitivity 0.1` now contributes immediately to the view, rather than becoming zero.

> Experimental, local/offline use only. Not certified for speedrun submissions; input, rendering and timing changes may require category approval. **Do not use on VAC.** This is a runtime compatibility adapter, not a rebuilt engine or a standalone copy of Half-Life.

## Download and install

Download **`UltimateHL-v3.7.1-addon.zip`** from [Releases](https://github.com/ProtoAus/UltimateHL/releases). The public download contains the launcher, adapter, rebuilt BXT, setup tools and notices. **It does not contain Half-Life, Valve assets, Big AG HUD artwork or the BXT injector.** Supply your own licensed supported game, HUD pack and injector. Normal game ownership/Steam requirements still apply.

### Requirements

- Windows and the original **2005 build 3248** game files with `valve_WON`; not the current Steam/25th Anniversary build and not arbitrary WON releases.
- Your Big AG HUD pack (`hud.txt` and its `.spr` files).
- Your BXT `Injector.exe` (from the [Bunnymod XT Windows distribution](https://github.com/YaLTeR/BunnymodXT/releases)).
- Python **3.10+** for setup only. Python is not needed to play afterward.
- An OpenGL-capable GPU. The default DXGI capture path additionally needs working NV/D3D11 interop; unsupported configurations fall back to OpenGL.

Extract the add-on, open a terminal in that folder, and run:

```powershell
py -m pip install -r requirements.txt
py tools/install_addon.py --game "C:\Games\Half-Life 2005 WON\Half-Life" --destination "C:\Games\UltimateHL" --ag-hud "C:\Downloads\big AG HUD" --injector "C:\Tools\BXT\Injector.exe"
```

Choose a **new destination folder**. Setup validates the engine/client fingerprints and creates a separate clean installation. It does not modify the source game or copy personal saves, binds or configs. The output contains locally copied licensed files: **do not upload that installed folder or ZIP it for public redistribution.**

Close other Half-Life copies, then run **`UltimateHL.exe`** in the new folder. Keep it beside `Half-Life`, `BXT` and `HUD-presets`. Do not launch `Half-Life/hl.exe` directly. Open **UltimateHL settings...** in the game menu, or enter `uhl_settings` in the console.

For your existing UltimateHL 3.7 installation, close the game and back up the old files before replacing `Half-Life/UHLCompat.dll` and `UltimateHL.exe` with the matching files from the download's `bin` folder. Keep the rebuilt UltimateHL BXT; an unrelated official BXT cannot load the adapter for the stock client. Do not run the clean installer over an existing folder.

## Features

### Compatibility, display and menus

- Runs the **stock `client.dll`**, **UHLCompat.dll**, and the **Tankfird BXT fork with UltimateHL integration**. Supported game binaries remain unchanged on disk; validated hooks are applied in memory.
- **Borderless windowed mode by default**, with a full-resolution DXGI presentation path for OBS **Display Capture**. Ordinary windowed mode remains available. Unsupported GPU interop falls back to legacy OpenGL; capture compatibility is not guaranteed on every driver/GPU.
- DPI-aware startup keeps menu clicks aligned with the visible interface.
- Working menus with the **built-in relative raw-input mouse path**. There is no need for RInput; **do not stack RInput or another raw-input injector** with this adapter.

### Mouse and console

- **Low-sensitivity precision:** fractional mouse movement survives filtering and sensitivity scaling, including values such as `sensitivity 0.1`. Tiny movements are applied immediately rather than discarded or delayed until they reach a whole scaled count.
- Raw gameplay input **bypasses Windows pointer acceleration / Enhanced Pointer Precision**. The client's attempts to change Windows acceleration or pointer speed are blocked, including its exit-time restore; your existing Windows preferences are left unchanged.
- Cursor confinement during focused gameplay, including very low-FPS play, makes accidental clicks on another monitor/window less likely. Alt-Tab, lost focus, the console and settings release the cursor.
- Clicking back into the game while the console is open no longer recentres the gameplay mouse or turns Gordon's view behind the console.
- **Ctrl+C copies selected console text**, as the console's right-click Copy action does.
- Optional load-time mouse X/Y discard and cursor pinning prevent queued mouse deltas from causing a view jump after loading. The saved camera orientation is not overwritten.

### Saves, loading and frame timing

- Load-specific repair of held stock **`+`/`-` button ownership**, addressing movement keys dropped across save-loads and map transitions.
- Shared loading-lifecycle handling for save-loads and trigger-based changelevels.
- The unwanted automatic in-game load menu is suppressed by default. Deliberately opening the menu still works.
- Optional precision frame timer, enabled in settings or with **`bxt_precise_fps_limit 1`**: targets each `fps_max` deadline more closely instead of waking a few milliseconds late. This reduces scheduling overshoot; Windows scheduling is not a guarantee of exact frame-start times. `bxt_frametime_stats` reports observed frame times.
- **1998-style save backups:** overwriting a save keeps the previous one as `name01` (`quick` → `quick01`). This is a single previous-save backup, not a rotating history.
- Optional load FPS floor; **`uhl_load_fps_min 0` is the default**, so no adapter-imposed floor is forced on glitch-sensitive routes.

### Textures, HUD and image quality

- **Texture fix:** native-size texture uploads and safe mip handling avoid unwanted legacy power-of-two resizing, with validation and fallback.
- **HUD sprite fix:** corrected texel centres and atlas guards reduce stray lines caused by sampling neighbouring HUD sprites.
- Optional **scene-only supersampling**, independent of HUD scale. The default is **2× per axis** (four times the scene pixels); use `uhl_render_scale 1` for native resolution.
- **Big AG HUD artwork by default**, using the pack you supply at setup. Original 640 and Compact 320 presets remain selectable. Restart after changing artwork.
- Independent HUD scale choices: **1×, 1.25×, 1.5×, 1.75× and 2×**, applied live. Health, armour, ammo and BXT's timer/speedometer scale on a separate filtered layer; text, menus and the crosshair remain at native size.
- **3D-view brightness**, via the settings slider or `uhl_brightness`: a curve applied to the finished scene that keeps black and white fixed, without rebuilding lightmaps. The legacy gamma/brightness cvars are held at engine defaults by default (`uhl_gamma_lock 1`). While locked, the legacy `brightness` command/menu slider redirects to the new effect (`brightness N` → `uhl_brightness N-1`).

### Local single-player practice

- **`sv_cheats 1` takes effect in the current local single-player map**, without starting a new map, for the supported cached permissions (including god/noclip and the GameDLL's impulse permissions).
- Cheats are never enabled automatically. This is restricted to a validated local one-player server with deathmatch/coop off; it does not grant permissions on remote or multiplayer servers.
- `sv_cheats 0` closes the permission gates, but does not remove granted weapons or automatically undo an already-enabled god/noclip effect. Turn those effects off first. `uhl_cheats_live 0` opts out.

The Tankfird fork also includes entity range/angular-velocity displays, stuck-groundentity practice, rotation-trace controls and physent displays. See [its feature notes](third_party/Tankfird-features.md). These practice modes are not enabled automatically.

## Useful commands

```cfg
sensitivity 0.1           // stock sensitivity, now with fractional precision
uhl_status                // feature validation, including fractional mouse precision
uhl_settings              // settings panel
uhl_settings_save         // persist supported numeric preferences
uhl_render_scale 1        // native scene; 2 = 2x supersampling on each axis
uhl_hud_scale 1.25         // also 1, 1.5, 1.75 and 2
uhl_brightness 0.5        // scene brightness: -0.9..3; 0 = off
uhl_gamma_lock 0          // allow legacy lightmap gamma/brightness again
uhl_load_fps_min 0        // no adapter load FPS floor (default)
bxt_precise_fps_limit 1   // optional more precise frame deadlines
bxt_frametime_stats       // measure real frame timing
```

Preferences are stored locally in `Half-Life/valve_WON/uhl_preferences.cfg`. The first launch starts with neutral WASD controls and sensitivity 3; change sensitivity in the console as usual. `uhl_status` should report **Fractional mouse precision: enabled**. Unsupported layouts fail validation rather than guessing offsets.

## How the precision fix works

Raw input alone did not solve the bug: the stock client's `IN_MouseMove` converted both filtered counts and sensitivity-scaled movement back to integers. Small products such as `1 × 0.1` became zero.

`native/precision_mouse.cpp` validates the complete supported calculation block, replaces that calculation with floating-point scaling, and changes its five downstream X/Y consumers from integer loads to float loads. The original client still handles look/strafe selection, `m_yaw`, `m_pitch`, pitch limits, movement commands and cursor recentering. HUD/zoom sensitivity overrides still apply. Nothing is patched on disk, and the changed instructions are restored on shutdown.

This removes **integer truncation drops in the supported client**. It cannot recover movement that never reaches Windows, exceed the mouse's hardware resolution, eliminate normal floating-point limits, or remove engine/network/demo angle quantization.

## Build and test

Requires a **32-bit MSVC toolchain / Visual Studio 2022 Build Tools**, Windows SDK, and CMake 3.20+:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
py -m pip install -r requirements.txt
py -m unittest discover -s tests -p "test_*.py" -v
py tools/test_native_loader.py
```

The adapter and launcher use the statically linked C++ runtime. The real-client precision/attach tests need your local supported client and its game dependencies; they skip when it is absent. Set `-DUHL_STOCK_CLIENT="C:/Games/.../valve_WON/cl_dlls/client.dll"` at configure time if needed.

The precision regression test invokes the **actual stock client's mouse code** with fake cursor/engine callbacks, so it neither launches the game nor moves your desktop pointer. It checks single-count X/Y motion at 0.01, 0.1, 0.5, 0.99, 1 and 3; both signs; filtered half-counts; zoom overrides; zero sensitivity; low-FPS accumulation; strafing; pitch clamps; relocation; restoration; and rejection of modified instructions. An unpatched control reproduces the original drop.

The stock-client layout also needs the rebuilt Tankfird BXT. Its exact upstream revision and UltimateHL patch are supplied in [`third_party`](third_party/README.md); follow those build instructions, rather than substituting an unrelated BXT DLL. See [TESTING.md](TESTING.md) for manual checks and older development history.

## Licensing and distribution

Original UltimateHL code is MIT licensed; see [LICENSE](LICENSE). BXT and its dependencies retain their own notices in [third_party/BunnymodXT-LICENSE.txt](third_party/BunnymodXT-LICENSE.txt), with upstream source and the integration patch linked in [third_party/README.md](third_party/README.md).

Valve's binaries/game data, AG artwork and third-party injectors are **not relicensed by this project**. Public releases are add-on-only. Local setup copies the files you supply for your own use; it does not bypass ownership checks or grant permission to redistribute a complete game.
