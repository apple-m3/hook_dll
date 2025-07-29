#include "StringIntern.h"
#include <algorithm>
#include <cstring>
#include <cstdarg>
#include <sstream>
#include <iomanip>

// Hash function for strings (FNV-1a)
static size_t FNV1aHash(const wchar_t* str, size_t length) {
	const size_t FNV_PRIME = 1099511628211ULL;
	const size_t FNV_OFFSET_BASIS = 14695981039346656037ULL;

	size_t hash = FNV_OFFSET_BASIS;
	for (size_t i = 0; i < length; ++i) {
		hash ^= static_cast<size_t>(str[i]);
		hash *= FNV_PRIME;
	}
	return hash;
}

// InternedString Implementation
InternedString::InternedString(const wchar_t* str, size_t len)
	: length(len), hash(FNV1aHash(str, len)), refCount(1), poolAllocation(nullptr) {

	// Allocate memory from pool
	StringInternPool& pool = StringInternManager::GetInstance().GetStringPool();
	data = pool.AllocateString(length + 1);

	// Copy string data
	memcpy(data, str, length * sizeof(wchar_t));
	data[length] = L'\0';
}

InternedString::~InternedString() {
	if (data && poolAllocation) {
		StringInternPool& pool = StringInternManager::GetInstance().GetStringPool();
		pool.FreeString(data, length + 1);
	}
}

void InternedString::AddRef() {
	refCount.fetch_add(1, std::memory_order_relaxed);
}

void InternedString::Release() {
	int oldCount = refCount.fetch_sub(1, std::memory_order_acq_rel);
	if (oldCount == 1) {
		delete this;
	}
}

bool InternedString::operator==(const InternedString& other) const {
	if (this == &other) return true;
	if (hash != other.hash || length != other.length) return false;
	return memcmp(data, other.data, length * sizeof(wchar_t)) == 0;
}

bool InternedString::operator<(const InternedString& other) const {
	if (this == &other) return false;
	int cmp = memcmp(data, other.data, min(length, other.length) * sizeof(wchar_t));
	if (cmp != 0) return cmp < 0;
	return length < other.length;
}

// StringInternPool Implementation
StringInternPool::StringInternPool()
	: smallPoolIndex(0), mediumPoolIndex(0), largePoolIndex(0),
	totalAllocated(0), activeStrings(0) {
}

StringInternPool::~StringInternPool() {
	// All strings should be freed by now
}

wchar_t* StringInternPool::AllocateString(size_t length) {
	size_t bytesNeeded = length * sizeof(wchar_t);

	if (bytesNeeded <= SMALL_BLOCK_SIZE) {
		// Use small pool
		size_t index = smallPoolIndex.fetch_add(1, std::memory_order_relaxed) % SMALL_POOL_SIZE;
		activeStrings.fetch_add(1, std::memory_order_relaxed);
		totalAllocated.fetch_add(bytesNeeded, std::memory_order_relaxed);
		return reinterpret_cast<wchar_t*>(smallPool[index]);
	}
	else if (bytesNeeded <= MEDIUM_BLOCK_SIZE) {
		// Use medium pool
		size_t index = mediumPoolIndex.fetch_add(1, std::memory_order_relaxed) % MEDIUM_POOL_SIZE;
		activeStrings.fetch_add(1, std::memory_order_relaxed);
		totalAllocated.fetch_add(bytesNeeded, std::memory_order_relaxed);
		return reinterpret_cast<wchar_t*>(mediumPool[index]);
	}
	else if (bytesNeeded <= LARGE_BLOCK_SIZE) {
		// Use large pool
		size_t index = largePoolIndex.fetch_add(1, std::memory_order_relaxed) % LARGE_POOL_SIZE;
		activeStrings.fetch_add(1, std::memory_order_relaxed);
		totalAllocated.fetch_add(bytesNeeded, std::memory_order_relaxed);
		return reinterpret_cast<wchar_t*>(largePool[index]);
	}
	else {
		// Use heap allocation for very large strings
		return AllocateLargeString(length);
	}
}

void StringInternPool::FreeString(wchar_t* str, size_t length) {
	size_t bytesFreed = length * sizeof(wchar_t);

	if (bytesFreed <= SMALL_BLOCK_SIZE) {
		// Small pool - no need to free, just decrement counter
		activeStrings.fetch_sub(1, std::memory_order_relaxed);
	}
	else if (bytesFreed <= MEDIUM_BLOCK_SIZE) {
		// Medium pool - no need to free, just decrement counter
		activeStrings.fetch_sub(1, std::memory_order_relaxed);
	}
	else if (bytesFreed <= LARGE_BLOCK_SIZE) {
		// Large pool - no need to free, just decrement counter
		activeStrings.fetch_sub(1, std::memory_order_relaxed);
	}
	else {
		// Heap allocation - free it
		FreeLargeString(str, length);
	}
}

