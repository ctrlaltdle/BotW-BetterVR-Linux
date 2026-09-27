# Running BetterVR on Linux (unofficial)

I run Linux, and I really wanted to play BotW in VR. There wasn't a Linux port of
BetterVR, and I'm not much of a coder - so I figured I'd see if I could finagle
something together with AI and actually get it working. Turns out you can.
This is that: a Linux port of Crementif's BetterVR mod.

**This is not Crementif's work.** BetterVR itself - the years of reverse-engineering,
the PowerPC patches, the hand/weapon tracking, literally all of it - was built by
Crementif and the other credited contributors (see the main [README](README.md#credits)).
I just ported the Windows-only layer they built to run on Linux too. If this is useful
to you, go support Crementif instead of me:
[GitHub Sponsors](https://github.com/sponsors/Crementif) ·
[Patreon](https://www.patreon.com/crementif).

This Linux port itself was built with heavy AI assistance - I directed the work but
didn't write the code by hand.

I'm not actively monitoring this or promising any fixes. It works on my machine, right
now, as described below. I might come back and fix things if people run into issues,
I might not - no guarantees either way. Use it at your own risk.

## What's actually confirmed working

I've only tested this on one machine: EndeavourOS (Arch-based), KDE Plasma, Wayland, a
Quest 3 connected over [WiVRn](https://github.com/WiVRn/WiVRn), Cemu 2.6.

On that setup: stereo 3D rendering, head tracking, real Touch controller support
(weapon grip/pickup/throw, bow draw and fire, climbing), the in-game HUD, and audio
through the headset all work.

I have no idea if this works on Nvidia GPUs, X11 instead of Wayland, any OpenXR runtime
other than WiVRn, or any distro other than Arch-based ones - just haven't tried. There's
also an optional wrist-mounted HUD placement (`HudOnWrist` in the settings) that works
in principle but the position/angle is a guess I never got to tune properly - see Known
Rough Edges below if you want to mess with it.

## Requirements

- A Linux desktop with a working Vulkan driver (built and tested against Mesa RADV on
  AMD; should work on other Vulkan GPUs, not tried).
- [Cemu](https://cemu.info/) 2.6+, the Linux AppImage build.
- A "legal" copy of Breath of the Wild for Wii U, update V208 (see the main README's
  installation steps 3-4 for how to verify the version in Cemu).
- [WiVRn](https://github.com/WiVRn/WiVRn) set up and actively streaming to your headset,
  *before* you do any of this. If it's not running and connected, Cemu will hang after
  boot waiting for a compositor session - that's expected, not a crash.
- `g++` with C++23 support (GCC 13+), the Vulkan loader dev headers, and an OpenXR
  loader dev package (name varies by distro - `openxr-loader` on Arch). Everything else
  (glm, imgui, implot) is vendored in `build-deps/`, nothing else to install.

## Setup

1. **Build it.**
   ```bash
   ./build-linux/build.sh
   ```
   Produces `build-linux/BetterVR_Layer.so`.

2. **Install the graphic pack** - this is what actually drives the VR camera/rendering,
   nothing else here does anything without it.
   ```bash
   ./build-linux/install_graphic_pack.sh
   ```
   Copies `resources/BreathOfTheWild_BetterVR` into Cemu's default graphic packs
   folder. If your Cemu profile lives somewhere nonstandard, just do the copy yourself -
   the script prints exactly what it's doing.

3. **Enable the recommended graphic packs in Cemu.** Open Cemu, select BotW, go to
   `Options -> Graphic Packs` (download the community packs first if you haven't). Turn
   on:
   - `Better VR` - should already be on by default once step 2 is done.
   - `Mods/FPS++` - required, the game crashes without it. Push `FPS Limit` /
     `Framerate Limit` up to 120 if your headset does 120Hz - BetterVR's own patches
     want more than the 60 default.
   - `Graphics` - bump `Resolution` well past BotW's native 1280x720 (2560x1440 or
     3840x2160 both look good). Without this the game gets stretched hard onto the
     headset's per-eye view and looks noticeably blurry.
   - `Mods/Draw Distance` - nudge `Texture Distance Detail (LOD)` toward "Higher" if
     distant terrain looks soft. Separate knob from resolution above.
   - `Enhancements` - "Clarity" and "Anisotropic Filtering" both help image quality.
     Turn **off** "Depth of Field" specifically - it's a flat 2D blur that doesn't know
     about VR's real stereo depth and can look wrong / mildly nauseating in headset.
   - `Mods/Extended Memory` - cheap, no downside, just leave it on.

   All plain checkboxes/dropdowns in Cemu - no file editing needed.

4. **Point the launch script at your own paths and run it.**
   ```bash
   export BETTERVR_GAME_RPX="/path/to/your/game/code/U-King.rpx"
   export BETTERVR_CEMU_APPIMAGE="/path/to/Cemu-2.6-x86_64.AppImage"
   # only if WiVRn's manifest isn't at the Arch/EndeavourOS default already in the script:
   # export BETTERVR_XR_RUNTIME_JSON="/path/to/openxr_wivrn.json"

   ./build-linux/run_cemu_botw.sh
   ```
   Sets the Vulkan layer env vars, routes Cemu's audio to WiVRn's virtual sink so it
   reaches the headset instead of your desktop speakers, and launches the game. Headset
   on, WiVRn showing an active session, *before* you run this.

## Known rough edges

- **Only tested against WiVRn.** Other OpenXR runtimes (Monado standalone, SteamVR,
  ALVR) might work, might not - never tried them.
- **The wrist-mounted HUD is a first-pass guess.** `HudOnWrist` in
  `BetterVR_settings.ini` turns it on, but the offset/angle values (in
  `src/rendering/renderer.cpp`, `Layer2D::FinishRendering`) were never actually tuned in
  headset. If you try it and it's floating in a weird spot, that's why - the numbers are
  right there to change.
