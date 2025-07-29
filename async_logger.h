#pragma once

#include <Windows.h>
#include <atomic>
#include <array>
#include <string>

// High-performance async logger with memory-mapped I/O
class AsyncLogger {
private:
    static constexpr size_t RING_BUFFER_SIZE = 1024 * 1024; // 1MB ring buffer
    static constexpr size_t MAX_LOG_ENTRY = 512;
    static constexpr size_t RING_BUFFER_MASK = RING_BUFFER_SIZE - 1;
    
    // Ring buffer for log entries
    struct LogEntry {
        std::atomic<size_t> length;
        char data[MAX_LOG_ENTRY];
        
        LogEntry() : length(0) {}
    };
    
    alignas(64) std::array<LogEntry, RING_BUFFER_SIZE / sizeof(LogEntry)> ringBuffer;
    alignas(64) std::atomic<size_t> writeIndex{0};
    alignas(64) std::atomic<size_t> readIndex{0};
    
    // Memory-mapped log file
    HANDLE hFile;
    HANDLE hMapping;
    char* mappedMemory;
    std::atomic<size_t> fileOffset{0};
    static constexpr size_t MAPPED_FILE_SIZE = 64 * 1024 * 1024; // 64MB
    
    // Background flush thread
    HANDLE flushThread;
    std::atomic<bool> shutdown{false};
    HANDLE flushEvent;
    
    // High-resolution timer for timestamps
    LARGE_INTEGER frequency;
    LARGE_INTEGER startTime;
    
    static DWORD WINAPI FlushThreadProc(LPVOID param) {
        return static_cast<AsyncLogger*>(param)->FlushThreadFunc();
    }
    
    DWORD FlushThreadFunc() {
        while (!shutdown.load(std::memory_order_acquire)) {
            WaitForSingleObject(flushEvent, 10); // 10ms timeout
            FlushPendingEntries();
        }
        FlushPendingEntries(); // Final flush
        return 0;
    }
    
    void FlushPendingEntries() {
        size_t currentRead = readIndex.load(std::memory_order_acquire);
        size_t currentWrite = writeIndex.load(std::memory_order_acquire);
        
        while (currentRead != currentWrite) {
            const LogEntry& entry = ringBuffer[currentRead & (ringBuffer.size() - 1)];
            size_t entryLength = entry.length.load(std::memory_order_acquire);
            
            if (entryLength > 0) {
                // Write to memory-mapped file
                size_t offset = fileOffset.fetch_add(entryLength, std::memory_order_acq_rel);
                if (offset + entryLength < MAPPED_FILE_SIZE) {
                    memcpy(mappedMemory + offset, entry.data, entryLength);
                    FlushViewOfFile(mappedMemory + offset, entryLength);
                }
                
                // Mark entry as consumed
                const_cast<LogEntry&>(entry).length.store(0, std::memory_order_release);
            }
            
            currentRead++;
            readIndex.store(currentRead, std::memory_order_release);
            currentWrite = writeIndex.load(std::memory_order_acquire);
        }
    }
    
public:
    AsyncLogger() : hFile(INVALID_HANDLE_VALUE), hMapping(NULL), mappedMemory(nullptr) {
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&startTime);
        
        // Create memory-mapped log file
        wchar_t logPath[MAX_PATH];
        GetTempPathW(MAX_PATH, logPath);
        wcscat_s(logPath, L"file_monitor.log");
        
        hFile = CreateFileW(logPath, GENERIC_READ | GENERIC_WRITE, 
                           FILE_SHARE_READ, NULL, CREATE_ALWAYS, 
                           FILE_ATTRIBUTE_NORMAL, NULL);
        
        if (hFile != INVALID_HANDLE_VALUE) {
            hMapping = CreateFileMappingW(hFile, NULL, PAGE_READWRITE, 
                                        0, MAPPED_FILE_SIZE, NULL);
            if (hMapping) {
                mappedMemory = static_cast<char*>(MapViewOfFile(hMapping, 
                                                              FILE_MAP_WRITE, 
                                                              0, 0, MAPPED_FILE_SIZE));
            }
        }
        
        // Start flush thread
        flushEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
        flushThread = CreateThread(NULL, 0, FlushThreadProc, this, 0, NULL);
    }
    
    ~AsyncLogger() {
        shutdown.store(true, std::memory_order_release);
        SetEvent(flushEvent);
        
        if (flushThread != INVALID_HANDLE_VALUE) {
            WaitForSingleObject(flushThread, 1000);
            CloseHandle(flushThread);
        }
        
        if (flushEvent != INVALID_HANDLE_VALUE) {
            CloseHandle(flushEvent);
        }
        
        if (mappedMemory) {
            UnmapViewOfFile(mappedMemory);
        }
        if (hMapping) {
            CloseHandle(hMapping);
        }
        if (hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
        }
    }
    
    // High-performance logging with printf-style formatting
    void log(const char* format, ...) {
        LARGE_INTEGER currentTime;
        QueryPerformanceCounter(&currentTime);
        
        // Calculate microsecond timestamp
        uint64_t microseconds = ((currentTime.QuadPart - startTime.QuadPart) * 1000000) / frequency.QuadPart;
        
        // Get next write slot
        size_t writePos = writeIndex.fetch_add(1, std::memory_order_acq_rel);
        LogEntry& entry = ringBuffer[writePos & (ringBuffer.size() - 1)];
        
        // Wait for slot to be available
        while (entry.length.load(std::memory_order_acquire) != 0) {
            _mm_pause(); // CPU hint for spin-wait
        }
        
        // Format log entry with timestamp
        char* buffer = entry.data;
        int prefixLen = sprintf_s(buffer, MAX_LOG_ENTRY, "[%llu] ", microseconds);
        
        if (prefixLen > 0) {
            va_list args;
            va_start(args, format);
            int contentLen = vsnprintf_s(buffer + prefixLen, MAX_LOG_ENTRY - prefixLen - 2, 
                                       _TRUNCATE, format, args);
            va_end(args);
            
            if (contentLen > 0) {
                buffer[prefixLen + contentLen] = '\n';
                buffer[prefixLen + contentLen + 1] = '\0';
                
                // Atomically publish the entry
                entry.length.store(prefixLen + contentLen + 1, std::memory_order_release);
                
                // Notify flush thread
                SetEvent(flushEvent);
            }
        }
    }
    
    void flush() {
        SetEvent(flushEvent);
        Sleep(1); // Give flush thread time to process
    }
    
    size_t getBufferSize() const {
        size_t write = writeIndex.load(std::memory_order_acquire);
        size_t read = readIndex.load(std::memory_order_acquire);
        return write - read;
    }
};