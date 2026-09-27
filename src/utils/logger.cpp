#include "pch.h"

#include "logger.h"

#ifdef _WIN32
// FILE_APPEND_DATA without FILE_WRITE_DATA is what makes each write an atomic append, so the layer,
// the launcher and Cemu's stderr can share one file. Adding FILE_WRITE_DATA breaks that.
static constexpr DWORD kLogFileAccess = FILE_APPEND_DATA;
static constexpr DWORD kLogFileShare = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
#else
#include <fcntl.h>
#include <unistd.h>
#include <cpuid.h>
#include <sys/sysinfo.h>
#include <sys/syscall.h>
#include <ctime>
#include "linux_layer_dir.h"
#endif

static constexpr size_t kMaxQueuedRecords = 4096;
static constexpr uint32_t kUncleanShutdownTimeoutMs = 2000;
static constexpr uint32_t kFlushTimeoutMs = 500;

struct LogRecord {
    std::string text;
    LogType type = INFO;
    uint32_t threadId = 0;
    uint64_t time = 0; // Windows: FILETIME ticks. Linux: milliseconds since Unix epoch.
};

// Never destructed because a detached writer thread (see StopWriterThread) may still touch these
static std::mutex& s_queueMutex = *new std::mutex();
static std::condition_variable& s_wakeWriter = *new std::condition_variable();
static std::condition_variable& s_drained = *new std::condition_variable();
static std::thread s_writerThread;
static std::atomic_bool s_running = false;
static bool s_writerBusy = false;

// Reusing s_queueMutex here would self-deadlock with Start/StopWriterThread
static std::mutex s_transitionMutex;
static size_t s_instanceRefCount = 0;

// Sticky: once a writer is detached, never start another one
static constinit std::atomic_bool s_writerDetached = false;

static std::vector<LogRecord>& s_produce = *new std::vector<LogRecord>();
static std::vector<LogRecord>& s_consume = *new std::vector<LogRecord>();
static size_t s_produceCount = 0;
static uint32_t s_droppedCount = 0;

static std::string& s_batchBuffer = *new std::string();
static std::string& s_directBuffer = *new std::string();

#ifdef _WIN32
static HANDLE s_fileHandle = INVALID_HANDLE_VALUE;
static HANDLE s_consoleHandle = NULL;
static double s_timeFrequency = 0.0;
static uint32_t s_processId = GetCurrentProcessId();
#else
static int s_fileFd = -1;
static uint32_t s_processId = (uint32_t)getpid();
#endif

static constinit std::atomic_bool s_showTimestamps = true;
static constinit std::atomic_bool s_showThreadIds = false;

static uint64_t GetTimeNow() {
#ifdef _WIN32
    FILETIME systemTime;
    GetSystemTimeAsFileTime(&systemTime);
    return ((uint64_t)systemTime.dwHighDateTime << 32) | systemTime.dwLowDateTime;
#else
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
#endif
}

static uint32_t GetCurrentThreadIdPortable() {
#ifdef _WIN32
    return GetCurrentThreadId();
#else
    return (uint32_t)syscall(SYS_gettid);
#endif
}

static const char* GetLogTypeName(LogType type) {
    switch (type) {
        case RENDERING: return "RENDER";
        case INTEROP: return "INTEROP";
        case CONTROLS: return "CONTROL";
        case PPC: return "PPC";
        case XR_DEBUGUTILS: return "XR";
        case ARROW_SHOT_CAPTURE: return "ARROW";
        case ROOMSCALE: return "ROOMSCALE";
        case INFO: return "INFO";
        case WARNING: return "WARN";
        case ERROR: return "ERROR";
        case VERBOSE: return "VERBOSE";
    }
    return "?";
}

static void FillRecord(LogRecord& record, LogType type, std::string_view message) {
    record.text.assign(message);
    record.type = type;
    record.threadId = GetCurrentThreadIdPortable();
    record.time = GetTimeNow();
}

