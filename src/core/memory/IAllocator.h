#pragma once
#include <cstddef>
#include <cstdint>
#include "MemoryMath.h"

namespace fw::memory {

    class IAllocator {
    public:
        IAllocator(size_t totalSize)
            : m_TotalSize(totalSize), m_Used(0), m_Peak(0), m_NumAllocations(0) {}

        virtual ~IAllocator() {
            m_TotalSize = 0;
        }

        // Interfaccia Core
        virtual void* Allocate(size_t size, uint8_t alignment = 8) = 0;
        virtual void Free(void* ptr) = 0;
        virtual void Init() = 0; // Initialize internal structures (e.g. FreeList nodes)

        // Telemetria di Base
        const size_t GetTotalSize() const { return m_TotalSize; }
        const size_t GetUsed() const { return m_Used; }
        const size_t GetPeak() const { return m_Peak; }
        const size_t GetNumAllocations() const { return m_NumAllocations; }

    protected:
        size_t m_TotalSize;
        size_t m_Used;
        size_t m_Peak;
        size_t m_NumAllocations;
    };

} // namespace fw::memory
