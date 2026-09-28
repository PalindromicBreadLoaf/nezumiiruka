# Porpoise - A GameCube and Wii Emulator for Nintendo Switch Based on Dolphin

Porpoise is an emulator for running GameCube and Wii games on the Nintendo Switch.
It features a hand-written Deko3D backend to give the Switch as much of a fighting chance as possible.
It's licensed under the terms of the GNU General Public License version 2 or later (GPLv2+).

## System Requirements

### Gamecube

* Many 2D and some 3D games can run at fullspeed at stock clocks
    * Overclocking can help with shader compilation stutter
    * Many 3D titles tend to hover around 80% at the moment and are fully CPU-bound.

### Wii

* Wii games require an overclock, the higher the better.
    * Some Wii titles, even with a max overclock, cannot hit fullspeed.

## Building

Requires [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the `switch-dev` package
group installed, and `$DEVKITPRO` set (`/opt/devkitpro` by default).
`bison`, `flex` and `python3` must be available on the build host.

Make sure to pull submodules before building:
```shell
git submodule update --init --recursive
```

Two checkouts are expected alongside this one. NXVK (optional), which supplies the Vulkan driver, and my uam's fork.
```shell
git clone https://github.com/PalindromicBreadLoaf/uam ../uam
git clone https://github.com/PalindromicBreadLoaf/nxvk.git ../nxvk
```
Point `UAM_ROOT`/`NXVK_ROOT` elsewhere if they live somewhere other than `../`.

Build each of the above projects using their respective methods before proceeding.

```shell
cmake -S . -B build/switch \
    -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build/switch --target porpoise_nro -j$(nproc)
```

This produces `build/switch/Binaries/porpoise.nro`. Copy it to `/switch/porpoise/` on the SD
card.

## Credits
* Massive thanks to the Dolphin Emulator team for creating this amazing emulator. None of this
would be possible without their work.
* Borealis was used for the menus. Many thanks to those that created it.
* The fine folks over at DevkitPro that developed so much of the framework this project and the whole Switch scene depends on.
