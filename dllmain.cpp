//#define WIN32_LEAN_AND_MEAN
#include "hDid.h"
#include "mon.h"
#include "psapi.h"
#include "lockfree_hashmap.h"
#include "extension_hash_table.h"
#include "async_logger.h"
#include "memory_mapped_pool.h"

#include <malloc.h>
#include <map>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <shlwapi.h>
#include <chrono>
#include <algorithm>
#include <cstdarg>
#include <unordered_map>
#include <vector>
#include <winbase.h>
#include <atomic>
#include <thread>
#include <array>
#include <shared_mutex>
#include <memory>

// ============================================================================
// PHASE 1: CRITICAL BOTTLENECKS - LOCK-FREE DATA STRUCTURES
// ============================================================================

// Lock-free file handle cache with improved memory management
static LockFreeHashMap<HANDLE, std::wstring> fileHandleCache;

// Lock-free bytes tracking cache with cumulative tracking
struct FileBytes {
    DWORD readBytes;
    DWORD writeBytes;
    FileBytes() : readBytes(0), writeBytes(0) {}
    FileBytes(DWORD read, DWORD write) : readBytes(read), writeBytes(write) {}
    
    FileBytes& operator+=(const FileBytes& other) {
        readBytes += other.readBytes;
        writeBytes += other.writeBytes;
        return *this;
    }
};
static LockFreeHashMap<std::wstring, FileBytes> fileBytesCache;

// Pre-computed extension table
static ExtensionHashTable g_extensionTable;

// Async logger for non-blocking I/O
static AsyncLogger* g_logger = nullptr;

// Process name cache with read-write lock and improved error handling
class ProcessNameCache {
private:
    std::unordered_map<DWORD, std::wstring> processCache;
    mutable std::shared_mutex cacheMutex;
    std::atomic<size_t> cacheHits{0};
    std::atomic<size_t> cacheMisses{0};
    
public:
    std::wstring getProcessName(DWORD processId) {
        // Try read lock first (multiple readers)
        {
            std::shared_lock<std::shared_mutex> lock(cacheMutex);
            auto it = processCache.find(processId);
            if (it != processCache.end()) {
                cacheHits.fetch_add(1, std::memory_order_relaxed);
                return it->second;
            }
        }
        
        cacheMisses.fetch_add(1, std::memory_order_relaxed);
        
        // Write lock only when cache miss
        std::unique_lock<std::shared_mutex> lock(cacheMutex);
        
        // Double-check after acquiring write lock
        auto it = processCache.find(processId);
        if (it != processCache.end()) {
            return it->second;
        }
        
        // Get process name and cache it
        std::wstring processName = getProcessNameFromSystem(processId);
        processCache[processId] = processName;
        return processName;
    }
    
    size_t getCacheHits() const {
        return cacheHits.load(std::memory_order_relaxed);
    }
    
    size_t getCacheMisses() const {
        return cacheMisses.load(std::memory_order_relaxed);
    }
    
    double getHitRate() const {
        size_t hits = getCacheHits();
        size_t misses = getCacheMisses();
        size_t total = hits + misses;
        if (total == 0) return 0.0;
        return static_cast<double>(hits) / total;
    }
    
    void clear() {
        std::unique_lock<std::shared_mutex> lock(cacheMutex);
        processCache.clear();
        cacheHits.store(0, std::memory_order_relaxed);
        cacheMisses.store(0, std::memory_order_relaxed);
    }
    
private:
    std::wstring getProcessNameFromSystem(DWORD processId) {
        // For the current process, use GetModuleFileNameW
        if (processId == GetCurrentProcessId()) {
            wchar_t processPath[MAX_PATH];
            if (GetModuleFileNameW(NULL, processPath, MAX_PATH)) {
                std::wstring fullPath = processPath;
                size_t lastSlash = fullPath.find_last_of(L"\\/");
                if (lastSlash != std::wstring::npos) {
                    return fullPath.substr(lastSlash + 1);
                }
                return fullPath;
            }
        }
        
        // For other processes, try to get the name from the process handle
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (hProcess) {
            wchar_t processPath[MAX_PATH];
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW(hProcess, 0, processPath, &size)) {
                CloseHandle(hProcess);
                std::wstring fullPath = processPath;
                size_t lastSlash = fullPath.find_last_of(L"\\/");
                if (lastSlash != std::wstring::npos) {
                    return fullPath.substr(lastSlash + 1);
                }
                return fullPath;
            }
            CloseHandle(hProcess);
        }
        
