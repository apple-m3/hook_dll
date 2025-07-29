#pragma once

#include <Windows.h>
#include <atomic>
#include <vector>
#include <string>
#include <map>

// Zero-CPU Logging System Constants
#define ZERO_CPU_LOG_BUFFER_SIZE (64 * 1024)  // 64KB per buffer
#define ZERO_CPU_LOG_BUFFER_COUNT 4           // 4 buffers for rotation
#define ZERO_CPU_LOG_BATCH_SIZE 100          // Batch 100 events before flush
#define ZERO_CPU_LOG_FLUSH_INTERVAL_MS 100   // Flush every 100ms

// Forward declarations
class ZeroCPULogBuffer;
class ZeroCPULogFile;
class ZeroCPULogger;

// Zero-CPU Log Buffer Class
class ZeroCPULogBuffer {
public:
	ZeroCPULogBuffer();
	~ZeroCPULogBuffer();

	bool write(const wchar_t* data, size_t length);
	bool is_full() const;
	void reset();
	const wchar_t* get_data() const { return buffer; }
	size_t get_used() const { return used; }
	size_t get_capacity() const { return ZERO_CPU_LOG_BUFFER_SIZE; }

private:
	wchar_t buffer[ZERO_CPU_LOG_BUFFER_SIZE / sizeof(wchar_t)];
	std::atomic<size_t> used;
	std::atomic<bool> in_use;
};

// Zero-CPU Log File Class
class ZeroCPULogFile {
public:
	ZeroCPULogFile();
	~ZeroCPULogFile();

	bool initialize(const wchar_t* filename);
	void write(const wchar_t* data, size_t length);										// write for text data
	void writeBinary(const void* data, size_t length); // write for binary data
	void flush();
	void shutdown();
	bool is_same_file(const wchar_t* filename) const;

private:
	HANDLE hFile;
	bool initialized;
	std::wstring current_filename;
	std::atomic<size_t> write_offset;

	// Dynamic buffer for small writes (avoid mapping overhead)
	static const size_t BUFFER_SIZE = 64 * 1024; // 64KB buffer
	wchar_t write_buffer[BUFFER_SIZE / sizeof(wchar_t)];
	std::atomic<size_t> buffer_used;

	void flush_buffer();
	void write_direct(const wchar_t* data, size_t length);								// write for text data
};

// Zero-CPU Logger Class
class ZeroCPULogger {
public:
	static ZeroCPULogger& getInstance();

	bool initialize();
	void shutdown();
	void log(const wchar_t* filename, const wchar_t* data, size_t length);				// write for text data
	void logBinary(const wchar_t* filename, const void* data, size_t length); // write for binary data
	void flush_all();
	void flush_thread_func();

private:
	ZeroCPULogger();
	~ZeroCPULogger();

	ZeroCPULogFile* get_or_create_file(const wchar_t* filename);

	std::vector<ZeroCPULogFile*> log_files;
	HANDLE flush_thread;
	std::atomic<bool> shutdown_flag;

	// Lock-free buffer pool
	ZeroCPULogBuffer buffer_pool[ZERO_CPU_LOG_BUFFER_COUNT];
	std::atomic<size_t> current_buffer;

	// Batching
	struct LogEntry {												// write for text data
		std::wstring filename;
		std::wstring data; // for text
		bool isBinary = false;
		std::vector<char> binaryData; // for binary
	};
	
	std::vector<LogEntry> log_queue;								// write for text data
	std::atomic<size_t> queued_count;

	// Thread synchronization
	CRITICAL_SECTION queue_cs;
	CRITICAL_SECTION files_cs;
	HANDLE flush_event;
};

// Global function declarations
void SaveFileZeroCPU(LPTSTR filePath, LPTSTR szBuf);