#!/usr/bin/env bash
# Manual build script for BetterVR_Layer.so on Linux, standing in for the (not-yet-ported)
# CMake+vcpkg build system. Compiles every mod .cpp plus the vendored imgui/implot sources
# and links them into a Vulkan layer shared library. Run from anywhere:
#   ./build-linux/build.sh
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OBJDIR="$ROOT/build-linux/obj"
mkdir -p "$OBJDIR"

CXX="${CXX:-g++}"
STD=-std=c++23
# EXTRA_INCLUDES goes first so a fresher Vulkan-Headers checkout (see CI workflow) wins
# over whatever vulkan/vulkan.h the system package manager provides - dependencies/vkroots.h
# references very recent Vulkan extension types that an older system package won't have.
INCLUDES="${EXTRA_INCLUDES:-} -I $ROOT/include -I $ROOT/src -I $ROOT/build-deps/glm -I $ROOT/build-deps/imgui -I $ROOT/build-deps/implot -I $ROOT/build-deps/imgui_club/imgui_memory_editor -I $ROOT/dependencies"
PCH="-include $ROOT/include/pch.h"
COMMON_FLAGS="$STD -fPIC -O0 -g $INCLUDES"

# NOTE: build-deps/imgui/backends is deliberately NOT on the include path - pch.h's
# `#include <imgui_impl_vulkan.h>` must resolve to dependencies/imgui_impl_vulkan.h (the
# copy the project actually vendors/uses), not the identically-named stock one in
# build-deps/imgui/backends. Including both paths causes a duplicate-definition error.

MOD_SOURCES=(
    src/utils/logger.cpp
    src/utils/debug_draw.cpp
    src/hooking/framebuffer.cpp
    src/hooking/layer.cpp
    src/hooking/camera.cpp
    src/hooking/game_state.cpp
    src/hooking/settings.cpp
    src/hooking/profiler_hooks.cpp
    src/hooking/weapon.cpp
    src/hooking/imgui_menus.cpp
    src/hooking/controls.cpp
    src/hooking/entity_debugger.cpp
    src/hooking/rumble.cpp
    src/hooking/skeleton.cpp
    src/hooking/entity_controller.cpp
    src/hooking/bow.cpp
    src/rendering/vulkan2.cpp
    src/rendering/renderer.cpp
    src/rendering/openxr.cpp
    src/rendering/swapchain.cpp
    src/rendering/texture.cpp
    src/rendering/vulkan.cpp
    src/rendering/vulkan_imgui.cpp
)

THIRD_PARTY_SOURCES=(
    build-deps/imgui/imgui.cpp
    build-deps/imgui/imgui_draw.cpp
    build-deps/imgui/imgui_tables.cpp
    build-deps/imgui/imgui_widgets.cpp
    build-deps/implot/implot.cpp
    build-deps/implot/implot_items.cpp
)

FAILED=0
OBJS=()

compile_one() {
    local src="$1"
    shift
    local extra_flags="$*"
    local objname
    objname=$(echo "$src" | tr '/' '_')
    local obj="$OBJDIR/${objname}.o"
    echo "Compiling $src ..."
    if ! $CXX $COMMON_FLAGS $extra_flags $PCH -c "$ROOT/$src" -o "$obj" 2> "$OBJDIR/${objname}.log"; then
        echo "  FAILED: $src (see $OBJDIR/${objname}.log)"
        FAILED=1
    else
        OBJS+=("$obj")
    fi
}

for src in "${MOD_SOURCES[@]}"; do
    compile_one "$src"
done

for src in "${THIRD_PARTY_SOURCES[@]}"; do
    compile_one "$src"
done

# imgui_impl_vulkan.cpp needs IMGUI_IMPL_VULKAN_NO_PROTOTYPES (matching CMakeLists.txt)
compile_one "dependencies/imgui_impl_vulkan.cpp" "-DIMGUI_IMPL_VULKAN_NO_PROTOTYPES"

if [ "$FAILED" -ne 0 ]; then
    echo "=== Some files failed to compile, aborting link ==="
    exit 1
fi

echo "=== Linking BetterVR_Layer.so ==="
$CXX -shared -fPIC -o "$ROOT/build-linux/BetterVR_Layer.so" "${OBJS[@]}" -lvulkan -lopenxr_loader -lpthread -ldl 2> "$OBJDIR/link.log"
LINK_STATUS=$?
if [ $LINK_STATUS -ne 0 ]; then
    echo "=== LINK FAILED ==="
    cat "$OBJDIR/link.log"
    exit 1
fi

echo "=== LINK SUCCEEDED: $ROOT/build-linux/BetterVR_Layer.so ==="
ls -la "$ROOT/build-linux/BetterVR_Layer.so"