        return L"unknown.exe";
    }
};

// ============================================================================
// GLOBAL INSTANCES
// ============================================================================

// Process name cache
static ProcessNameCache g_processCache;

// Windows XP compatibility
bool g_isWindowsXP = false;

// Pre-allocated buffers for string operations
char g_narrowBuffer[1024];
wchar_t g_wideBuffer[512];

// Performance monitoring
struct PerformanceStats {
    std::atomic<size_t> totalFileOperations{0};
    std::atomic<size_t> monitoredFileOperations{0};
    std::atomic<size_t> cacheHits{0};
    std::atomic<size_t> cacheMisses{0};
    std::atomic<size_t> writeErrors{0};
    std::atomic<size_t> readErrors{0};
};
static PerformanceStats g_perfStats;



// ============================================================================
// HOOK FUNCTIONS - REQUIRED FOR API HOOKING
// ============================================================================

// Process Memory Access with improved error handling
BOOL WriteCurrentProcessMemory(DWORD* ptr, unsigned char* buf, unsigned int len)
{
    DWORD flOldProtect, v4;
    
    BOOL result = 0;

    if (!PRS)
    {
        CPID = GetCurrentProcessId();
        PRS = OpenProcess(0x1F0FFFu, 0, CPID);
        if (!PRS) {
            return FALSE;
        }
    }

    if (!VirtualProtectEx(PRS, ptr, len, 4u, &flOldProtect)) {
        return FALSE;
    }
    
    if (!WriteProcessMemory(PRS, (LPVOID)ptr, (LPCVOID)buf, (SIZE_T)len, (SIZE_T*)&v4)) {
        VirtualProtectEx(PRS, ptr, len, flOldProtect, &v4);
        return FALSE;
    }
    
    result = VirtualProtectEx(PRS, ptr, len, flOldProtect, &v4);
    
    return result;
}

bool HookFuncCalls(SDLLHook* Hook)
{
    SFunctionHook* FHook = Hook->Functions;
    
    while (FHook->HookFn)
    {
        if (!FHook->isHook)
        {
            FHook++; continue;
        }
        
        DWORD* funcPtr;

        if(FHook->OrigFn)
        {
            funcPtr = (DWORD*)FHook->OrigFn;
        }else
        {
            if (!FHook->DllName)
            {
                return false;
            }
            HMODULE hModDLL = GetModuleHandle(FHook->DllName);
            if (!hModDLL)
            {
                FHook++; continue;
            }
            
            funcPtr = (DWORD*)GetProcAddress(hModDLL, FHook->Name);
            if (!funcPtr)
            {
                FHook++; continue;
            }
        }
        
        DWORD opcordLen = FHook->opcordLen;
        
        // These next few lines ensure that we'll be able to modify the IAT,
        // which is often in a read-only section in the EXE.
        DWORD flOldProtect, flNewProtect, flDontCare;
        MEMORY_BASIC_INFORMATION mbi;
        
        // Get the current protection attributes                            
        if (!VirtualQuery(funcPtr, &mbi, sizeof(mbi))) {
            FHook++; continue;
        }
        
        // remove ReadOnly and ExecuteRead attributes, add on ReadWrite flag
        flNewProtect = mbi.Protect;
        flNewProtect &= ~(PAGE_READONLY | PAGE_EXECUTE_READ);
        flNewProtect |= (PAGE_READWRITE);
        
        if (!VirtualProtect(funcPtr, opcordLen, flNewProtect, &flOldProtect)) {
            FHook++; continue;
        }
        
        FHook->opcord = (unsigned char*)malloc(opcordLen);
        if (!FHook->opcord) {
            VirtualProtect(funcPtr, opcordLen, flOldProtect, &flDontCare);
            FHook++; continue;
        }
        
        memcpy(FHook->opcord, funcPtr, opcordLen);
        
        DWORD* HookfuncPtr = (DWORD*)FHook->HookFn;

        memset(funcPtr, 0x90, opcordLen);
        *(unsigned char*)(funcPtr) = 0xE9;
        *(DWORD*)((unsigned char*)(funcPtr)+1) = (int)((int)HookfuncPtr - (int)funcPtr - 5);
        
        FHook->Hookopcord = (unsigned char*)malloc(opcordLen);
        if (!FHook->Hookopcord) {
            free(FHook->opcord);
            FHook->opcord = nullptr;
            VirtualProtect(funcPtr, opcordLen, flOldProtect, &flDontCare);
            FHook++; continue;
        }
        
        memcpy(FHook->Hookopcord, funcPtr, opcordLen);
        
        FHook->OrigFn = funcPtr;
        
        // Put the page attributes back the way they were.
        VirtualProtect(funcPtr, opcordLen, flOldProtect, &flDontCare);
        
        FHook++;
    }
    
    return true;
}