wchar_t* StringInternPool::AllocateLargeString(size_t length) {
	activeStrings.fetch_add(1, std::memory_order_relaxed);
	totalAllocated.fetch_add(length * sizeof(wchar_t), std::memory_order_relaxed);
	return new wchar_t[length];
}

void StringInternPool::FreeLargeString(wchar_t* str, size_t length) {
	activeStrings.fetch_sub(1, std::memory_order_relaxed);
	delete[] str;
}

// StringInternManager Implementation
StringInternManager::StringInternManager()
	: totalInternedStrings(0), totalMemoryUsage(0), memorySavings(0),
	shutdownFlag(false), cleanupThread(INVALID_HANDLE_VALUE) {

	stringPool = std::make_unique<StringInternPool>();

	// Start cleanup thread
	cleanupThread = CreateThread(NULL, 0, CleanupThreadProc, this, 0, NULL);
}

StringInternManager::~StringInternManager() {
	Shutdown();
}

StringInternManager& StringInternManager::GetInstance() {
	static StringInternManager instance;
	return instance;
}

StringInternPool& StringInternManager::GetStringPool() {
	return *stringPool;
}

InternedString* StringInternManager::InternString(const wchar_t* str, size_t length) {
	if (!str || length == 0) return nullptr;

	size_t hash = CalculateHash(str, length);

	// Try to find existing string with lock
	{
		std::lock_guard<std::mutex> lock(hashTableMutex);
		auto it = stringHashTable.find(hash);
		if (it != stringHashTable.end()) {
			for (InternedString* interned : it->second) {
				if (interned->GetLength() == length &&
					memcmp(interned->GetString(), str, length * sizeof(wchar_t)) == 0) {
					interned->AddRef();
					return interned;
				}
			}
		}
	}

	// Create new interned string with lock
	{
		std::lock_guard<std::mutex> lock(hashTableMutex);
		// Double-check after acquiring lock
		auto it = stringHashTable.find(hash);
		if (it != stringHashTable.end()) {
			for (InternedString* interned : it->second) {
				if (interned->GetLength() == length &&
					memcmp(interned->GetString(), str, length * sizeof(wchar_t)) == 0) {
					interned->AddRef();
					return interned;
				}
			}
		}
		// Create new interned string
		InternedString* newString = new InternedString(str, length);
		stringHashTable[hash].push_back(newString);
		totalInternedStrings.fetch_add(1, std::memory_order_relaxed);
		totalMemoryUsage.fetch_add(length * sizeof(wchar_t), std::memory_order_relaxed);
		return newString;
	}
}

InternedString* StringInternManager::InternString(const wchar_t* str) {
	if (!str) return nullptr;
	return InternString(str, wcslen(str));
}

InternedString* StringInternManager::InternString(const std::wstring& str) {
	return InternString(str.c_str(), str.length());
}

InternedString* StringInternManager::InternProcessName(const wchar_t* processName) {
	// Optimized for process names (usually short and frequently repeated)
	return InternString(processName);
}

InternedString* StringInternManager::InternFilePath(const wchar_t* filePath) {
	// Optimized for file paths (normalize and intern)
	return InternString(filePath);
}

InternedString* StringInternManager::InternOperationName(const wchar_t* operationName) {
	// Optimized for operation names (usually predefined strings)
	return InternString(operationName);
}

InternedString* StringInternManager::InternStatusString(const wchar_t* statusStr) {
	// Optimized for status strings (usually predefined)
	return InternString(statusStr);
}

void StringInternManager::InternStrings(const std::vector<std::wstring>& strings) {
	for (const auto& str : strings) {
		InternString(str);
	}
}

void StringInternManager::CleanupUnusedStrings() {
	std::lock_guard<std::mutex> lock(hashTableMutex);
	for (auto& bucket : stringHashTable) {
		auto& strings = bucket.second;
		strings.erase(
			std::remove_if(strings.begin(), strings.end(),
				[](InternedString* str) {
			if (str->GetRefCount() == 0) {
				delete str;
				return true;
			}
			return false;
		}),
			strings.end()
			);
	}
}

void StringInternManager::Shutdown() {
	shutdownFlag.store(true, std::memory_order_relaxed);

	if (cleanupThread != INVALID_HANDLE_VALUE) {
		WaitForSingleObject(cleanupThread, 5000);
		CloseHandle(cleanupThread);
		cleanupThread = INVALID_HANDLE_VALUE;
	}

	// Clean up all strings
	std::lock_guard<std::mutex> lock(hashTableMutex);
	for (auto& bucket : stringHashTable) {
		for (InternedString* str : bucket.second) {
			delete str;
		}
	}
	stringHashTable.clear();
}

