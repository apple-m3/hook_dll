#include "ZeroCPULogBuffer.h"
#include <algorithm>

// Thread function for XP compatibility
DWORD WINAPI FlushThreadProc(LPVOID param) {
	ZeroCPULogger* logger = static_cast<ZeroCPULogger*>(param);
	logger->flush_thread_func();
	return 0;
}

// Zero-CPU Log Buffer Implementation
ZeroCPULogBuffer::ZeroCPULogBuffer() {
	used.store(0);
	in_use.store(false);
}

ZeroCPULogBuffer::~ZeroCPULogBuffer() {
}

bool ZeroCPULogBuffer::write(const wchar_t* data, size_t length) {
	size_t current_used = used.load();
	size_t data_size_bytes = length * sizeof(wchar_t);

	if (current_used + data_size_bytes > ZERO_CPU_LOG_BUFFER_SIZE) {
		return false; // Buffer full
	}

	// Lock-free write
	size_t expected = current_used;
	while (!used.compare_exchange_weak(expected, current_used + data_size_bytes)) {
		current_used = expected;
		if (current_used + data_size_bytes > ZERO_CPU_LOG_BUFFER_SIZE) {
			return false;
		}
	}

	// Copy data
	memcpy(buffer + (current_used / sizeof(wchar_t)), data, data_size_bytes);
	return true;
}

bool ZeroCPULogBuffer::is_full() const {
	return used.load() >= ZERO_CPU_LOG_BUFFER_SIZE;
}

void ZeroCPULogBuffer::reset() {
	used.store(0);
	in_use.store(false);
}

// Zero-CPU Log File Implementation
ZeroCPULogFile::ZeroCPULogFile() : hFile(INVALID_HANDLE_VALUE), initialized(false), write_offset(0), buffer_used(0) {
}

ZeroCPULogFile::~ZeroCPULogFile() {
	shutdown();
}

bool ZeroCPULogFile::initialize(const wchar_t* filename) {
	if (initialized) {
		return true;
	}

	// Store the filename
	current_filename = filename;

	// Create directory if it doesn't exist
	wchar_t dir_path[MAX_PATH];
	wcscpy_s(dir_path, MAX_PATH, filename);

	// Find the last backslash to separate directory from filename
	wchar_t* last_slash = wcsrchr(dir_path, L'\\');
	if (last_slash) {
		*last_slash = L'\0'; // Null-terminate at directory path

							 // Create directory recursively
		wchar_t* path_ptr = dir_path;
		if (path_ptr[1] == L':') { // Skip drive letter
			path_ptr += 2;
		}

		while (path_ptr && *path_ptr) {
			if (*path_ptr == L'\\') {
				*path_ptr = L'\0';
				CreateDirectoryW(dir_path, NULL);
				*path_ptr = L'\\';
			}
			path_ptr++;
		}
		CreateDirectoryW(dir_path, NULL);
	}

	// Create or open the file
	hFile = CreateFileW(filename,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
		NULL);

	if (hFile == INVALID_HANDLE_VALUE) {
		return false;
	}

	// Get current file size
	DWORD file_size_high;
	DWORD file_size_low = GetFileSize(hFile, &file_size_high);
	if (file_size_low == INVALID_FILE_SIZE) {
		file_size_low = 0;
		file_size_high = 0;
	}

	// Set write offset to end of file
	write_offset.store(((ULONGLONG)file_size_high << 32) | file_size_low);
	buffer_used.store(0);

	initialized = true;
	return true;
}

bool ZeroCPULogFile::is_same_file(const wchar_t* filename) const {
	return (current_filename == filename);
}

void ZeroCPULogFile::write(const wchar_t* data, size_t length) {
	if (!initialized || hFile == INVALID_HANDLE_VALUE) {
		return;
	}

	size_t data_size_bytes = length * sizeof(wchar_t);

	// If data is small, use buffer
	if (data_size_bytes <= BUFFER_SIZE) {
		size_t current_buffer_used = buffer_used.load();

		// Check if data fits in buffer
		if (current_buffer_used + data_size_bytes <= BUFFER_SIZE) {
			// Copy to buffer
			memcpy(write_buffer + (current_buffer_used / sizeof(wchar_t)), data, data_size_bytes);
			buffer_used.store(current_buffer_used + data_size_bytes);
		}
		else {
			// Buffer is full, flush it first
			flush_buffer();

			// Copy new data to buffer
			memcpy(write_buffer, data, data_size_bytes);
			buffer_used.store(data_size_bytes);
		}
	}
	else {
		// Large data, flush buffer first then write directly
		flush_buffer();
		write_direct(data, length);
	}
}

