#pragma once

#include <Windows.h>
#include <atomic>
#include <array>
#include <memory>
#include <string>

// Lock-free hashmap with linear probing for maximum performance
template<typename K, typename V>
class LockFreeHashMap {
private:
    static constexpr size_t HASH_SIZE = 8192; // Power of 2 for fast modulo
    static constexpr size_t HASH_MASK = HASH_SIZE - 1;
    
    struct Entry {
        std::atomic<K> key;
        std::atomic<V> value;
        std::atomic<bool> valid;
        
        Entry() : key{}, value{}, valid(false) {}
    };
    
    alignas(64) std::array<Entry, HASH_SIZE> table;
    
    // Fast hash function for handles
    size_t hash(HANDLE h) const {
        uintptr_t x = reinterpret_cast<uintptr_t>(h);
        x ^= x >> 16;
        x *= 0x45d9f3b;
        x ^= x >> 16;
        x *= 0x45d9f3b;
        x ^= x >> 16;
        return x & HASH_MASK;
    }
    
    // Fast hash function for strings
    size_t hash(const std::wstring& str) const {
        size_t h = 14695981039346656037ULL; // FNV offset basis
        for (wchar_t c : str) {
            h ^= c;
            h *= 1099511628211ULL; // FNV prime
        }
        return h & HASH_MASK;
    }

public:
    LockFreeHashMap() = default;
    
    // Put operation with linear probing
    void put(const K& key, const V& value) {
        size_t idx = hash(key);
        
        for (size_t i = 0; i < HASH_SIZE; ++i) {
            size_t pos = (idx + i) & HASH_MASK;
            Entry& entry = table[pos];
            
            K expected_key{};
            if (entry.key.compare_exchange_weak(expected_key, key, std::memory_order_acq_rel)) {
                entry.value.store(value, std::memory_order_release);
                entry.valid.store(true, std::memory_order_release);
                return;
            } else if (entry.key.load(std::memory_order_acquire) == key) {
                entry.value.store(value, std::memory_order_release);
                return;
            }
        }
    }
    
    // Get operation with linear probing
    V get(const K& key) const {
        size_t idx = hash(key);
        
        for (size_t i = 0; i < HASH_SIZE; ++i) {
            size_t pos = (idx + i) & HASH_MASK;
            const Entry& entry = table[pos];
            
            if (!entry.valid.load(std::memory_order_acquire)) {
                return V{};
            }
            
            if (entry.key.load(std::memory_order_acquire) == key) {
                return entry.value.load(std::memory_order_acquire);
            }
        }
        return V{};
    }
    
    // Erase operation
    void erase(const K& key) {
        size_t idx = hash(key);
        
        for (size_t i = 0; i < HASH_SIZE; ++i) {
            size_t pos = (idx + i) & HASH_MASK;
            Entry& entry = table[pos];
            
            if (entry.key.load(std::memory_order_acquire) == key) {
                entry.valid.store(false, std::memory_order_release);
                entry.key.store(K{}, std::memory_order_release);
                entry.value.store(V{}, std::memory_order_release);
                return;
            }
        }
    }
};