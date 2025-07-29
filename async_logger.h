// async_logger.h - compatibility facade
#pragma once

#include "ZeroCPULogBuffer.h"
#include <cstdarg>
#include <string>
#include <vector>
#include <cstdio>
#ifdef _MSC_VER
#include <cwchar>
#endif
#include <cstring>

// NOTE:
//   The original code base expected an AsyncLogger class that offered a small
//   printf-style API (log/flush/getBufferSize).  The high-performance
//   ZeroCPULogger already available in the repository provides a more
//   efficient backend, but with a different interface.
//
//   This header implements a thin façade that preserves the old signature and
//   transparently forwards every call to the singleton ZeroCPULogger.
//   By doing so we enable immediate adoption of the lock-free logger without
//   touching the numerous call sites in dllmain.cpp and elsewhere.
//
//   The façade is *header-only* to avoid additional compilation units.
//   It allocates a small, fixed stack buffer per call and never performs any
//   heap allocation, fully respecting the zero-CPU/zero-RAM design goal.
//
#ifndef HAVE_WCSNLEN
inline size_t portable_wcsnlen(const wchar_t* s, size_t maxlen) {
    const wchar_t* end = (const wchar_t*)memchr(s, L'\0', maxlen * sizeof(wchar_t));
    if (end) {
        return end - s;
    }
    return maxlen;
}
#endif

class AsyncLogger {
public:
    AsyncLogger() {
        // Initialise the underlying logger once.
        ZeroCPULogger::getInstance().initialize();
    }

    ~AsyncLogger() {
        // Ensure everything is flushed before the process terminates.
        ZeroCPULogger::getInstance().shutdown();
    }

    // printf-style narrow-string logging (UTF-8 / ANSI). The message is written
    // to the default file "log.txt" in the current working directory.
    void log(const char* fmt, ...) {
        if (!fmt) return;
        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        if (len <= 0) return;

        // Convert to UTF-16 ‑ WideCharToMultiByte would be the other way
        // around; here we can use the C-locale as these are ASCII control
        // fields.
        wchar_t wbuffer[1024];
        size_t wlen = 0;
#ifdef _WIN32
        wlen = MultiByteToWideChar(CP_UTF8, 0, buffer, len, wbuffer, (int)(sizeof(wbuffer) / sizeof(wchar_t)));
#else
        // Fallback for non-Windows builds (should not occur in target env).
        for (int i = 0; i < len && i < (int)(sizeof(wbuffer) / sizeof(wchar_t)); ++i) {
            wbuffer[i] = (unsigned char)buffer[i];
        }
        wlen = (size_t)len;
#endif
        if (wlen == 0) return;
        ZeroCPULogger::getInstance().log(L"log.txt", wbuffer, wlen);
    }

    // wchar_t format version (printf-style wide strings)
    void log(const wchar_t* fmt, ...) {
        if (!fmt) return;
        wchar_t wbuffer[1024];
        va_list args;
        va_start(args, fmt);
#ifdef _WIN32
        _vsnwprintf_s(wbuffer, _TRUNCATE, fmt, args);
#else
        vswprintf(wbuffer, sizeof(wbuffer)/sizeof(wchar_t), fmt, args);
#endif
        va_end(args);
        size_t wlen =
#ifdef _MSC_VER
            wcsnlen_s(wbuffer, sizeof(wbuffer)/sizeof(wchar_t));
#else
            portable_wcsnlen(wbuffer, sizeof(wbuffer)/sizeof(wchar_t));
#endif
        if (wlen == 0) return;
        ZeroCPULogger::getInstance().log(L"log.txt", wbuffer, wlen);
    }

    void flush() {
        ZeroCPULogger::getInstance().flush_all();
    }

    // For compatibility – returns zero because the queue lives entirely inside
    // ZeroCPULogger and is flushed frequently.  You can replace this with a
    // real value by exposing a getter in ZeroCPULogger if needed.
    size_t getBufferSize() const { return 0; }
};