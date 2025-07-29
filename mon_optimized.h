#pragma once

#include <Windows.h>
#include <atomic>

// ============================================================================
// ZERO-CPU MONITORING STRUCTURES
// ============================================================================

// Function hook structure optimized for zero-CPU overhead
struct SFunctionHook {
    const char* Name;           // Function name
    const char* DllName;        // DLL name (optional)
    void* HookFn;              // Hook function pointer
    void* OrigFn;              // Original function pointer
    unsigned char* opcord;      // Original opcodes
    unsigned char* Hookopcord;  // Hook opcodes
    DWORD opcordLen;           // Opcode length
    bool isHook;               // Hook enabled flag
};

// DLL hook structure for zero-CPU API hooking
struct SDLLHook {
    SFunctionHook* Functions;   // Array of function hooks
    const char* DllName;       // DLL name
};

// ============================================================================
// ZERO-CPU HOOK FUNCTION DECLARATIONS
// ============================================================================

// Zero-CPU optimized hook functions
HANDLE WINAPI ZeroCPUCreateFileA(LPCTSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
HANDLE WINAPI ZeroCPUCreateFileW(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
BOOL WINAPI ZeroCPUReadFile(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
BOOL WINAPI ZeroCPUWriteFile(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
BOOL WINAPI ZeroCPUCloseHandle(HANDLE);

// Hook management functions
bool HookFuncCalls(SDLLHook* Hook);
bool HookOffFuncCalls(SDLLHook* Hook);
void HookOff(SDLLHook* hook, int Funcindex);
void HookOn(SDLLHook* hook, int Funcindex);

// ============================================================================
// ZERO-CPU HOOK INDICES
// ============================================================================

enum APIHookIndices {
    APIHook_CreateFileA = 0,
    APIHook_CreateFileW = 1,
    APIHook_ReadFile = 2,
    APIHook_WriteFile = 3,
    APIHook_CloseHandle = 4,
    APIHook_Count = 5
};

// ============================================================================
// GLOBAL HOOK CONFIGURATION
// ============================================================================

// Zero-CPU optimized function hooks array
static SFunctionHook APIHookFunctions[] = {
    // CreateFileA
    {
        "CreateFileA",          // Name
        "kernel32.dll",         // DllName
        ZeroCPUCreateFileA,     // HookFn
        nullptr,                // OrigFn (will be filled)
        nullptr,                // opcord (will be allocated)
        nullptr,                // Hookopcord (will be allocated)
        5,                      // opcordLen (standard JMP instruction)
        true                    // isHook
    },
    
    // CreateFileW
    {
        "CreateFileW",          // Name
        "kernel32.dll",         // DllName
        ZeroCPUCreateFileW,     // HookFn
        nullptr,                // OrigFn (will be filled)
        nullptr,                // opcord (will be allocated)
        nullptr,                // Hookopcord (will be allocated)
        5,                      // opcordLen (standard JMP instruction)
        true                    // isHook
    },
    
    // ReadFile
    {
        "ReadFile",             // Name
        "kernel32.dll",         // DllName
        ZeroCPUReadFile,        // HookFn
        nullptr,                // OrigFn (will be filled)
        nullptr,                // opcord (will be allocated)
        nullptr,                // Hookopcord (will be allocated)
        5,                      // opcordLen (standard JMP instruction)
        true                    // isHook
    },
    
    // WriteFile
    {
        "WriteFile",            // Name
        "kernel32.dll",         // DllName
        ZeroCPUWriteFile,       // HookFn
        nullptr,                // OrigFn (will be filled)
        nullptr,                // opcord (will be allocated)
        nullptr,                // Hookopcord (will be allocated)
        5,                      // opcordLen (standard JMP instruction)
        true                    // isHook
    },
    
    // CloseHandle
    {
        "CloseHandle",          // Name
        "kernel32.dll",         // DllName
        ZeroCPUCloseHandle,     // HookFn
        nullptr,                // OrigFn (will be filled)
        nullptr,                // opcord (will be allocated)
        nullptr,                // Hookopcord (will be allocated)
        5,                      // opcordLen (standard JMP instruction)
        true                    // isHook
    },
    
    // Terminator
    {
        nullptr,                // Name
        nullptr,                // DllName
        nullptr,                // HookFn
        nullptr,                // OrigFn
        nullptr,                // opcord
        nullptr,                // Hookopcord
        0,                      // opcordLen
        false                   // isHook
    }
};

// Main API hook structure
static SDLLHook APIHook = {
    APIHookFunctions,           // Functions
    "kernel32.dll"              // DllName
};

// ============================================================================
// ZERO-CPU PERFORMANCE MACROS
// ============================================================================

// Fast memory barrier for x86/x64
#if defined(_M_X64) || defined(_M_IX86)
    #define ZERO_CPU_MEMORY_BARRIER() _mm_mfence()
    #define ZERO_CPU_PAUSE() _mm_pause()
#else
    #define ZERO_CPU_MEMORY_BARRIER() MemoryBarrier()
    #define ZERO_CPU_PAUSE() YieldProcessor()
#endif

// Cache line size for alignment
#define ZERO_CPU_CACHE_LINE_SIZE 64

// Alignment macro for zero-CPU performance
#define ZERO_CPU_ALIGN(x) alignas(x)

// Force inline for critical path functions
#define ZERO_CPU_FORCE_INLINE __forceinline

// Likely/unlikely branch prediction hints
#define ZERO_CPU_LIKELY(x) (x)
#define ZERO_CPU_UNLIKELY(x) (x)

// ============================================================================
// ZERO-CPU ATOMIC OPERATIONS
// ============================================================================

// Fast atomic increment for performance counters
template<typename T>
ZERO_CPU_FORCE_INLINE void ZeroCPUAtomicIncrement(std::atomic<T>& counter) {
    counter.fetch_add(1, std::memory_order_relaxed);
}

// Fast atomic add for byte counters
template<typename T>
ZERO_CPU_FORCE_INLINE void ZeroCPUAtomicAdd(std::atomic<T>& counter, T value) {
    counter.fetch_add(value, std::memory_order_relaxed);
}

// Fast atomic load for reading counters
template<typename T>
ZERO_CPU_FORCE_INLINE T ZeroCPUAtomicLoad(const std::atomic<T>& counter) {
    return counter.load(std::memory_order_acquire);
}