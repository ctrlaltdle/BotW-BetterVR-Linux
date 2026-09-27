#pragma once
// Linux-only: resolves symbols in the main Cemu executable that exist in its
// (unstripped) ELF symbol table but are NOT in the dynamic symbol table, so
// dlsym() cannot find them. This is the case for every symbol BetterVR's
// Windows build resolves via GetProcAddress(GetModuleHandleA(NULL), ...) -
// those are exported cleanly on the Windows build (likely via explicit
// extern "C" dllexport specifically for external tool/mod support), but the
// Linux build never exports them dynamically, even though the underlying
// symbols genuinely exist in the binary (confirmed via `nm`/`readelf`).
//
// Technique: parse Cemu's own ELF file from disk (found via /proc/self/exe,
// since this code runs loaded inside the Cemu process as a Vulkan layer),
// locate the symbol in .symtab/.strtab, and add the process's actual ASLR
// load bias (from /proc/self/maps) to get a real, callable runtime address.
// This is the same technique tools like Frida use for unexported symbols -
// it is fragile across Cemu builds/versions (a recompile can move or rename
// symbols) but works reliably for a specific known binary, which is exactly
// our situation (Cemu 2.6 Linux AppImage).

#ifdef __linux__

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <elf.h>
#include <fstream>
#include <optional>
#include <unistd.h>

class LinuxSymbolResolver {
public:
    static LinuxSymbolResolver& instance() {
        static LinuxSymbolResolver singleton;
        return singleton;
    }

    // Returns the real runtime address of `symbolName` inside the main executable,
    // or nullptr if it can't be found. Cached after first successful lookup.
    void* Resolve(const std::string& symbolName) {
        if (!m_initialized) {
            Initialize();
        }

        auto it = m_symbolOffsets.find(symbolName);
        if (it == m_symbolOffsets.end()) {
            return nullptr;
        }
        return reinterpret_cast<void*>(m_loadBase + it->second);
    }

private:
    bool m_initialized = false;
    uint64_t m_loadBase = 0;
    std::unordered_map<std::string, uint64_t> m_symbolOffsets; // name -> link-time vaddr

    void Initialize() {
        m_initialized = true;

        char exePath[4096] = {};
        ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        if (len <= 0) {
            fprintf(stderr, "[LinuxSymbolResolver] Failed to resolve /proc/self/exe\n");
            return;
        }
        exePath[len] = '\0';

        m_loadBase = FindLoadBase(exePath);
        if (m_loadBase == 0) {
            fprintf(stderr, "[LinuxSymbolResolver] Failed to find load base of %s in /proc/self/maps\n", exePath);
            return;
        }

        if (!ParseSymtab(exePath)) {
            fprintf(stderr, "[LinuxSymbolResolver] Failed to parse ELF symbol table of %s\n", exePath);
            return;
        }

        fprintf(stderr, "[LinuxSymbolResolver] Initialized: load base 0x%lx, %zu symbols indexed from %s\n",
            (unsigned long)m_loadBase, m_symbolOffsets.size(), exePath);
    }

    // Finds the lowest mapped virtual address for `path` in this process's own maps -
    // i.e. the actual runtime location the PIE was loaded at (ASLR base).
    static uint64_t FindLoadBase(const char* path) {
        std::ifstream maps("/proc/self/maps");
        if (!maps.is_open()) return 0;

        std::string line;
        while (std::getline(maps, line)) {
            if (line.find(path) == std::string::npos) continue;

            // line format: "<start>-<end> perms offset dev inode pathname"
            uint64_t start = std::stoull(line.substr(0, line.find('-')), nullptr, 16);
            return start;
        }
        return 0;
    }

    bool ParseSymtab(const char* path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        Elf64_Ehdr ehdr;
        file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
        if (!file || memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
            return false;
        }

        // Read section headers
        std::vector<Elf64_Shdr> sections(ehdr.e_shnum);
        file.seekg(ehdr.e_shoff);
        file.read(reinterpret_cast<char*>(sections.data()), sizeof(Elf64_Shdr) * ehdr.e_shnum);
        if (!file) return false;

        // Section header string table, to find sections by name
        std::vector<char> shstrtab(sections[ehdr.e_shstrndx].sh_size);
        file.seekg(sections[ehdr.e_shstrndx].sh_offset);
        file.read(shstrtab.data(), shstrtab.size());

        const Elf64_Shdr* symtabSection = nullptr;
        const Elf64_Shdr* strtabSection = nullptr;
        for (const auto& sec : sections) {
            const char* name = shstrtab.data() + sec.sh_name;
            if (strcmp(name, ".symtab") == 0) symtabSection = &sec;
            else if (strcmp(name, ".strtab") == 0) strtabSection = &sec;
        }

        if (!symtabSection || !strtabSection) {
            fprintf(stderr, "[LinuxSymbolResolver] No .symtab/.strtab - binary may be stripped\n");
            return false;
        }

        std::vector<char> strtab(strtabSection->sh_size);
        file.seekg(strtabSection->sh_offset);
        file.read(strtab.data(), strtab.size());

        size_t symCount = symtabSection->sh_size / sizeof(Elf64_Sym);
        std::vector<Elf64_Sym> symbols(symCount);
        file.seekg(symtabSection->sh_offset);
        file.read(reinterpret_cast<char*>(symbols.data()), symtabSection->sh_size);
        if (!file) return false;

        m_symbolOffsets.reserve(symCount);
        for (const auto& sym : symbols) {
            if (sym.st_name == 0 || sym.st_value == 0) continue;
            const char* name = strtab.data() + sym.st_name;
            m_symbolOffsets.emplace(name, sym.st_value);
        }

        return true;
    }
};

#endif // __linux__