static void AppendRecord(std::string& batch, const LogRecord& record) {
    if (s_showTimestamps.load(std::memory_order_relaxed)) {
#ifdef _WIN32
        FILETIME utcTime;
        utcTime.dwLowDateTime = (DWORD)(record.time & 0xFFFFFFFFull);
        utcTime.dwHighDateTime = (DWORD)(record.time >> 32);

        FILETIME localFileTime;
        SYSTEMTIME localTime;
        if (FileTimeToLocalFileTime(&utcTime, &localFileTime) && FileTimeToSystemTime(&localFileTime, &localTime)) {
            std::format_to(std::back_inserter(batch), "[{:02}:{:02}:{:02}.{:03}] ", localTime.wHour, localTime.wMinute, localTime.wSecond, localTime.wMilliseconds);
        }
#else
        time_t seconds = (time_t)(record.time / 1000);
        int milliseconds = (int)(record.time % 1000);
        struct tm localTm{};
        localtime_r(&seconds, &localTm);
        std::format_to(std::back_inserter(batch), "[{:02}:{:02}:{:02}.{:03}] ", localTm.tm_hour, localTm.tm_min, localTm.tm_sec, milliseconds);
#endif
    }

    if (s_showThreadIds.load(std::memory_order_relaxed)) {
        std::format_to(std::back_inserter(batch), "[{}:{}] ", s_processId, record.threadId);
    }

    std::format_to(std::back_inserter(batch), "[{:<7}] ", GetLogTypeName(record.type));
    batch.append(record.text);
    batch.append("\n");
}

static void WriteBatch(const std::string& batch) {
    if (batch.empty()) {
        return;
    }

#ifdef _WIN32
    if (s_fileHandle != INVALID_HANDLE_VALUE) {
        DWORD bytesWritten = 0;
        WriteFile(s_fileHandle, batch.data(), (DWORD)batch.size(), &bytesWritten, nullptr);
    }

    if (s_consoleHandle != NULL && s_consoleHandle != INVALID_HANDLE_VALUE) {
        DWORD charsWritten = 0;
        WriteConsoleA(s_consoleHandle, batch.data(), (DWORD)batch.size(), &charsWritten, NULL);
    }

#ifdef _DEBUG
    OutputDebugStringA(batch.c_str());
#endif
#else
    if (s_fileFd != -1) {
        // O_APPEND write() is atomic for writes this size, matching the FILE_APPEND_DATA
        // semantics the Windows path relies on to let multiple processes share one file.
        ssize_t written = write(s_fileFd, batch.data(), batch.size());
        (void)written;
    }

    ssize_t consoleWritten = write(STDOUT_FILENO, batch.data(), batch.size());
    (void)consoleWritten;
#endif
}

static void DrainQueue() {
    while (true) {
        size_t count = 0;
        uint32_t dropped = 0;
        {
            std::lock_guard<std::mutex> lock(s_queueMutex);
            if (s_produceCount == 0) {
                s_writerBusy = false;
                break;
            }

            s_produce.swap(s_consume);
            count = s_produceCount;
            s_produceCount = 0;
            dropped = s_droppedCount;
            s_droppedCount = 0;
            s_writerBusy = true;
        }

        s_batchBuffer.clear();
        for (size_t index = 0; index < count; ++index) {
            AppendRecord(s_batchBuffer, s_consume[index]);
        }

        if (dropped > 0) {
            LogRecord droppedRecord;
            FillRecord(droppedRecord, WARNING, std::format("... {} log messages dropped, queue was full", dropped));
            AppendRecord(s_batchBuffer, droppedRecord);
        }

        WriteBatch(s_batchBuffer);
    }

    s_drained.notify_all();
}

static void WriterThreadMain() {
    while (s_running.load(std::memory_order_relaxed)) {
        {
            std::unique_lock<std::mutex> lock(s_queueMutex);
            s_wakeWriter.wait(lock, [] { return s_produceCount > 0 || !s_running.load(std::memory_order_relaxed); });
        }
        DrainQueue();
    }

    DrainQueue();
}

static void FlushBlocking(uint32_t timeoutMs) {
    std::unique_lock<std::mutex> lock(s_queueMutex);
    if (!s_running.load(std::memory_order_relaxed)) {
        return;
    }
    s_drained.wait_for(lock, std::chrono::milliseconds(timeoutMs), [] { return s_produceCount == 0 && !s_writerBusy; });
}

