#pragma once

#include <Windows.h>
#include <atomic>

// ============================================================================
// ZERO-CPU GLOBAL VARIABLES
// ============================================================================

// Process handles for zero-CPU operations
extern DWORD CPID;              // Current Process ID (cached)
extern HANDLE PRS;              // Process Handle (cached)

// ============================================================================
// ZERO-CPU PERFORMANCE CONSTANTS
// ============================================================================

// Memory alignment for optimal cache performance
#define ZERO_CPU_ALIGNMENT 64

// Maximum path length for zero-allocation operations
#define ZERO_CPU_MAX_PATH 512

// Buffer sizes optimized for zero-CPU performance
#define ZERO_CPU_SMALL_BUFFER 256
#define ZERO_CPU_MEDIUM_BUFFER 1024
#define ZERO_CPU_LARGE_BUFFER 4096

// ============================================================================
// ZERO-CPU MEMORY MANAGEMENT
// ============================================================================

// Fast memory operations with zero overhead
#define ZERO_CPU_MEMCPY(dest, src, size) __movsb((unsigned char*)(dest), (const unsigned char*)(src), (size))
#define ZERO_CPU_MEMSET(dest, value, size) __stosb((unsigned char*)(dest), (unsigned char)(value), (size))
#define ZERO_CPU_MEMCMP(ptr1, ptr2, size) memcmp((ptr1), (ptr2), (size))

// ============================================================================
// ZERO-CPU STRING OPERATIONS
// ============================================================================

// Fast string operations optimized for zero-CPU usage
namespace ZeroCPUString {
    
    // Ultra-fast string length calculation
    __forceinline size_t FastStrLen(const char* str) {
        if (!str) return 0;
        const char* start = str;
        while (*str) ++str;
        return str - start;
    }
    
    // Ultra-fast wide string length calculation
    __forceinline size_t FastWcsLen(const wchar_t* str) {
        if (!str) return 0;
        const wchar_t* start = str;
        while (*str) ++str;
        return str - start;
    }
    
    // Fast string comparison
    __forceinline int FastStrCmp(const char* str1, const char* str2) {
        if (!str1 || !str2) return str1 ? 1 : (str2 ? -1 : 0);
        while (*str1 && *str1 == *str2) {
            ++str1;
            ++str2;
        }
        return *str1 - *str2;
    }
    
    // Fast wide string comparison
    __forceinline int FastWcsCmp(const wchar_t* str1, const wchar_t* str2) {
        if (!str1 || !str2) return str1 ? 1 : (str2 ? -1 : 0);
        while (*str1 && *str1 == *str2) {
            ++str1;
            ++str2;
        }
        return *str1 - *str2;
    }
    
    // Fast lowercase conversion (in-place)
    __forceinline void FastToLower(char* str) {
        if (!str) return;
        while (*str) {
            if (*str >= 'A' && *str <= 'Z') {
                *str += 32;
            }
            ++str;
        }
    }
    
    // Fast wide lowercase conversion (in-place)
    __forceinline void FastToLowerW(wchar_t* str) {
        if (!str) return;
        while (*str) {
            if (*str >= L'A' && *str <= L'Z') {
                *str += 32;
            }
            ++str;
        }
    }
}

// ============================================================================
// ZERO-CPU HASH FUNCTIONS
// ============================================================================

namespace ZeroCPUHash {
    
    // Ultra-fast FNV-1a hash for strings
    __forceinline size_t FastHash(const char* data, size_t length) {
        const size_t FNV_PRIME = 1099511628211ULL;
        const size_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
        
        size_t hash = FNV_OFFSET_BASIS;
        for (size_t i = 0; i < length; ++i) {
            hash ^= static_cast<size_t>(data[i]);
            hash *= FNV_PRIME;
        }
        return hash;
    }
    
    // Ultra-fast FNV-1a hash for wide strings
    __forceinline size_t FastHashW(const wchar_t* data, size_t length) {
        const size_t FNV_PRIME = 1099511628211ULL;
        const size_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
        
        size_t hash = FNV_OFFSET_BASIS;
        for (size_t i = 0; i < length; ++i) {
            hash ^= static_cast<size_t>(data[i]);
            hash *= FNV_PRIME;
        }
        return hash;
    }
    
    // Fast hash for handles
    __forceinline size_t FastHashHandle(HANDLE handle) {
        uintptr_t x = reinterpret_cast<uintptr_t>(handle);
        x ^= x >> 16;
        x *= 0x45d9f3b;
        x ^= x >> 16;
        x *= 0x45d9f3b;
        x ^= x >> 16;
        return x;
    }
    
