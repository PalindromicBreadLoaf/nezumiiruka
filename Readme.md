# Nezumiruka - A Gamecube and Wii Emulator for the Nintendo Switch

Nezumiruka is a port of the [Dolphin](https://github.com/dolphin-emu/dolphin) emulator to the Nintendo Switch.

It features a hand-written Deko3D backend to give the Switch as much of a fighting chance as possible
of reaching the goal of mostly fullspeed Gamecube at stock handheld clockspeeds.

Nezumiruka itself licensed under the same terms as Dolphin (GNU General Public License version 2 or later (GPLv2+)).
LSFG-NX (of which some code was borrowed) is licensed under the GNU General Public License 3 or later (GPLv3+).
Borealis is licensed under the Apache-2.0 license.

## System Requirements

### Gamecube

* Many 2D and some 3D games can run at fullspeed at stock clocks
    * Many 3D titles tend to hover around 80% at the moment and are fully CPU-bound.

### Wii

* Wii games require an overclock, the higher the better.
    * Some Wii titles, even with a max overclock, cannot hit fullspeed.

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
cmake --build build/switch --target nezumiruka_nro -j$(nproc)
```

Only the deko3d renderer is built by default. This should be the renderer you use as it is more 
performant than Vulkan in every case that I saw. 

If you want, pass `-DNEZUMIRUKA_VULKAN=ON` to also build the Vulkan renderer (requires NXVK).

This produces `build/switch/Binaries/nezumiruka.nro`. Copy it to `/switch/nezumiruka/` on the SD
card.

## Frame generation

Nezumiruka can use Lossless Scaling's frame generation to generate extra frames between the ones a game
renders. You must supply your own copy of `Lossless.dll`, which can be bought on [Steam](https://store.steampowered.com/app/993090/Lossless_Scaling/).

1. Copy `Lossless.dll` to `/switch/nezumiruka/FrameGeneration/Lossless.dll`.
2. In Nezumiruka, open Settings -> Frame generation and choose Prepare shaders. This will
   take a few minutes.
3. Turn on frame generation and pick a multiplier, flow scale, and performance mode.

Do note that frame generation is very resource intensive and will necessitate an overclock to get good
results out of. Also, currently only the Deko3D backend has a frame generation implementation.

## Credits
* Massive thanks to the Dolphin Emulator team for creating this amazing emulator. None of this
would be possible without their work.
* Borealis was used for the menus. Many thanks to those that created it.
* The fine folks over at DevkitPro that developed so much of the framework this project and the whole Switch scene depends on.