bool HookOffFuncCalls(SDLLHook* Hook)
{
    SFunctionHook* FHook = Hook->Functions;
    
    while (FHook->HookFn)
    {
        if (!FHook->isHook)
        {
            FHook++; continue;
        }
        
        if (FHook->opcord) {
            WriteCurrentProcessMemory((DWORD*)FHook->OrigFn, FHook->opcord, FHook->opcordLen);
        }
        
        FHook++;
    }
    return true;
}

void HookOff(SDLLHook* hook, int Funcindex)
{
    if (hook->Functions[Funcindex].opcord) {
        WriteCurrentProcessMemory((DWORD*)hook->Functions[Funcindex].OrigFn, 
                                 hook->Functions[Funcindex].opcord, 
                                 hook->Functions[Funcindex].opcordLen);
    }
}

void HookOn(SDLLHook* hook, int Funcindex)
{
    if (hook->Functions[Funcindex].Hookopcord) {
        WriteCurrentProcessMemory((DWORD*)hook->Functions[Funcindex].OrigFn, 
                                 hook->Functions[Funcindex].Hookopcord, 
                                 hook->Functions[Funcindex].opcordLen);
    }
}

// ============================================================================
// OPTIMIZED UTILITY FUNCTIONS
// ============================================================================

// Fast string conversion without allocation
inline void FastWideToNarrow(const std::wstring& wide, char* buffer, size_t bufferSize) {
    if (wide.empty()) {
        buffer[0] = '\0';
        return;
    }
    
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, buffer, (int)bufferSize, NULL, NULL);
    if (len <= 0) {
        buffer[0] = '\0';
    }
}

// Fast lowercase conversion without std::transform
inline void FastToLower(std::wstring& str) {
    for (size_t i = 0; i < str.length(); i++) {
        wchar_t c = str[i];
        if (c >= L'A' && c <= L'Z') {
            str[i] = c + 32; // Convert to lowercase
        }
    }
}

// Fast extension extraction
inline std::wstring FastGetExtension(const std::wstring& path) {
    size_t dotPos = path.find_last_of(L'.');
    if (dotPos == std::wstring::npos) return L"";
    return path.substr(dotPos);
}

// Check if current process should be monitored (skip system processes for performance)
bool ShouldMonitorProcess() {
    static std::atomic<bool> checked{false};
    static std::atomic<bool> shouldMonitor{true};
    
    if (!checked.load(std::memory_order_acquire)) {
        DWORD processId = GetCurrentProcessId();
        
        // Skip system processes for performance
        if (processId == 0 || processId == 4) {
            shouldMonitor.store(false, std::memory_order_release);
        } else {
            // Check if it's a system process by name
            std::wstring processName = g_processCache.getProcessName(processId);
            FastToLower(processName);
            
                    // Skip common system processes, but allow test_monitor.exe
        if (processName.find(L"svchost") != std::wstring::npos ||
            processName.find(L"lsass") != std::wstring::npos ||
            processName.find(L"winlogon") != std::wstring::npos ||
            processName.find(L"csrss") != std::wstring::npos ||
            processName.find(L"wininit") != std::wstring::npos ||
            processName.find(L"services") != std::wstring::npos ||
            processName.find(L"spoolsv") != std::wstring::npos ||
            processName.find(L"dwm") != std::wstring::npos ||
            (processName.find(L"explorer") != std::wstring::npos && 
             processName.find(L"test_monitor") == std::wstring::npos) ||
            processName.find(L"System") != std::wstring::npos ||
            processName.find(L"Idle") != std::wstring::npos) {
                shouldMonitor.store(false, std::memory_order_release);
            }
        }
        
        checked.store(true, std::memory_order_release);
    }
    
    return shouldMonitor.load(std::memory_order_acquire);
}

