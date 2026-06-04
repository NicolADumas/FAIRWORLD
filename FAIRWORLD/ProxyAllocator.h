#pragma once
#include "IAllocator.h"

namespace fw::memory {

    // ProxyAllocator fa da wrapper attorno a un altro allocatore per categorizzare la memoria
    class ProxyAllocator : public IAllocator {
    public:
        ProxyAllocator(const char* name, IAllocator& allocator);
        ~ProxyAllocator() override;

        void* Allocate(size_t size, uint8_t alignment = 8) override;
        void Free(void* ptr) override;
        void Init() override;

        const char* GetName() const { return m_Name; }
        IAllocator& GetUnderlyingAllocator() { return m_Allocator; }

    private:
        const char* m_Name;
        IAllocator& m_Allocator;
    };

} // namespace fw::memory