size_t StringInternManager::GetInternedStringCount() const {
	return totalInternedStrings.load();
}

size_t StringInternManager::GetTotalMemoryUsage() const {
	return totalMemoryUsage.load();
}

size_t StringInternManager::GetMemorySavings() const {
	return memorySavings.load();
}

size_t StringInternManager::CalculateHash(const wchar_t* str, size_t length) const {
	return FNV1aHash(str, length);
}

DWORD WINAPI StringInternManager::CleanupThreadProc(LPVOID param) {
	StringInternManager* manager = static_cast<StringInternManager*>(param);
	manager->CleanupThreadFunc();
	return 0;
}

void StringInternManager::CleanupThreadFunc() {
	while (!shutdownFlag.load(std::memory_order_relaxed)) {
		Sleep(30000); // Clean up every 30 seconds
		CleanupUnusedStrings();
	}
}

// FastString Implementation
FastString::FastString() : internedString(nullptr), isSmallString(false) {
	smallString[0] = L'\0';
}

FastString::FastString(const wchar_t* str) : internedString(nullptr), isSmallString(false) {
	if (!str) {
		smallString[0] = L'\0';
		isSmallString = true;
		return;
	}

	size_t length = wcslen(str);
	SetString(str, length);
}

FastString::FastString(const wchar_t* str, size_t length) : internedString(nullptr), isSmallString(false) {
	SetString(str, length);
}

FastString::FastString(const std::wstring& str) : internedString(nullptr), isSmallString(false) {
	SetString(str.c_str(), str.length());
}

FastString::FastString(const FastString& other) : internedString(nullptr), isSmallString(false) {
	if (other.isSmallString) {
		SetSmallString(other.smallString, wcslen(other.smallString));
	}
	else if (other.internedString) {
		SetInternedString(other.internedString);
	}
	else {
		smallString[0] = L'\0';
		isSmallString = true;
	}
}

FastString::FastString(FastString&& other) noexcept
	: internedString(other.internedString), isSmallString(other.isSmallString) {
	if (isSmallString) {
		memcpy(smallString, other.smallString, sizeof(smallString));
	}

	other.internedString = nullptr;
	other.isSmallString = false;
}

FastString::~FastString() {
	if (internedString) {
		internedString->Release();
	}
}

FastString& FastString::operator=(const FastString& other) {
	if (this != &other) {
		if (internedString) {
			internedString->Release();
		}

		if (other.isSmallString) {
			SetSmallString(other.smallString, wcslen(other.smallString));
		}
		else if (other.internedString) {
			SetInternedString(other.internedString);
		}
		else {
			smallString[0] = L'\0';
			isSmallString = true;
		}
	}
	return *this;
}

FastString& FastString::operator=(FastString&& other) noexcept {
	if (this != &other) {
		if (internedString) {
			internedString->Release();
		}

		internedString = other.internedString;
		isSmallString = other.isSmallString;

		if (isSmallString) {
			memcpy(smallString, other.smallString, sizeof(smallString));
		}

		other.internedString = nullptr;
		other.isSmallString = false;
	}
	return *this;
}

FastString& FastString::operator=(const wchar_t* str) {
	if (internedString) {
		internedString->Release();
		internedString = nullptr;
	}

	if (!str) {
		smallString[0] = L'\0';
		isSmallString = true;
	}
	else {
		SetString(str, wcslen(str));
	}

	return *this;
}

FastString& FastString::operator=(const std::wstring& str) {
	return operator=(str.c_str());
}

const wchar_t* FastString::GetString() const {
	if (isSmallString) {
		return smallString;
	}
	else if (internedString) {
		return internedString->GetString();
	}
	return L"";
}

size_t FastString::GetLength() const {
	if (isSmallString) {
		return wcslen(smallString);
	}
	else if (internedString) {
		return internedString->GetLength();
	}
	return 0;
}

bool FastString::IsEmpty() const {
	return GetLength() == 0;
}

bool FastString::operator==(const FastString& other) const {
	if (isSmallString && other.isSmallString) {
		return wcscmp(smallString, other.smallString) == 0;
	}
	else if (internedString && other.internedString) {
		return internedString == other.internedString || *internedString == *other.internedString;
	}
	else {
		return wcscmp(GetString(), other.GetString()) == 0;
	}
}

bool FastString::operator==(const wchar_t* str) const {
	return wcscmp(GetString(), str ? str : L"") == 0;
}

