#pragma once

#include <Windows.h>
#include <atomic>
#include <array>

class ExtensionHashTable {
private:
    static constexpr size_t HASH_SIZE = 256;
    static constexpr size_t HASH_MASK = HASH_SIZE - 1;
    
    // Pre-computed hashes for monitored extensions
    std::array<std::atomic<bool>, HASH_SIZE> hashTable;
    std::atomic<size_t> hashCount{0};
    
    // Fast hash function for file extensions
    size_t hashExtension(const std::wstring& ext) const {
        size_t hash = 5381;
        for (wchar_t c : ext) {
            // Convert to lowercase inline
            wchar_t lc = (c >= L'A' && c <= L'Z') ? c + 32 : c;
            hash = ((hash << 5) + hash) + lc;
        }
        return hash & HASH_MASK;
    }
    
public:
    ExtensionHashTable() {
        // Initialize all to false
        for (auto& entry : hashTable) {
            entry.store(false, std::memory_order_relaxed);
        }
        
        // Pre-compute hashes for monitored extensions
        const wchar_t* extensions[] = {
            L".exe", L".dll", L".sys", L".bat", L".cmd", L".com", L".scr",
            L".msi", L".msp", L".msu", L".inf", L".reg", L".vbs", L".js",
            L".ps1", L".psm1", L".psd1", L".jar", L".class", L".py",
            L".rb", L".pl", L".php", L".asp", L".aspx", L".jsp",
            L".doc", L".docx", L".xls", L".xlsx", L".ppt", L".pptx",
            L".pdf", L".rtf", L".txt", L".log", L".ini", L".cfg",
            L".xml", L".json", L".csv", L".sql", L".db", L".mdb"
        };
        
        for (const wchar_t* ext : extensions) {
            addExtension(ext);
        }
    }
    
    void addExtension(const std::wstring& ext) {
        size_t hash = hashExtension(ext);
        if (!hashTable[hash].load(std::memory_order_acquire)) {
            hashTable[hash].store(true, std::memory_order_release);
            hashCount.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    bool shouldMonitor(const std::wstring& ext) const {
        if (ext.empty()) return false;
        size_t hash = hashExtension(ext);
        return hashTable[hash].load(std::memory_order_acquire);
    }
    
    size_t getHashCount() const {
        return hashCount.load(std::memory_order_acquire);
    }
};