void ZeroCPULogFile::flush_buffer() {
	if (!initialized || hFile == INVALID_HANDLE_VALUE) {
		return;
	}

	size_t current_buffer_used = buffer_used.load();
	if (current_buffer_used == 0) {
		return;
	}

	// Write buffer content to file
	write_direct(write_buffer, current_buffer_used / sizeof(wchar_t));

	// Reset buffer
	buffer_used.store(0);
}

void ZeroCPULogFile::write_direct(const wchar_t* data, size_t length) {
	if (!initialized || hFile == INVALID_HANDLE_VALUE) {
		return;
	}

	size_t data_size_bytes = length * sizeof(wchar_t);
	size_t current_offset = write_offset.load();

	// Set file pointer to current write position
	LARGE_INTEGER offset;
	offset.QuadPart = current_offset;
	if (SetFilePointerEx(hFile, offset, NULL, FILE_BEGIN)) {
		DWORD bytes_written = 0;
		if (WriteFile(hFile, data, (DWORD)data_size_bytes, &bytes_written, NULL)) {
			// Update write offset
			write_offset.store(current_offset + bytes_written);
		}
	}
}

void ZeroCPULogFile::flush() {
	if (initialized && hFile != INVALID_HANDLE_VALUE) {
		flush_buffer();
		FlushFileBuffers(hFile);
	}
}

void ZeroCPULogFile::shutdown() {
	if (initialized) {
		flush_buffer();
	}

	if (hFile != INVALID_HANDLE_VALUE) {
		CloseHandle(hFile);
		hFile = INVALID_HANDLE_VALUE;
	}

	initialized = false;
	write_offset.store(0);
	buffer_used.store(0);
}

void ZeroCPULogFile::writeBinary(const void* data, size_t length) {
	if (!initialized || hFile == INVALID_HANDLE_VALUE) {
		return;
	}
	size_t current_offset = write_offset.load();
	LARGE_INTEGER offset;
	offset.QuadPart = current_offset;
	if (SetFilePointerEx(hFile, offset, NULL, FILE_BEGIN)) {
		DWORD bytes_written = 0;
		if (WriteFile(hFile, data, (DWORD)length, &bytes_written, NULL)) {
			write_offset.store(current_offset + bytes_written);
		}
	}
}

// Zero-CPU Logger Implementation
ZeroCPULogger::ZeroCPULogger() : flush_thread(NULL), shutdown_flag(false), queued_count(0) {
	InitializeCriticalSection(&queue_cs);
	InitializeCriticalSection(&files_cs);
	flush_event = CreateEventW(NULL, FALSE, FALSE, NULL);
}

ZeroCPULogger::~ZeroCPULogger() {
	shutdown();
	DeleteCriticalSection(&queue_cs);
	DeleteCriticalSection(&files_cs);
	if (flush_event) {
		CloseHandle(flush_event);
	}
}

ZeroCPULogger& ZeroCPULogger::getInstance() {
	static ZeroCPULogger instance;
	return instance;
}

bool ZeroCPULogger::initialize() {
	if (flush_thread) {
		return true; // Already initialized
	}

	shutdown_flag.store(false);

	// Create flush thread - XP compatible
	DWORD thread_id;
	flush_thread = CreateThread(NULL, 0, FlushThreadProc, this, 0, &thread_id);

	return flush_thread != NULL;
}

void ZeroCPULogger::shutdown() {
	shutdown_flag.store(true);

	if (flush_event) {
		SetEvent(flush_event);
	}

	if (flush_thread) {
		WaitForSingleObject(flush_thread, 5000); // Wait up to 5 seconds
		CloseHandle(flush_thread);
		flush_thread = NULL;
	}

	flush_all();

	// Clean up log files
	EnterCriticalSection(&files_cs);
	for (size_t i = 0; i < log_files.size(); i++) {
		ZeroCPULogFile* file = log_files[i];
		if (file) {
			file->shutdown();
			delete file;
		}
	}
	log_files.clear();
	LeaveCriticalSection(&files_cs);
}

