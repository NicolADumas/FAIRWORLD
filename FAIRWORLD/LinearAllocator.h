#pragma once
#include "IAllocator.h"

namespace fw::memory {

    class LinearAllocator : public IAllocator {
    public:
        LinearAllocator(size_t totalSize, void* startPtr = nullptr);
        ~LinearAllocator() override;

        void* Allocate(size_t size, uint8_t alignment = 8) override;
        void Free(void* ptr) override;
        void Init() override;

        // Resetta l'allocatore azzerando l'offset (ideale per la fine di un frame)
        void Reset();

    private:
        void* m_StartPtr;
        size_t m_Offset;
        bool m_OwnsMemory; // True se ha allocato m_StartPtr con std::malloc internamente
    };

} // namespace fw::memory
