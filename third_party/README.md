# BXT source, integration and notices

The released `BunnymodXT.dll` is built from [Tankfird/BunnymodXT](https://github.com/Tankfird/BunnymodXT), `testing` branch at commit **`edac2e5df202d24eb71995d198ffbbe936c18ab2`**, plus [`BunnymodXT-uhl.patch`](BunnymodXT-uhl.patch).

The patch contains all UltimateHL changes to that revision: adapter loading for the stock client, selective HUD-canvas cooperation, crosshair commands, frame-time measurement, the precise FPS limiter and single previous-save backups. The low-sensitivity fix itself is in UltimateHL's `native/precision_mouse.cpp`, not in BXT.

The upstream [source at that commit](https://github.com/Tankfird/BunnymodXT/tree/edac2e5df202d24eb71995d198ffbbe936c18ab2) and its pinned submodules provide the rest of the source. No upstream source is silently replaced by a binary-only change. [`BunnymodXT-LICENSE.txt`](BunnymodXT-LICENSE.txt) preserves the upstream MIT license and bundled dependency notices. [`Tankfird-features.md`](Tankfird-features.md) is the upstream feature list.

## Rebuild on Windows

Install Visual Studio 2022 Build Tools (C++/Win32 and Windows SDK), Rust with the **`i686-pc-windows-msvc`** target, and Boost **1.78** headers. Use the VS 2022 bundled **CMake 3.x** for BXT; its legacy cotire/Rust modules do not work unchanged with CMake 4.x.

From the directory containing the UltimateHL checkout:

```powershell
git clone https://github.com/Tankfird/BunnymodXT.git BunnymodXT-Tankfird
git -C BunnymodXT-Tankfird checkout edac2e5df202d24eb71995d198ffbbe936c18ab2
git -C BunnymodXT-Tankfird submodule update --init --recursive
git -C BunnymodXT-Tankfird apply --check ../UltimateHL/third_party/BunnymodXT-uhl.patch
git -C BunnymodXT-Tankfird apply ../UltimateHL/third_party/BunnymodXT-uhl.patch
rustup target add i686-pc-windows-msvc
```

Replace the example CMake/Boost paths below with your installed locations:

```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S BunnymodXT-Tankfird -B BunnymodXT-Tankfird/build -G "Visual Studio 17 2022" -A Win32 -DBOOST_ROOT="C:/deps/boost_1_78_0"
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build BunnymodXT-Tankfird/build --config Release
```

For a source checkout, install with `tools/install_addon.py --binaries build/Release --bxt-dll ../BunnymodXT-Tankfird/build/Release/BunnymodXT.dll` and the other game/HUD/injector arguments from the main README.

## Public package boundaries

The add-on ZIP contains only UltimateHL's three built binaries (launcher, adapter and rebuilt BXT), original setup/configuration code and these notices. It deliberately excludes Valve executables, DLLs and assets; all HUD sprite artwork, including Big AG; and third-party injectors. Those inputs are supplied by the person running setup. Installing locally is not permission to redistribute the resulting complete game folder.