static void OpenLogFile() {
#ifdef _WIN32
    // Deliberately relative: Cemu's working directory on Windows is the launcher
    // directory, which is what puts the layer and the launcher in one shared file.
    std::wstring logPath = L"BetterVR_log.txt";
    if (const wchar_t* overridePath = _wgetenv(L"BETTERVR_LOG_PATH"); overridePath != nullptr && overridePath[0] != L'\0') {
        logPath = overridePath;
    }

    s_fileHandle = CreateFileW(logPath.c_str(), kLogFileAccess, kLogFileShare, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
#else
    // Resolved next to the layer's own .so rather than trusting Cemu's process cwd -
    // see the comment on GetSettingsIniPath() in mod_settings.h for why.
    std::string logPath = GetBetterVRLayerDir() + "/BetterVR_log.txt";
    if (const char* overridePath = std::getenv("BETTERVR_LOG_PATH"); overridePath != nullptr && overridePath[0] != '\0') {
        logPath = overridePath;
    }

    s_fileFd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
#endif
}

static void StartWriterThread() {
    std::lock_guard<std::mutex> lock(s_queueMutex);
    if (s_running.load(std::memory_order_relaxed)) {
        return;
    }

    s_produce.reserve(512);
    s_consume.reserve(512);
    s_running.store(true, std::memory_order_relaxed);
    s_writerThread = std::thread(WriterThreadMain);
}

static bool StopWriterThread(uint32_t timeoutMs) {
    std::thread writerThread;
    {
        std::scoped_lock lock(s_queueMutex);
        if (!s_running.load(std::memory_order_relaxed)) {
            return true;
        }
        s_running.store(false, std::memory_order_relaxed);
        writerThread = std::move(s_writerThread);
    }

    s_wakeWriter.notify_all();

    if (!writerThread.joinable()) {
        return true;
    }

#ifdef _WIN32
    if (timeoutMs == INFINITE) {
        writerThread.join();
        return true;
    }

    // A timeout here means we're holding the loader lock, not that the writer is stuck. Detach instead of blocking forever.
    if (WaitForSingleObject(writerThread.native_handle(), timeoutMs) == WAIT_OBJECT_0) {
        writerThread.join();
        return true;
    }

    writerThread.detach();
    return false;
#else
    // The Windows detach-on-timeout path above exists to dodge a DLL loader-lock deadlock
    // that has no equivalent here (this .so is loaded as a Vulkan layer, not from DllMain
    // at process attach/detach time). A plain blocking join is safe on Linux and keeps
    // std::thread's internal bookkeeping intact (pthread_timedjoin_np + detach() would double-reap).
    (void)timeoutMs;
    writerThread.join();
    return true;
#endif
}


static bool ShutdownLogging(uint32_t timeoutMs) {
    if (!s_running.load(std::memory_order_relaxed)) {
        return true;
    }

    Log::print<INFO>("Pausing BetterVR log writer (no active Vulkan instances)...");

    if (!StopWriterThread(timeoutMs)) {
        s_writerDetached.store(true, std::memory_order_relaxed);
        return false;
    }

    DrainQueue();

    {
        std::scoped_lock lock(s_queueMutex);
#ifdef _WIN32
        if (s_fileHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(s_fileHandle);
            s_fileHandle = INVALID_HANDLE_VALUE;
        }
#else
        if (s_fileFd != -1) {
            close(s_fileFd);
            s_fileFd = -1;
        }
#endif
    }

    return true;
}

static void LogSystemHardwareInfo() {
#ifdef _WIN32
    int cpuInfo[4] = {0, 0, 0, 0};
    __cpuid(cpuInfo, 0x80000000);
    const unsigned int maxExId = static_cast<unsigned int>(cpuInfo[0]);

    std::string cpuBrand;
    if (maxExId >= 0x80000004) {
        char brand[49] = {};
        __cpuid(reinterpret_cast<int*>(brand + 0), 0x80000002);
        __cpuid(reinterpret_cast<int*>(brand + 16), 0x80000003);
        __cpuid(reinterpret_cast<int*>(brand + 32), 0x80000004);
        cpuBrand = brand;
        while (!cpuBrand.empty() && cpuBrand.front() == ' ') cpuBrand.erase(cpuBrand.begin());
        while (!cpuBrand.empty() && cpuBrand.back() == ' ') cpuBrand.pop_back();
        if (!cpuBrand.empty()) {
            Log::print<INFO>("CPU: {}", cpuBrand.c_str());
        }
    }

    MEMORYSTATUSEX statex{};
    statex.dwLength = sizeof(statex);
    if (GlobalMemoryStatusEx(&statex)) {
        const double totalGiB = double(statex.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
        Log::print<INFO>("RAM: {:.2f} GiB", totalGiB);
    }
#else
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    unsigned int maxExId = 0;
    if (__get_cpuid(0x80000000, &eax, &ebx, &ecx, &edx)) {
        maxExId = eax;
    }

    std::string cpuBrand;
    if (maxExId >= 0x80000004) {
        char brand[49] = {};
        for (unsigned int i = 0; i < 3; ++i) {
            __get_cpuid(0x80000002 + i, &eax, &ebx, &ecx, &edx);
            std::memcpy(brand + i * 16 + 0, &eax, 4);
            std::memcpy(brand + i * 16 + 4, &ebx, 4);
            std::memcpy(brand + i * 16 + 8, &ecx, 4);
            std::memcpy(brand + i * 16 + 12, &edx, 4);
        }
        cpuBrand = brand;
        while (!cpuBrand.empty() && cpuBrand.front() == ' ') cpuBrand.erase(cpuBrand.begin());
        while (!cpuBrand.empty() && cpuBrand.back() == ' ') cpuBrand.pop_back();
        if (!cpuBrand.empty()) {
            Log::print<INFO>("CPU: {}", cpuBrand.c_str());
        }
    }

    struct sysinfo memInfo{};
    if (sysinfo(&memInfo) == 0) {
        const double totalGiB = double(memInfo.totalram) * double(memInfo.mem_unit) / (1024.0 * 1024.0 * 1024.0);
        Log::print<INFO>("RAM: {:.2f} GiB", totalGiB);
    }
#endif
}

void Log::submit(LogType type, std::string_view message) {
    {
        std::scoped_lock lock(s_queueMutex);

        if (!s_running.load(std::memory_order_relaxed)) {
            // Reopening is safe to redo on every call: OPEN_ALWAYS + FILE_APPEND_DATA on Windows,
            // O_CREAT | O_APPEND on Linux.
#ifdef _WIN32
            if (s_fileHandle == INVALID_HANDLE_VALUE) {
                OpenLogFile();
            }
#else
            if (s_fileFd == -1) {
                OpenLogFile();
            }
#endif

            LogRecord record;
            FillRecord(record, type, message);

            s_directBuffer.clear();
            AppendRecord(s_directBuffer, record);
            WriteBatch(s_directBuffer);
            return;
        }

        if (s_produceCount >= kMaxQueuedRecords) {
            s_droppedCount++;
            return;
        }

        if (s_produceCount >= s_produce.size()) {
            s_produce.emplace_back();
        }

        FillRecord(s_produce[s_produceCount++], type, message);
    }

    s_wakeWriter.notify_one();
}

Log::Log() {
#ifdef _WIN32
    AllocConsole();
    SetConsoleTitleA("BetterVR Debugging Console");
    s_consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);

    LARGE_INTEGER timeLI;
    QueryPerformanceFrequency(&timeLI);
    s_timeFrequency = double(timeLI.QuadPart) / 1000.0;
#endif
    // Linux: no AllocConsole/SetConsoleTitleA equivalent needed - Cemu already has whatever
    // terminal launched it, and WriteBatch() below writes straight to STDOUT_FILENO.

    OpenLogFile();
    StartWriterThread();

    Log::print<INFO>("Successfully started BetterVR!");
    LogSystemHardwareInfo();
}

Log::~Log() {
    const bool cleanShutdown = ShutdownLogging(kUncleanShutdownTimeoutMs);
    if (!cleanShutdown || s_writerDetached.load(std::memory_order_relaxed)) {
        return;
    }

    {
        std::scoped_lock lock(s_queueMutex);
#ifdef _WIN32
        if (s_fileHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(s_fileHandle);
            s_fileHandle = INVALID_HANDLE_VALUE;
        }
        s_consoleHandle = NULL;
#else
        if (s_fileFd != -1) {
            close(s_fileFd);
            s_fileFd = -1;
        }
#endif
    }

#ifdef _WIN32
    FreeConsole();
#endif
}

void Log::OnInstanceCreated() {
    std::scoped_lock lock(s_transitionMutex);
    if (s_instanceRefCount++ != 0) {
        return;
    }

    if (s_writerDetached.load(std::memory_order_relaxed)) {
        return;
    }

    {
        std::scoped_lock lock(s_queueMutex);
#ifdef _WIN32
        if (s_fileHandle == INVALID_HANDLE_VALUE) {
            OpenLogFile();
        }
#else
        if (s_fileFd == -1) {
            OpenLogFile();
        }
#endif
    }
    StartWriterThread();
}

void Log::OnInstanceDestroyed() {
    std::scoped_lock lock(s_transitionMutex);
    if (s_instanceRefCount == 0) {
        return;
    }
    if (--s_instanceRefCount != 0) {
        return;
    }

    ShutdownLogging(kUncleanShutdownTimeoutMs);
}

void Log::SetShowTimestamps(bool enabled) {
    s_showTimestamps.store(enabled, std::memory_order_relaxed);
}

void Log::SetShowThreadIds(bool enabled) {
    s_showThreadIds.store(enabled, std::memory_order_relaxed);
}

void Log::Flush() {
    FlushBlocking(kFlushTimeoutMs);
}

#ifdef _WIN32
void Log::printTimeElapsed(const char* message_prefix, LARGE_INTEGER time) {
    LARGE_INTEGER timeNow;
    QueryPerformanceCounter(&timeNow);
    Log::print<INFO>("{}: {} ms", message_prefix, (double)(time.QuadPart - timeNow.QuadPart) / s_timeFrequency);
}
#endif