// Fast file path resolution with Windows XP compatibility
std::wstring GetFilePathFromHandle(HANDLE hFile) {
    // Windows XP compatibility: Use different approaches based on OS version
    if (!g_isWindowsXP) {
        // Windows 7/10: Use modern APIs for better performance
        
        // Try GetFileInformationByHandleEx first (Windows 7+)
        typedef BOOL (WINAPI *GetFileInformationByHandleEx_t)(HANDLE, FILE_INFO_BY_HANDLE_CLASS, LPVOID, DWORD);
        static GetFileInformationByHandleEx_t pGetFileInformationByHandleEx = NULL;
        static bool getFileInfoExInitialized = false;
        
        if (!getFileInfoExInitialized) {
            HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
            if (hKernel32) {
                pGetFileInformationByHandleEx = (GetFileInformationByHandleEx_t)GetProcAddress(hKernel32, "GetFileInformationByHandleEx");
            }
            getFileInfoExInitialized = true;
        }
        
        if (pGetFileInformationByHandleEx) {
            FILE_NAME_INFO fni;
            fni.FileNameLength = 0;
            
            if (pGetFileInformationByHandleEx(hFile, (FILE_INFO_BY_HANDLE_CLASS)0x21, &fni, sizeof(fni))) {
                std::wstring fullPath(fni.FileName, fni.FileNameLength / sizeof(wchar_t));
                
                // Remove "\\?\" prefix if present
                if (fullPath.substr(0, 4) == L"\\\\?\\") {
                    fullPath = fullPath.substr(4);
                }
                
                return fullPath;
            }
        }
        
        // Try GetFinalPathNameByHandleW (Windows Vista+)
        typedef DWORD (WINAPI *GetFinalPathNameByHandleW_t)(HANDLE, LPWSTR, DWORD, DWORD);
        static GetFinalPathNameByHandleW_t pGetFinalPathNameByHandleW = NULL;
        static bool getFinalPathInitialized = false;
        
        if (!getFinalPathInitialized) {
            HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
            if (hKernel32) {
                pGetFinalPathNameByHandleW = (GetFinalPathNameByHandleW_t)GetProcAddress(hKernel32, "GetFinalPathNameByHandleW");
            }
            getFinalPathInitialized = true;
        }
        
        if (pGetFinalPathNameByHandleW) {
            wchar_t filePath[MAX_PATH];
            DWORD pathLen = pGetFinalPathNameByHandleW(hFile, filePath, MAX_PATH, FILE_NAME_NORMALIZED);
            
            if (pathLen > 0 && pathLen < MAX_PATH) {
                std::wstring fullPath = filePath;
                
                // Remove "\\?\" prefix if present
                if (fullPath.substr(0, 4) == L"\\\\?\\") {
                    fullPath = fullPath.substr(4);
                }
                
                return fullPath;
            }
        }
    }
    
    // Windows XP fallback: Use GetModuleFileName approach
    wchar_t filePath[MAX_PATH];
    DWORD pathLen = GetModuleFileNameW(NULL, filePath, MAX_PATH);
    
    if (pathLen > 0 && pathLen < MAX_PATH) {
        // For Windows XP, we'll use a simpler approach
        return L"<monitored_file>";
    }
    
    // If all else fails, return a generic name
    return L"unknown_file";
}