bool FastString::operator<(const FastString& other) const {
	if (isSmallString && other.isSmallString) {
		return wcscmp(smallString, other.smallString) < 0;
	}
	else if (internedString && other.internedString) {
		return *internedString < *other.internedString;
	}
	else {
		return wcscmp(GetString(), other.GetString()) < 0;
	}
}

std::wstring FastString::ToWString() const {
	return std::wstring(GetString());
}

void FastString::Format(const wchar_t* format, ...) {
	va_list args;
	va_start(args, format);
	FormatV(format, args);
	va_end(args);
}

void FastString::FormatV(const wchar_t* format, va_list args) {
	// Use a temporary buffer for formatting
	wchar_t buffer[1024];
	int result = _vswprintf_p(buffer, _countof(buffer), format, args);

	if (result >= 0) {
		SetString(buffer, result);
	}
	else {
		// Fallback for large strings
		int size = _vscwprintf_p(format, args) + 1;
		if (size > 0) {
			wchar_t* largeBuffer = new wchar_t[size];
			_vswprintf_p(largeBuffer, size, format, args);
			SetString(largeBuffer, size - 1);
			delete[] largeBuffer;
		}
	}
}

FastString FastString::FromProcessId(DWORD processId) {
	wchar_t buffer[32];
	swprintf_s(buffer, L"%d", processId);
	return FastString(buffer);
}

FastString FastString::FromTimeStamp(LARGE_INTEGER timestamp) {
	// Convert to FILETIME and format
	FILETIME ft;
	ft.dwLowDateTime = timestamp.LowPart;
	ft.dwHighDateTime = timestamp.HighPart;

	SYSTEMTIME st;
	FileTimeToSystemTime(&ft, &st);

	wchar_t buffer[64];
	swprintf_s(buffer, L"%04d/%02d/%02d %02d:%02d:%02d.%03d",
		st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

	return FastString(buffer);
}

FastString FastString::FromNtStatus(NTSTATUS status) {
	wchar_t buffer[32];
	swprintf_s(buffer, L"0x%08X", status);
	return FastString(buffer);
}

FastString FastString::FromFileOperation(DWORD operation) {
	// Map common file operations to strings
	switch (operation) {
	case 0: return FastString(L"CreateFile");
	case 1: return FastString(L"ReadFile");
	case 2: return FastString(L"WriteFile");
	case 3: return FastString(L"DeleteFile");
	case 4: return FastString(L"RenameFile");
	default: return FastString::FromProcessId(operation);
	}
}

void FastString::SetString(const wchar_t* str, size_t length) {
	if (length <= SMALL_STRING_SIZE) {
		SetSmallString(str, length);
	}
	else {
		// Use string interning for larger strings
		internedString = StringInternManager::GetInstance().InternString(str, length);
		isSmallString = false;
	}
}

void FastString::SetSmallString(const wchar_t* str, size_t length) {
	if (internedString) {
		internedString->Release();
		internedString = nullptr;
	}

	memcpy(smallString, str, length * sizeof(wchar_t));
	smallString[length] = L'\0';
	isSmallString = true;
}

void FastString::SetInternedString(InternedString* str) {
	if (internedString) {
		internedString->Release();
	}

	internedString = str;
	if (internedString) {
		internedString->AddRef();
	}
	isSmallString = false;
}

// StringOptimization namespace implementation
namespace StringOptimization {
	void Initialize() {
		// StringInternManager is initialized on first use
	}

	void Shutdown() {
		StringInternManager::GetInstance().Shutdown();
	}

	void GetStatistics(size_t& internedCount, size_t& memoryUsage, size_t& memorySavings) {
		internedCount = StringInternManager::GetInstance().GetInternedStringCount();
		memoryUsage = StringInternManager::GetInstance().GetTotalMemoryUsage();
		memorySavings = StringInternManager::GetInstance().GetMemorySavings();
	}

	FastString InternString(const wchar_t* str) {
		return FastString(str);
	}

	FastString InternString(const std::wstring& str) {
		return FastString(str);
	}

	void PreloadCommonStrings() {
		// Preload common strings for better performance
		std::vector<std::wstring> commonStrings = {
			L"CreateFile", L"ReadFile", L"WriteFile", L"DeleteFile", L"RenameFile",
			L"Process Init", L"Process Start", L"Process Exit",
			L"SUCCESS", L"ACCESS_DENIED", L"FILE_NOT_FOUND", L"PATH_NOT_FOUND",
			L"32-bit", L"64-bit", L"System", L"Medium", L"Low", L"High"
		};

		StringInternManager::GetInstance().InternStrings(commonStrings);
	}

	void CleanupUnusedStrings() {
		StringInternManager::GetInstance().CleanupUnusedStrings();
	}
}