void ZeroCPULogger::log(const wchar_t* filename, const wchar_t* data, size_t length) {
	if (shutdown_flag.load()) {
		return;
	}

	// Add to queue for batching
	EnterCriticalSection(&queue_cs);
	LogEntry entry;
	entry.filename = filename;
	entry.data.assign(data, length);
	log_queue.push_back(entry);
	queued_count.fetch_add(1);
	LeaveCriticalSection(&queue_cs);

	// Notify flush thread if batch is ready
	if (queued_count.load() >= ZERO_CPU_LOG_BATCH_SIZE) {
		SetEvent(flush_event);
	}
}

void ZeroCPULogger::logBinary(const wchar_t* filename, const void* data, size_t length) {
	if (shutdown_flag.load()) {
		return;
	}
	EnterCriticalSection(&queue_cs);
	LogEntry entry;
	entry.filename = filename;
	entry.isBinary = true;
	entry.binaryData.assign((const char*)data, (const char*)data + length);
	log_queue.push_back(entry);
	queued_count.fetch_add(1);
	LeaveCriticalSection(&queue_cs);

	if (queued_count.load() >= ZERO_CPU_LOG_BATCH_SIZE) {
		SetEvent(flush_event);
	}
}

void ZeroCPULogger::flush_all() {
	std::vector<LogEntry> batch;

	// Get all queued entries
	EnterCriticalSection(&queue_cs);
	batch.swap(log_queue);
	queued_count.store(0);
	LeaveCriticalSection(&queue_cs);

	// Process batch
	for (size_t i = 0; i < batch.size(); i++) {
		const LogEntry& entry = batch[i];
		ZeroCPULogFile* file = get_or_create_file(entry.filename.c_str());
		if (file) {
			if (entry.isBinary) {
				file->writeBinary(entry.binaryData.data(), entry.binaryData.size());
			} else {
				file->write(entry.data.c_str(), entry.data.length());
			}
		}
	}

	// Flush all files
	EnterCriticalSection(&files_cs);
	for (size_t i = 0; i < log_files.size(); i++) {
		ZeroCPULogFile* file = log_files[i];
		if (file) {
			file->flush();
		}
	}
	LeaveCriticalSection(&files_cs);
}

void ZeroCPULogger::flush_thread_func() {
	while (!shutdown_flag.load()) {
		// Wait for batch or timeout
		DWORD wait_result = WaitForSingleObject(flush_event, ZERO_CPU_LOG_FLUSH_INTERVAL_MS);

		if (wait_result == WAIT_OBJECT_0 || queued_count.load() >= ZERO_CPU_LOG_BATCH_SIZE) {
			flush_all();
		}
	}
}

ZeroCPULogFile* ZeroCPULogger::get_or_create_file(const wchar_t* filename) {
	EnterCriticalSection(&files_cs);

	// Find existing file (simple linear search for compatibility)
	for (size_t i = 0; i < log_files.size(); i++) {
		ZeroCPULogFile* file = log_files[i];
		if (file && file->is_same_file(filename)) {
			LeaveCriticalSection(&files_cs);
			return file;
		}
	}

	// Create new file
	ZeroCPULogFile* new_file = new ZeroCPULogFile();
	if (new_file->initialize(filename)) {
		log_files.push_back(new_file);
		LeaveCriticalSection(&files_cs);
		return new_file;
	}

	delete new_file;
	LeaveCriticalSection(&files_cs);
	return NULL;
}

// Global function implementation
void SaveFileZeroCPU(LPTSTR filePath, LPTSTR szBuf) {
	// Use the new zero-CPU logging system
	// Convert TCHAR to wchar_t for Unicode builds
#ifdef UNICODE
	size_t len = wcslen((const wchar_t*)szBuf);
	ZeroCPULogger::getInstance().log((const wchar_t*)filePath, (const wchar_t*)szBuf, len);
#else
	// For ANSI builds, convert to Unicode
	int wlen = MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szBuf, -1, NULL, 0);
	if (wlen > 0) {
		wchar_t* wbuf = new wchar_t[wlen];
		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szBuf, -1, wbuf, wlen);

		int wpathlen = MultiByteToWideChar(CP_ACP, 0, (LPCSTR)filePath, -1, NULL, 0);
		if (wpathlen > 0) {
			wchar_t* wpath = new wchar_t[wpathlen];
			MultiByteToWideChar(CP_ACP, 0, (LPCSTR)filePath, -1, wpath, wpathlen);

			ZeroCPULogger::getInstance().log(wpath, wbuf, wcslen(wbuf));

			delete[] wpath;
		}
		delete[] wbuf;
	}
#endif
}