// Fast file extension checking with pre-computed hashes
bool ShouldMonitorFile(const std::wstring& filePath) {
    // Fast lowercase conversion
    std::wstring lowerPath = filePath;
    FastToLower(lowerPath);
    
    // Find file extension using optimized function
    std::wstring extension = FastGetExtension(lowerPath);
    if (extension.empty()) {
        return false;
    }
    
    // O(1) hash lookup instead of O(n) linear search
    return g_extensionTable.shouldMonitor(extension);
}

// ============================================================================
// OPTIMIZED HOOK FUNCTIONS
// ============================================================================

HANDLE WINAPI MyCreateFileA(
    LPCTSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile)
{
    HANDLE hFile;
    
    HookOff(&APIHook, APIHook_CreateFileA);

    hFile = CreateFileA(lpFileName,
         dwDesiredAccess,
         dwShareMode,
         lpSecurityAttributes,
         dwCreationDisposition,
         dwFlagsAndAttributes,
         hTemplateFile);

    if (hFile != INVALID_HANDLE_VALUE) {
        g_perfStats.totalFileOperations.fetch_add(1, std::memory_order_relaxed);
        
        // Only monitor if this process should be monitored
        if (ShouldMonitorProcess()) {
            // Convert to wide string for consistency
            int len = MultiByteToWideChar(CP_ACP, 0, lpFileName, -1, NULL, 0);
            if (len > 0) {
                std::wstring wideFileName(len - 1, L'\0');
                MultiByteToWideChar(CP_ACP, 0, lpFileName, -1, &wideFileName[0], len);
                
                // Check if file should be monitored
                if (ShouldMonitorFile(wideFileName)) {
                    g_perfStats.monitoredFileOperations.fetch_add(1, std::memory_order_relaxed);
                    
                    // Get full file path
                    std::wstring fullPath = GetFilePathFromHandle(hFile);
                    if (fullPath.empty()) {
                        fullPath = wideFileName; // Fallback to original name
                    }
                    
                    // Lock-free cache access
                    fileHandleCache.put(hFile, fullPath);
                    
                    // Debug: Log file being monitored
                    FastWideToNarrow(wideFileName, g_narrowBuffer, sizeof(g_narrowBuffer));
                    g_logger->log("DEBUG|Monitoring file: %s", g_narrowBuffer);
                }
            }
        }
    }
    
    HookOn(&APIHook, APIHook_CreateFileA);

    return hFile;
}

HANDLE WINAPI MyCreateFileW(
    LPCWSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile)
{
    HANDLE hFile;

    HookOff(&APIHook, APIHook_CreateFileW);

    hFile = CreateFileW(
        lpFileName,
        dwDesiredAccess,
        dwShareMode,
        lpSecurityAttributes,
        dwCreationDisposition,
        dwFlagsAndAttributes,
        hTemplateFile
    );

    if (hFile != INVALID_HANDLE_VALUE) {
        g_perfStats.totalFileOperations.fetch_add(1, std::memory_order_relaxed);
        
        // Only monitor if this process should be monitored
        if (ShouldMonitorProcess()) {
            // Check if file should be monitored
            if (ShouldMonitorFile(lpFileName)) {
                g_perfStats.monitoredFileOperations.fetch_add(1, std::memory_order_relaxed);
                
                // Get full file path
                std::wstring fullPath = GetFilePathFromHandle(hFile);
                if (fullPath.empty()) {
                    fullPath = lpFileName; // Fallback to original name
                }
                
                // Lock-free cache access
                fileHandleCache.put(hFile, fullPath);
                
                // Debug: Log file being monitored
                FastWideToNarrow(lpFileName, g_narrowBuffer, sizeof(g_narrowBuffer));
                g_logger->log("DEBUG|Monitoring file: %s", g_narrowBuffer);
            }
        }
    }

    HookOn(&APIHook, APIHook_CreateFileW);

    return hFile;
}

