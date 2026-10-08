# Nezumiiruka - A GameCube and Wii Emulator for the Nintendo Switch
Nezumiiruka is a port of the [Dolphin](https://github.com/dolphin-emu/dolphin) emulator to the Nintendo Switch.

Note that Nezumiiruka is not affiliated with the Dolphin development team.

It features a hand-written Deko3D backend to give the Switch as much of a fighting chance as possible
of reaching the goal of mostly fullspeed GameCube at stock handheld clockspeeds.

Nezumiiruka itself is licensed under the same terms as Dolphin (GNU General Public License version 2 or later (GPLv2+)).
LSFG-NX (of which some code was borrowed) is licensed under the GNU General Public License 3 or later (GPLv3+).
Borealis is licensed under the Apache-2.0 license. libusbhsfs is licensed under the GNU General Public License version 2
or later license (GPLv2+).

If you find this software helpful, consider donating at: https://ko-fi.com/palindromicbreadloaf

## Installation
Download the latest nezumiiruka.nro from the [releases](https://github.com/PalindromicBreadLoaf/nezumiiruka/releases)

Place the file in `sdmc:/switch/nezumiiruka/nezumiiruka.nro`

By default, roms are checked for in `sdmc:/switch/nezumiiruka/roms/`

This can be changed in the settings if you wish to have your roms elsewhere.

Supported game formats are: ISO/GCM/RVZ/WBFS/CISO/GCZ/WAD/DOL/ELF. RVZ is recommended.

## Performance
### GameCube

* Many 2D and some 3D games can run at fullspeed at stock clocks
    * Many 3D titles tend to hover around 80% at the moment and are fully CPU-bound.

### Wii

* Wii games require an overclock, the higher the better.
  * For info on overclocking, see [Horizon OC](https://github.com/Horizon-OC/Horizon-OC)

## Features
- Everything that Dolphin supports (sans Netplay)
- A new Deko3D renderer
- Performance enhancements for the Nintendo Switch
- Premade controller presets
- Switch-specific updater
- Frame generation via lsfg (see below)

## Frame generation
Nezumiiruka can use Lossless Scaling's frame generation to generate extra frames between the ones a game
renders. You must supply your own copy of `Lossless.dll`, which can be bought on [Steam](https://store.steampowered.com/app/993090/Lossless_Scaling/).

1. Copy `Lossless.dll` to `/switch/nezumiiruka/FrameGeneration/Lossless.dll`.
2. In Nezumiiruka, open Settings -> Frame generation and choose Prepare shaders. This will
   take a few minutes.
3. Turn on frame generation and pick a multiplier, flow scale, and performance mode.

Do note that frame generation is very resource intensive and will necessitate an overclock to get good
results out of. Also, currently only the Deko3D backend has a frame generation implementation.

## Controls
- In-game menu: `+` and `−` 
- Statistics overlay: `L3` and `R3`
- Everything else is mapped as you would expect
- (Wii only) `L3` resets pointer
- (Wii only) `R3` HOME button

## Notes
Shaders are not compatible with upstream. You cannot generate them on PC and transfer them over.

Savestates are likely to break across releases, and are also not compatible with upstream. 

## Reporting Bugs
Please report any bugs that you find to the [Github Issues](https://github.com/PalindromicBreadLoaf/nezumiiruka/issues).

The requested logs can be found under the nezumiiruka folder.

You can also request features if there's anything that you want to see added.

## Building
Requires [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the `switch-dev` package
group installed.

`bison`, `flex`, and `python3` also must be available for building.

Make sure to pull submodules before building:
```shell
git submodule update --init --recursive
```

NXVK (optional, only if you want the Vulkan backend), should be installed prior to building:
```shell
git clone https://github.com/PalindromicBreadLoaf/nxvk.git
cd nxvk
make
sudo make install
```

```shell
cmake -S . -B build/switch \
    -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build/switch --target nezumiiruka_nro -j$(nproc)
```

Only the deko3d renderer is built by default. This should be the renderer you use as it is more 
performant than Vulkan in every case that I saw.

If you want, pass `-DNEZUMIIRUKA_VULKAN=ON` to also build the Vulkan renderer (requires NXVK).

This produces `build/switch/Binaries/nezumiiruka.nro`. Copy it to `/switch/nezumiiruka/` on the SD
card.

## Credits
* Massive thanks to the Dolphin Emulator team for creating this amazing emulator. None of this
would be possible without their work.
* Borealis was used for the menus. Many thanks to those that created it.
* The fine folks over at DevkitPro that developed so much of the framework this project and the whole Switch scene depends on.
* The ARMv8-A reference manual (not actually)
* mGBA for Switch-specific mGBA things
* LSFG-NX for Deko3D framegen code
* libusbhsfs for USB implementation
* uam for the Deko3D shader compiler
