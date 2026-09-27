# Linux port — technical notes

Root-cause writeups for the non-obvious bugs hit while porting this layer to Linux.
Reference material, not a status tracker — see [LINUX.md](../LINUX.md) for what's
currently confirmed working and what isn't.

## The HLE hook registration bug

One bug caused both "3D never renders" and a deterministic crash on entering gameplay:
the Linux port's `CemuHooks` was calling the wrong Cemu-internal function to register
its ~70 native gameplay hooks.

Cemu's real `osLib_registerHLEFunction(library, function, callback)` does two things:
adds the callback to a raw dispatch table (`PPCInterpreter_registerHLECall`), **and**
records the `(library, function)` name pair in a separate lookup table
(`osLib_addFunctionInternal`'s `s_osFunctionTable`) that graphic-pack ASM patches use to
resolve `$import.coreinit.hook_X`-style references to an actual address. The initial
port only did the first half - calling `PPCInterpreter_registerHLECall` directly, since
`osLib_registerHLEFunction` itself doesn't exist as a distinct symbol in this release
build (it's a trivial one-line wrapper that got inlined away). Every hook registered
successfully into the raw table but was never given a name, so every graphic-pack patch
referencing one by name failed to resolve - meaning none of the mod's ~70 gameplay hooks
ever actually fired, and the patches baked in garbage addresses that eventually crashed
the game's own job-queue system once real 3D gameplay started exercising them.

Fixed by resolving `osLib_addFunctionInternal` instead (found by reading Cemu's own
open-source `src/Cafe/OS/common/OSCommon.cpp` and `src/Cafe/OS/RPL/rpl.cpp`, then
confirming the mangled symbol exists in the release binary via `nm`) - same 3-argument
signature, does the complete job. See `src/hooking/cemu_hooks.h`.

Confirmed via direct instrumentation (temporarily traced every one of the 71 hook
functions to a file): zero hooks fired across a 6-minute session before the fix;
multiple different hooks fired within 10 seconds after it.

## Other non-obvious bugs found during the port

- **Upside-down image**: `src/shaders/present.vert`'s vertex shader negated Y when
  mapping UV to clip space - correct for D3D12 (Y-up in NDC), wrong for Vulkan (Y-down).
- **Vulkan layer self-interception deadlock**: this Vulkan layer applies itself to every
  instance/device in the process (`VK_INSTANCE_LAYERS` is process-wide), including
  Device B's own internal instance/device/images/queue submissions. Three
  self-reentrancy points fixed via `IsDeviceBInstance`/`IsDeviceBDevice` bypasses in
  `layer.cpp`/`framebuffer.cpp`.
- **Missing graphic pack**: the actual VR rendering is driven by game-side PPC patches
  (`resources/BreathOfTheWild_BetterVR/`) that have to be copied into Cemu's real
  `graphicPacks` folder manually - see `build-linux/install_graphic_pack.sh`.
- **Vulkan API version mismatch**: Device B's instance targeted 1.2; `vkCmdBeginRendering`
  needs 1.3 to resolve its core name reliably. Bumped to `VK_API_VERSION_1_3`.
- **`xrGetVulkanGraphicsDevice2KHR` misuse**: was being called on Cemu's own instance,
  which the "2KHR" entry point doesn't accept (needs an instance created via
  `xrCreateVulkanInstanceKHR`). Replaced with `VkPhysicalDeviceIDProperties::deviceUUID`
  comparison against Device B's already-correctly-selected physical device.
- **GCC/MSVC struct-layout ABI mismatch**: `BEVec3` and relatives silently grew from 12
  to 16 bytes under GCC (an empty-base-class address-uniqueness rule MSVC doesn't
  enforce the same way), throwing off every offset in the game-memory struct layouts
  that depend on them. Found and fixed at the root; all 68 memory-layout
  `static_assert`s pass unconditionally now.
- **Settings/log path resolution**: `BetterVR_settings.ini`/`BetterVR_log.txt` used to
  resolve relative to Cemu's process working directory, which an AppImage-launched Cemu
  doesn't reliably leave pointed at anything useful (confirmed: can be `/`). Both now
  self-locate next to the layer's own `.so` via `dladdr` instead - see
  `src/utils/linux_layer_dir.h`.

## Visual quality (render resolution, filtering, image clarity)

The game's own render resolution (Wii U native, 1280x720) was getting stretched hard
onto the Quest 3's much larger per-eye swapchain (~2476x2594), and the present
pipeline's sampler was set to `VK_FILTER_NEAREST`. Fixed by switching to
`VK_FILTER_LINEAR` in `RND_Vulkan2::PresentPipeline` (`src/rendering/vulkan2.cpp`) and
raising the game's actual render resolution via Cemu's own `Graphics` community graphic
pack (see LINUX.md's setup instructions) - the resolution bump matters more than the
filter change, since it addresses the actual source resolution rather than just
smoothing the upscale of a low-detail image.

## Known, deliberately deferred gaps

- No real Wayland/X11 window handle discovery - desktop-window-focused features only.
- Entity debugger's keyboard-driven text entry is a no-op on Linux - debug-only.
- No Vulkan debug-draw overlay pipeline - never ported from D3D12.
- Pre-existing, harmless: `BootDirectlyTitleId=` logs a "missing value" parse warning on
  every launch. Cosmetic only.