BOOL WINAPI MyReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
    BOOL result;
    
    HookOff(&APIHook, APIHook_ReadFile);
    
    result = ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
    
    // Only track if bytes were actually requested and read
    if (result && nNumberOfBytesToRead > 0 && lpNumberOfBytesRead && *lpNumberOfBytesRead > 0) {
        // Lock-free cache access
        std::wstring filePath = fileHandleCache.get(hFile);
        
        if (!filePath.empty()) {
            // Lock-free bytes tracking - cumulative tracking
            FileBytes currentBytes = fileBytesCache.get(filePath);
            currentBytes.readBytes += *lpNumberOfBytesRead;
            fileBytesCache.put(filePath, currentBytes);
        }
    } else if (!result) {
        g_perfStats.readErrors.fetch_add(1, std::memory_order_relaxed);
    }
    
    HookOn(&APIHook, APIHook_ReadFile);
    
    return result;
}

BOOL WINAPI MyWriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped)
{
    BOOL result;
    
    HookOff(&APIHook, APIHook_WriteFile);
    
    result = WriteFile(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);
    
    // Only track if bytes were actually requested and written
    if (result && nNumberOfBytesToWrite > 0 && lpNumberOfBytesWritten && *lpNumberOfBytesWritten > 0) {
        // Lock-free cache access
        std::wstring filePath = fileHandleCache.get(hFile);
        
        if (!filePath.empty()) {
            // Lock-free bytes tracking - cumulative tracking
            FileBytes currentBytes = fileBytesCache.get(filePath);
            currentBytes.writeBytes += *lpNumberOfBytesWritten;
            fileBytesCache.put(filePath, currentBytes);
        }
    } else if (!result) {
        g_perfStats.writeErrors.fetch_add(1, std::memory_order_relaxed);
    }
    
    HookOn(&APIHook, APIHook_WriteFile);
    
    return result;
}

BOOL WINAPI MyCloseHandle(HANDLE hObject)
{
    BOOL result;
    
    HookOff(&APIHook, APIHook_CloseHandle);
    
    result = CloseHandle(hObject);
    
    if (result) {
        // Lock-free cache access
        std::wstring filePath = fileHandleCache.get(hObject);
        
        if (!filePath.empty()) {
            // Remove from cache
            fileHandleCache.erase(hObject);
            
            // Get bytes and log
            FileBytes fileBytes = fileBytesCache.get(filePath);
            fileBytesCache.erase(filePath);
            
            // Get cached process name
            DWORD processId = GetCurrentProcessId();
            std::wstring processName = g_processCache.getProcessName(processId);
            
            // Debug: Log file close
            FastWideToNarrow(filePath, g_narrowBuffer, sizeof(g_narrowBuffer));
            g_logger->log("DEBUG|Closing file: %s (R:%u W:%u)", g_narrowBuffer, fileBytes.readBytes, fileBytes.writeBytes);
            
            // Log read bytes if any
            if (fileBytes.readBytes > 0 && g_logger) {
                g_logger->log("|%s|R|%s|%X", g_narrowBuffer, fileBytes.readBytes);
            }
            
            // Log write bytes if any
            if (fileBytes.writeBytes > 0 && g_logger) {
                g_logger->log("|%s|W|%s|%X", g_narrowBuffer, fileBytes.writeBytes);
            }
        }
    }
    
    HookOn(&APIHook, APIHook_CloseHandle);
    
    return result;
}

// ============================================================================
// DLL MAIN - OPTIMIZED INITIALIZATION
// ============================================================================

