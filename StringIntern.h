#pragma once

#include <Windows.h>
#include <atomic>
#include <unordered_map>
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include <cstdint>

// Forward declarations
class InternedString;
class StringInternPool;
class StringInternManager;

// InternedString - Reference counted, interned string
class InternedString {
public:
	InternedString(const wchar_t* str, size_t length);
	~InternedString();

	// Reference counting
	void AddRef();
	void Release();
	int GetRefCount() const { return refCount.load(); }

	// String access
	const wchar_t* GetString() const { return data; }
	size_t GetLength() const { return length; }
	size_t GetHash() const { return hash; }

	// Comparison
	bool operator==(const InternedString& other) const;
	bool operator<(const InternedString& other) const;

private:
	wchar_t* data;
	size_t length;
	size_t hash;
	std::atomic<int> refCount;

	// Memory pool allocation
	void* poolAllocation;
};

// StringInternPool - Memory pool for string storage
class StringInternPool {
public:
	StringInternPool();
	~StringInternPool();

	// Allocate string memory
	wchar_t* AllocateString(size_t length);
	void FreeString(wchar_t* str, size_t length);

	// Pool statistics
	size_t GetTotalAllocated() const { return totalAllocated.load(); }
	size_t GetActiveStrings() const { return activeStrings.load(); }

private:
	// Memory pool tiers
	static const size_t SMALL_STRING_MAX = 64;
	static const size_t MEDIUM_STRING_MAX = 256;
	static const size_t LARGE_STRING_MAX = 1024;

	// Pool blocks
	struct PoolBlock {
		std::atomic<bool> inUse;
		size_t size;
		char data[1];
	};

	// Small strings pool (64 bytes)
	static const size_t SMALL_POOL_SIZE = 1024;
	static const size_t SMALL_BLOCK_SIZE = 64;
	alignas(void*) char smallPool[SMALL_POOL_SIZE][SMALL_BLOCK_SIZE];
	std::atomic<size_t> smallPoolIndex;

	// Medium strings pool (256 bytes)
	static const size_t MEDIUM_POOL_SIZE = 512;
	static const size_t MEDIUM_BLOCK_SIZE = 256;
	alignas(void*) char mediumPool[MEDIUM_POOL_SIZE][MEDIUM_BLOCK_SIZE];
	std::atomic<size_t> mediumPoolIndex;

	// Large strings pool (1024 bytes)
	static const size_t LARGE_POOL_SIZE = 256;
	static const size_t LARGE_BLOCK_SIZE = 1024;
	alignas(void*) char largePool[LARGE_POOL_SIZE][LARGE_BLOCK_SIZE];
	std::atomic<size_t> largePoolIndex;

	// Statistics
	std::atomic<size_t> totalAllocated;
	std::atomic<size_t> activeStrings;

	// Fallback allocation for very large strings
	wchar_t* AllocateLargeString(size_t length);
	void FreeLargeString(wchar_t* str, size_t length);
};

// StringInternManager - Main string interning manager
class StringInternManager {
public:
	static StringInternManager& GetInstance();

	// String interning operations
	InternedString* InternString(const wchar_t* str, size_t length);
	InternedString* InternString(const wchar_t* str);
	InternedString* InternString(const std::wstring& str);

	// Memory pool access
	StringInternPool& GetStringPool();

	// Optimized string operations for common patterns
	InternedString* InternProcessName(const wchar_t* processName);
	InternedString* InternFilePath(const wchar_t* filePath);
	InternedString* InternOperationName(const wchar_t* operationName);
	InternedString* InternStatusString(const wchar_t* statusStr);

	// Batch operations for multiple strings
	void InternStrings(const std::vector<std::wstring>& strings);

	// Memory management
	void CleanupUnusedStrings();
	void Shutdown();

	// Statistics
	size_t GetInternedStringCount() const;
	size_t GetTotalMemoryUsage() const;
	size_t GetMemorySavings() const;

private:
	StringInternManager();
	~StringInternManager();

	// String storage
	std::unordered_map<size_t, std::vector<InternedString*>> stringHashTable;
	std::mutex hashTableMutex;

	// Memory pool
	std::unique_ptr<StringInternPool> stringPool;

	// Statistics
	std::atomic<size_t> totalInternedStrings;
	std::atomic<size_t> totalMemoryUsage;
	std::atomic<size_t> memorySavings;

	// Hash function for strings
	size_t CalculateHash(const wchar_t* str, size_t length) const;

	// Cleanup thread
	HANDLE cleanupThread;
	std::atomic<bool> shutdownFlag;
	static DWORD WINAPI CleanupThreadProc(LPVOID param);
	void CleanupThreadFunc();
};

// Optimized CString replacement for high-performance scenarios
class FastString {
public:
	FastString();
	FastString(const wchar_t* str);
	FastString(const wchar_t* str, size_t length);
	FastString(const std::wstring& str);
	FastString(const FastString& other);
	FastString(FastString&& other) noexcept;
	~FastString();

	FastString& operator=(const FastString& other);
	FastString& operator=(FastString&& other) noexcept;
	FastString& operator=(const wchar_t* str);
	FastString& operator=(const std::wstring& str);

	// String access
	const wchar_t* GetString() const;
	size_t GetLength() const;
	bool IsEmpty() const;

	// Comparison
	bool operator==(const FastString& other) const;
	bool operator==(const wchar_t* str) const;
	bool operator<(const FastString& other) const;

	// Conversion
	operator const wchar_t*() const { return GetString(); }
	std::wstring ToWString() const;

	// Format operations (optimized)
	void Format(const wchar_t* format, ...);
	void FormatV(const wchar_t* format, va_list args);

	// Static factory methods for common operations
	static FastString FromProcessId(DWORD processId);
	static FastString FromTimeStamp(LARGE_INTEGER timestamp);
	static FastString FromNtStatus(NTSTATUS status);
	static FastString FromFileOperation(DWORD operation);

private:
	InternedString* internedString;

	// Small string optimization (strings <= 16 characters stored inline)
	static const size_t SMALL_STRING_SIZE = 16;
	wchar_t smallString[SMALL_STRING_SIZE];
	bool isSmallString;

	void SetString(const wchar_t* str, size_t length);
	void SetSmallString(const wchar_t* str, size_t length);
	void SetInternedString(InternedString* str);
};

// Global functions for string optimization
namespace StringOptimization {
	// Initialize the string interning system
	void Initialize();

	// Shutdown the string interning system
	void Shutdown();

	// Get statistics
	void GetStatistics(size_t& internedCount, size_t& memoryUsage, size_t& memorySavings);

	// Optimized string operations
	FastString InternString(const wchar_t* str);
	FastString InternString(const std::wstring& str);

	// Batch operations
	void PreloadCommonStrings();
	void CleanupUnusedStrings();
}

// Macro for easy string interning
#define INTERN_STRING(str) StringOptimization::InternString(str)
#define FAST_STRING(str) FastString(str) 