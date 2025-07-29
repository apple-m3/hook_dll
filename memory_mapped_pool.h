#pragma once

#include <Windows.h>
#include <atomic>
#include <array>

// Memory-mapped pool allocator for zero-allocation performance
class MemoryMappedPool {
private:
    static constexpr size_t POOL_SIZE = 16 * 1024 * 1024; // 16MB pool
    static constexpr size_t BLOCK_SIZE = 64; // 64-byte aligned blocks
    static constexpr size_t BLOCK_COUNT = POOL_SIZE / BLOCK_SIZE;
    
    // Memory-mapped pool
    HANDLE hMapping;
    char* poolMemory;
    
    // Free block tracking with atomic operations
    std::atomic<uint64_t> freeBlocks[BLOCK_COUNT / 64]; // Bitset for free blocks
    std::atomic<size_t> nextFreeHint{0};
    std::atomic<size_t> allocatedBlocks{0};
    
    // Find first free block using bit manipulation
    size_t findFreeBlock() {
        size_t hint = nextFreeHint.load(std::memory_order_acquire);
        
        for (size_t i = 0; i < BLOCK_COUNT / 64; ++i) {
            size_t idx = (hint + i) % (BLOCK_COUNT / 64);
            uint64_t mask = freeBlocks[idx].load(std::memory_order_acquire);
            
            if (mask != 0xFFFFFFFFFFFFFFFFULL) {
                // Find first zero bit (free block)
                unsigned long bitIndex;
                if (_BitScanForward64(&bitIndex, ~mask)) {
                    size_t blockIndex = idx * 64 + bitIndex;
                    
                    // Try to claim this block
                    uint64_t expected = mask;
                    uint64_t desired = mask | (1ULL << bitIndex);
                    
                    if (freeBlocks[idx].compare_exchange_weak(expected, desired, 
                                                            std::memory_order_acq_rel)) {
                        nextFreeHint.store(idx, std::memory_order_relaxed);
                        return blockIndex;
                    }
                }
            }
        }
        return SIZE_MAX; // No free blocks
    }
    
public:
    MemoryMappedPool() : hMapping(NULL), poolMemory(nullptr) {
        // Create memory-mapped pool
        hMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                    0, POOL_SIZE, L"FileMonitorPool");
        
        if (hMapping) {
            poolMemory = static_cast<char*>(MapViewOfFile(hMapping, FILE_MAP_WRITE,
                                                        0, 0, POOL_SIZE));
        }
        
        // Initialize all blocks as free
        for (auto& mask : freeBlocks) {
            mask.store(0, std::memory_order_relaxed);
        }
    }
    
    ~MemoryMappedPool() {
        if (poolMemory) {
            UnmapViewOfFile(poolMemory);
        }
        if (hMapping) {
            CloseHandle(hMapping);
        }
    }
    
    // Allocate aligned memory block
    void* allocate(size_t size) {
        if (!poolMemory || size == 0) return nullptr;
        
        // Calculate required blocks (round up)
        size_t blocksNeeded = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        
        if (blocksNeeded == 1) {
            // Single block allocation (fast path)
            size_t blockIndex = findFreeBlock();
            if (blockIndex != SIZE_MAX) {
                allocatedBlocks.fetch_add(1, std::memory_order_relaxed);
                return poolMemory + (blockIndex * BLOCK_SIZE);
            }
        } else {
            // Multi-block allocation (slower path)
            for (size_t start = 0; start <= BLOCK_COUNT - blocksNeeded; ++start) {
                bool canAllocate = true;
                
                // Check if consecutive blocks are available
                for (size_t i = 0; i < blocksNeeded; ++i) {
                    size_t blockIndex = start + i;
                    size_t arrayIndex = blockIndex / 64;
                    size_t bitIndex = blockIndex % 64;
                    
                    uint64_t mask = freeBlocks[arrayIndex].load(std::memory_order_acquire);
                    if (mask & (1ULL << bitIndex)) {
                        canAllocate = false;
                        break;
                    }
                }
                
                if (canAllocate) {
                    // Try to allocate all blocks atomically
                    bool success = true;
                    for (size_t i = 0; i < blocksNeeded; ++i) {
                        size_t blockIndex = start + i;
                        size_t arrayIndex = blockIndex / 64;
                        size_t bitIndex = blockIndex % 64;
                        
                        uint64_t expected = freeBlocks[arrayIndex].load(std::memory_order_acquire);
                        uint64_t desired = expected | (1ULL << bitIndex);
                        
                        if (!freeBlocks[arrayIndex].compare_exchange_weak(expected, desired,
                                                                        std::memory_order_acq_rel)) {
                            success = false;
                            break;
                        }
                    }
                    
                    if (success) {
                        allocatedBlocks.fetch_add(blocksNeeded, std::memory_order_relaxed);
                        return poolMemory + (start * BLOCK_SIZE);
                    }
                }
            }
        }
        
        return nullptr; // Pool exhausted
    }
    
    // Free memory block
    void deallocate(void* ptr, size_t size) {
        if (!ptr || !poolMemory) return;
        
        char* charPtr = static_cast<char*>(ptr);
        if (charPtr < poolMemory || charPtr >= poolMemory + POOL_SIZE) return;
        
        size_t startBlock = (charPtr - poolMemory) / BLOCK_SIZE;
        size_t blocksToFree = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        
        // Free blocks
        for (size_t i = 0; i < blocksToFree; ++i) {
            size_t blockIndex = startBlock + i;
            size_t arrayIndex = blockIndex / 64;
            size_t bitIndex = blockIndex % 64;
            
            uint64_t mask = freeBlocks[arrayIndex].load(std::memory_order_acquire);
            uint64_t newMask = mask & ~(1ULL << bitIndex);
            freeBlocks[arrayIndex].store(newMask, std::memory_order_release);
        }
        
        allocatedBlocks.fetch_sub(blocksToFree, std::memory_order_relaxed);
        
        // Update hint for faster allocation
        nextFreeHint.store(startBlock / 64, std::memory_order_relaxed);
    }
    
    // Get pool statistics
    size_t getAllocatedBlocks() const {
        return allocatedBlocks.load(std::memory_order_acquire);
    }
    
    size_t getFreeBlocks() const {
        return BLOCK_COUNT - getAllocatedBlocks();
    }
    
    double getUtilization() const {
        return static_cast<double>(getAllocatedBlocks()) / BLOCK_COUNT;
    }
    
    bool isValid() const {
        return poolMemory != nullptr;
    }
};