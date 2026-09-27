#!/usr/bin/env bash
# Launches Cemu + BOTW with the Linux-ported BetterVR Vulkan layer forced on.
# Needs: the WiVRn service running (systemctl --user status wivrn) AND the Quest 3
# actively connected/streaming - without a connected headset this will get exactly as
# far as OpenXR instance/extension enumeration and then block waiting for the compositor
# (confirmed safe/expected behavior, not a crash - see build-linux/PORTING_NOTES.md).
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# These three almost certainly need to change for your own setup - either edit them
# directly below, or export the same-named variable before running this script so
# your own values win instead. See LINUX.md for how to find each of these on your
# own machine.
GAME_RPX="${BETTERVR_GAME_RPX:-/home/dave/Documents/fin/BOTW/game/The Legend of Zelda Breath of the Wild [Game] [00050000101c9400]/code/U-King.rpx}"
CEMU_APPIMAGE="${BETTERVR_CEMU_APPIMAGE:-$HOME/Applications/Cemu/Cemu-2.6-x86_64.AppImage}"
XR_RUNTIME_JSON_PATH="${BETTERVR_XR_RUNTIME_JSON:-/usr/share/openxr/1/openxr_wivrn.json}"

if [ ! -f "$GAME_RPX" ]; then
    echo "Game RPX not found at: $GAME_RPX"
    echo "Set BETTERVR_GAME_RPX to your own game's U-King.rpx path (see LINUX.md)."
    exit 1
fi
if [ ! -f "$CEMU_APPIMAGE" ]; then
    echo "Cemu AppImage not found at: $CEMU_APPIMAGE"
    echo "Set BETTERVR_CEMU_APPIMAGE to your own Cemu AppImage path (see LINUX.md)."
    exit 1
fi
if [ ! -f "$XR_RUNTIME_JSON_PATH" ]; then
    echo "OpenXR runtime manifest not found at: $XR_RUNTIME_JSON_PATH"
    echo "This is the path WiVRn's package installs on Arch/EndeavourOS - other"
    echo "distros may put it elsewhere. Set BETTERVR_XR_RUNTIME_JSON, or unset this"
    echo "check entirely if your system's default OpenXR runtime is already WiVRn."
    exit 1
fi

if [ ! -f "$HERE/BetterVR_Layer.so" ]; then
    echo "BetterVR_Layer.so not built yet - run build-linux/build.sh first"
    exit 1
fi

export VK_LAYER_PATH="$HERE"
export VK_INSTANCE_LAYERS="VK_LAYER_CREMENTIF_bettervr"
export ENABLE_BETTERVR_MOD=1
export XR_RUNTIME_JSON="$XR_RUNTIME_JSON_PATH"
# The layer finds its own settings.ini/log.txt next to its own .so on disk (it locates
# itself with dladdr), so no need to set BETTERVR_SETTINGS_PATH/BETTERVR_LOG_PATH or
# manage the process cwd here - both still work as manual overrides if you ever want one.

# Cemu's Cubeb audio backend just plays to whatever PipeWire's current default sink
# is (its own TVDevice setting is left blank = system default) - without this it goes
# to the desktop's own speakers/DAC instead of the headset, since WiVRn's virtual
# "wivrn.sink" only receives audio that's explicitly routed to it. This stays the
# default after Cemu exits too (not restored), which is fine here since you generally
# want notification sounds etc. going to the headset while it's the active output.
pactl set-default-sink wivrn.sink 2>/dev/null || echo "Warning: couldn't set WiVRn as the default audio sink - is WiVRn running?"

echo "Launching Cemu + BOTW with BetterVR_Layer.so active..."
echo "BetterVR log: $BETTERVR_LOG_PATH"
exec "$CEMU_APPIMAGE" -g "$GAME_RPX"
