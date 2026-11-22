# Nezumiiruka - A GameCube and Wii Emulator for the Nintendo Switch
Nezumiiruka is a port of the [Dolphin](https://github.com/dolphin-emu/dolphin) emulator to the Nintendo Switch family of systems.

Note that Nezumiiruka has no affiliation with the Dolphin development team.

Nezumiiruka features a hand-written deko3d/drako3d backend to give the Switch as much of a fighting chance as possible
of reaching the goal of mostly fullspeed GameCube at stock handheld Switch 1 clockspeeds. The Switch 2 can reach fullspeed
in most Wii titles fairly easily, even at 4K.

Nezumiiruka itself is licensed under the same terms as Dolphin (GNU General Public License version 2 or later (GPLv2+)).
LSFG-NX (of which some code was borrowed) is licensed under the GNU General Public License 3 or later (GPLv3+).
Borealis is licensed under the Apache-2.0 license. libusbhsfs is licensed under the GNU General Public License version 2
or later license (GPLv2+).

If you find this software helpful, consider donating at: https://ko-fi.com/palindromicbreadloaf

## Installation
Download the latest nezumiiruka.nro (Switch 1) or nezumiiruka-ounce.nro (Switch 2) from the [releases](https://github.com/PalindromicBreadLoaf/nezumiiruka/releases)

Place the file in `sdmc:/switch/nezumiiruka/nezumiiruka.nro` for Switch 1 or `sdmc:/switch/nezumiiruka/nezumiiruka-ounce.nro` for Switch 2.

On Switch 2, [kaiHEN](https://github.com/ouncebrew/kaihen) is also required to be setup.

By default, roms are checked for in `sdmc:/switch/nezumiiruka/roms/`

This can be changed in the settings if you wish to have your roms elsewhere.

Supported game formats are: ISO/GCM/RVZ/WBFS/CISO/GCZ/WAD/DOL/ELF. RVZ is recommended.

## Switch 1 Performance
### GameCube

* Many 2D and some 3D games can run at fullspeed at stock clocks
    * Many 3D titles tend to hover around 80% at the moment and are fully CPU-bound.

### Wii

* Wii games require an overclock, the higher the better.
  * For info on overclocking, see [Horizon OC](https://github.com/Horizon-OC/Horizon-OC)

## Switch 2 Performance

* Wii is about 80% usually, GC is almost always fullspeed.

### Switch 2 limitations
The following are not currently available on Switch 2:
- USB storage
- Clock and performance configuration settings
- Fastmem (kaiHEN does not allow it).

## Features
- Everything that Dolphin supports (sans Netplay)
- A new deko3d/drako3d renderer
- Performance enhancements specifically for the Nintendo Switch
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
results out of. Switch 2 does a lot better with framegen due to the much more powerful/modern GPU. Also,
currently only the deko3d/drako3d backend has a frame generation implementation and there aren't plans to 
change that.

## Controls
- In-game menu: `+` and `−`
- Statistics overlay: `L3` and `R3`
- Everything else is mapped as you would expect
- (Wii only) `L3` resets pointer
- (Wii only) `R3` HOME button
- (Wii only) The touchscreen can be used as input for the Wii pointer.
- Depending on the configuration, the right stick is either shake or pointer.
  - Custom configurations can be made if you wish to change this.

## Notes
Shaders are not compatible with upstream. You cannot generate them on PC and transfer them over.

Savestates are likely to break across releases, and are also not compatible with upstream.

## Reporting Bugs
Please report any bugs that you find to the [Github Issues](https://github.com/PalindromicBreadLoaf/nezumiiruka/issues).

The requested logs can be found under the nezumiiruka folder.

You can also request features if there's anything that you want to see added.

## Building
### Switch 1
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
performant than (or at worst equivalent to) Vulkan in every case that I saw.

If you want, pass `-DNEZUMIIRUKA_VULKAN=ON` to also build the Vulkan renderer (requires NXVK).

This produces `build/switch/Binaries/nezumiiruka.nro`. Copy it to `/switch/nezumiiruka/` on the SD
card.

### Switch 2

Requires: 
* [ounce-devkit](https://github.com/ouncebrew/ounce-devkit)
* [drako3d](https://github.com/ouncebrew/drako3d) (which itself requires 
  * [drakotools](https://github.com/ouncebrew/drakotools) (Put `drakodef` and `drakomme` in `$PATH`):
* libuam2rt from [uam2](https://github.com/ouncebrew/uam2)

```sh
cmake -S . -B build/switch2 \
    -DCMAKE_TOOLCHAIN_FILE=CMake/Switch2.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build/switch2 --target nezumiiruka_nro -j$(nproc)
```

If you want, pass `-DNEZUMIIRUKA_VULKAN=ON` to also build the Vulkan renderer (requires OunceVK).

This produces `build/switch2/Binaries/nezumiiruka.nro`. Copy it to `/switch/nezumiiruka/nezumiiruka-ounce-vulkan.nro` on the SD
card.

## Credits
* Massive thanks to the Dolphin Emulator team for creating this amazing emulator. None of this
would be possible without their work.
* Borealis was used for the menus. Many thanks to those that created it.
* The fine folks over at DevkitPro that developed so much of the framework this project and the whole Switch scene depends on.
* Kurumuz who made kaiHEN and did much of the Switch 2 bringup
* The ARMv8-A reference manual (not actually I hate this thing.)
* mGBA for Switch-specific mGBA things
* LSFG-NX for deko3d framegen code
* libusbhsfs for USB implementation
* uam for the deko3d shader compiler
* uam2, libuam2rt, and drako3d for a Switch 2 native GPU backend