BOOL APIENTRY DllMain(HINSTANCE hModule, DWORD fdwReason, LPVOID lpReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH) {
        // CRITICAL FIX: Initialize the logger first
        g_logger = new AsyncLogger();
        
        // Detect Windows XP for optimization - using modern approach
        #pragma warning(disable: 4996) // Disable deprecation warning for GetVersionEx
        OSVERSIONINFO osvi;
        ZeroMemory(&osvi, sizeof(OSVERSIONINFO));
        osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
        GetVersionEx(&osvi);
        g_isWindowsXP = (osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 1);
        #pragma warning(default: 4996)
        
        // Set up hooks
        if (!HookFuncCalls(&APIHook)) {
            g_logger->log("ERROR|Failed to set up API hooks");
            return FALSE;
        }
        
        // Log DLL injection with optimized format
        DWORD processId = GetCurrentProcessId();
        std::wstring processName = g_processCache.getProcessName(processId);
        FastWideToNarrow(processName, g_narrowBuffer, sizeof(g_narrowBuffer));
        g_logger->log("|%s|INJECT|%X", g_narrowBuffer, processId);
        
        // Debug: Log if process should be monitored
        bool shouldMonitor = ShouldMonitorProcess();
        g_logger->log("DEBUG|Process monitoring enabled: %s", shouldMonitor ? "YES" : "NO");
        
        // Debug: Log extension table info
        g_logger->log("DEBUG|Extension table contains %zu monitored extensions", g_extensionTable.getHashCount());
        
        // Debug: Log hook setup status
        g_logger->log("DEBUG|API hooks setup completed successfully");
        
        // Force flush to ensure logs are written
        g_logger->flush();
    }
    
    if (fdwReason == DLL_PROCESS_DETACH) {
        if (g_logger) {
            // Log DLL unload
            DWORD processId = GetCurrentProcessId();
            std::wstring processName = g_processCache.getProcessName(processId);
            FastWideToNarrow(processName, g_narrowBuffer, sizeof(g_narrowBuffer));
            g_logger->log("|%s|UNLOAD|%X", g_narrowBuffer, processId);
            
            // Log performance statistics
            g_logger->log("STATS|TotalOps:%zu|MonitoredOps:%zu|CacheHitRate:%.2f|WriteErrors:%zu|ReadErrors:%zu",
                        g_perfStats.totalFileOperations.load(),
                        g_perfStats.monitoredFileOperations.load(),
                        g_processCache.getHitRate(),
                        g_perfStats.writeErrors.load(),
                        g_perfStats.readErrors.load());
            
            // Flush any remaining log data
            g_logger->flush();
            
            // Clean up logger
            delete g_logger;
            g_logger = nullptr;
        }
        
        HookOffFuncCalls(&APIHook);
    }

    return TRUE;
}

// ============================================================================
// EXPORT FUNCTIONS - REQUIRED FOR DLL INJECTION
// ============================================================================

__MIDL_DECLSPEC_DLLEXPORT void dumb()
{
    // Dummy export function required for DLL injection
    // This function is called by injection tools to verify DLL loading
    
    // Debug: Force a test log entry
    if (g_logger) {
        g_logger->log("|DEBUG|DLL_LOADED|%X", GetCurrentProcessId());
        g_logger->flush();
    }
}

// Debug function to test logging
__MIDL_DECLSPEC_DLLEXPORT void testLog()
{
    if (g_logger) {
        g_logger->log("|DEBUG|TEST_LOG|%X", GetCurrentProcessId());
        g_logger->flush();
    }
}

// Test function to verify extension monitoring
__MIDL_DECLSPEC_DLLEXPORT bool testExtension(const char* extension)
{
    if (!g_logger) return false;
    
    // Convert to wide string
    int len = MultiByteToWideChar(CP_ACP, 0, extension, -1, NULL, 0);
    if (len > 0) {
        std::wstring wideExt(len - 1, L'\0');
        MultiByteToWideChar(CP_ACP, 0, extension, -1, &wideExt[0], len);
        
        bool shouldMonitor = g_extensionTable.shouldMonitor(wideExt);
        g_logger->log("DEBUG|Extension test: %s -> %s", extension, shouldMonitor ? "MONITOR" : "IGNORE");
        g_logger->flush();
        return shouldMonitor;
    }
    return false;
}

// Performance monitoring function
__MIDL_DECLSPEC_DLLEXPORT void getStats()
{
    if (g_logger) {
        g_logger->log("PERF|TotalOps:%zu|MonitoredOps:%zu|CacheHitRate:%.2f|WriteErrors:%zu|ReadErrors:%zu|BufferSize:%zu",
                    g_perfStats.totalFileOperations.load(),
                    g_perfStats.monitoredFileOperations.load(),
                    g_processCache.getHitRate(),
                    g_perfStats.writeErrors.load(),
                    g_perfStats.readErrors.load(),
                    g_logger->getBufferSize());
        g_logger->flush();
    }
} 