    // Fast hash for process IDs
    __forceinline size_t FastHashPID(DWORD pid) {
        return (pid * 2654435761U) >> 16;
    }
}

// ============================================================================
// ZERO-CPU TIME OPERATIONS
// ============================================================================

namespace ZeroCPUTime {
    
    // High-resolution timestamp structure
    struct Timestamp {
        LARGE_INTEGER value;
        
        Timestamp() {
            QueryPerformanceCounter(&value);
        }
        
        // Get microseconds since creation
        uint64_t GetMicroseconds() const {
            LARGE_INTEGER freq, current;
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&current);
            return ((current.QuadPart - value.QuadPart) * 1000000) / freq.QuadPart;
        }
    };
    
    // Fast timestamp for logging
    __forceinline uint64_t FastTimestamp() {
        static LARGE_INTEGER frequency = {0};
        static LARGE_INTEGER startTime = {0};
        
        if (frequency.QuadPart == 0) {
            QueryPerformanceFrequency(&frequency);
            QueryPerformanceCounter(&startTime);
        }
        
        LARGE_INTEGER currentTime;
        QueryPerformanceCounter(&currentTime);
        return ((currentTime.QuadPart - startTime.QuadPart) * 1000000) / frequency.QuadPart;
    }
}

// ============================================================================
// ZERO-CPU DEBUGGING AND PROFILING
// ============================================================================

#ifdef _DEBUG
    #define ZERO_CPU_DEBUG_BREAK() __debugbreak()
    #define ZERO_CPU_ASSERT(condition) \
        do { \
            if (!(condition)) { \
                __debugbreak(); \
            } \
        } while(0)
#else
    #define ZERO_CPU_DEBUG_BREAK() ((void)0)
    #define ZERO_CPU_ASSERT(condition) ((void)0)
#endif

// Performance profiling macros
#define ZERO_CPU_PROFILE_START(name) \
    static std::atomic<uint64_t> name##_total{0}; \
    static std::atomic<uint64_t> name##_count{0}; \
    uint64_t name##_start = ZeroCPUTime::FastTimestamp();

#define ZERO_CPU_PROFILE_END(name) \
    do { \
        uint64_t name##_duration = ZeroCPUTime::FastTimestamp() - name##_start; \
        name##_total.fetch_add(name##_duration, std::memory_order_relaxed); \
        name##_count.fetch_add(1, std::memory_order_relaxed); \
    } while(0)

#define ZERO_CPU_PROFILE_REPORT(name) \
    do { \
        uint64_t total = name##_total.load(std::memory_order_acquire); \
        uint64_t count = name##_count.load(std::memory_order_acquire); \
        uint64_t avg = count > 0 ? total / count : 0; \
        /* Log or output: name, total, count, avg */ \
    } while(0)

// ============================================================================
// ZERO-CPU COMPILER OPTIMIZATIONS
// ============================================================================

// Branch prediction hints
#if defined(_MSC_VER)
    #define ZERO_CPU_LIKELY(x) (x)
    #define ZERO_CPU_UNLIKELY(x) (x)
#elif defined(__GNUC__)
    #define ZERO_CPU_LIKELY(x) __builtin_expect(!!(x), 1)
    #define ZERO_CPU_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define ZERO_CPU_LIKELY(x) (x)
    #define ZERO_CPU_UNLIKELY(x) (x)
#endif

// Function attributes for zero-CPU performance
#define ZERO_CPU_HOT __declspec(noinline)
#define ZERO_CPU_COLD __declspec(noinline)
#define ZERO_CPU_PURE __declspec(noalias)

// Memory prefetch hints
#define ZERO_CPU_PREFETCH(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#define ZERO_CPU_PREFETCH_NTA(addr) _mm_prefetch((const char*)(addr), _MM_HINT_NTA)

// ============================================================================
// ZERO-CPU ERROR HANDLING
// ============================================================================

// Fast error codes for zero-CPU operations
enum ZeroCPUError {
    ZERO_CPU_SUCCESS = 0,
    ZERO_CPU_ERROR_MEMORY = 1,
    ZERO_CPU_ERROR_INVALID_PARAM = 2,
    ZERO_CPU_ERROR_NOT_INITIALIZED = 3,
    ZERO_CPU_ERROR_BUFFER_FULL = 4,
    ZERO_CPU_ERROR_SYSTEM = 5
};

// Fast error checking macro
#define ZERO_CPU_CHECK(condition, error_code) \
    do { \
        if (ZERO_CPU_UNLIKELY(!(condition))) { \
            return error_code; \
        } \
    } while(0)