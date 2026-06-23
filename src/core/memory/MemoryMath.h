#pragma once
#include <cstdint>

namespace fw::memory {

    inline bool IsPowerOfTwo(uintptr_t x) {
        return (x & (x - 1)) == 0;
    }

    // Align a memory address to the given alignment (must be power of two)
    inline uintptr_t AlignForward(uintptr_t address, uint8_t alignment) {
        const uintptr_t mask = alignment - 1;
        return (address + mask) & ~mask;
    }

    inline void* AlignForward(void* address, uint8_t alignment) {
        return reinterpret_cast<void*>(AlignForward(reinterpret_cast<uintptr_t>(address), alignment));
    }

    // Calculate the number of bytes needed to align an address
    inline uint8_t AlignForwardAdjustment(const void* address, uint8_t alignment) {
        uint8_t adjustment = alignment - (reinterpret_cast<uintptr_t>(address) & static_cast<uintptr_t>(alignment - 1));
        if (adjustment == alignment) {
            return 0; // Already aligned
        }
        return adjustment;
    }

    // Calculate alignment adjustment taking into account an extra header
    // (useful for Stack/Pool/FreeList allocators that need to store metadata right before the pointer)
    inline uint8_t AlignForwardAdjustmentWithHeader(const void* address, uint8_t alignment, uint8_t headerSize) {
        uint8_t adjustment = AlignForwardAdjustment(address, alignment);
        uint8_t neededSpace = headerSize;

        if (adjustment < neededSpace) {
            neededSpace -= adjustment;
            adjustment += alignment * (neededSpace / alignment);
            if (neededSpace % alignment > 0) {
                adjustment += alignment;
            }
        }
        return adjustment;
    }

} // namespace fw::memory
