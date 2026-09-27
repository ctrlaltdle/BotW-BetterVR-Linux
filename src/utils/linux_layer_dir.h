#pragma once
// Linux-only: finds the directory BetterVR_Layer.so itself lives in, so files that
// belong next to it (BetterVR_settings.ini, BetterVR_log.txt) resolve there by default
// instead of depending on Cemu's process working directory - which an AppImage-launched
// Cemu does not reliably leave pointed at anything useful (confirmed: it can be "/").
// This replaces needing BETTERVR_SETTINGS_PATH/BETTERVR_LOG_PATH set correctly by
// whatever launched Cemu; those env vars still work as an override if set, but nothing
// breaks anymore if they aren't.

#ifdef __linux__

#include <dlfcn.h>
#include <libgen.h>
#include <string>
#include <cstring>
#include <vector>

inline std::string GetBetterVRLayerDir() {
    static const std::string dir = [] {
        Dl_info info{};
        // Any address inside this shared library works here - what matters is which
        // .so dladdr resolves it back to, not what the function itself does.
        if (dladdr(reinterpret_cast<void*>(&GetBetterVRLayerDir), &info) && info.dli_fname != nullptr) {
            std::string path = info.dli_fname;
            std::vector<char> buf(path.begin(), path.end());
            buf.push_back('\0');
            return std::string(dirname(buf.data()));
        }
        return std::string(".");
    }();
    return dir;
}

#endif
