#include "pch.h"
#include "AsyncInput.h"

namespace fw {

AsyncInput::AsyncInput() {
    m_writeBuffer = &m_bufferA;
    m_readBuffer = &m_bufferB;
    
    // Pre-allocare un po' di spazio per evitare riallocazioni durante l'hot-path OS
    m_bufferA.reserve(256);
    m_bufferB.reserve(256);
}

void AsyncInput::PushEvent(InputEvent event) {
    // Il lock è trattenuto per pochissimo tempo (solo il tempo di un push_back)
    // Non blocca quasi mai l'OS a meno che l'ECS non stia scambiando i buffer in quel microsecondo.
    std::lock_guard<std::mutex> lock(m_swapMutex);
    m_writeBuffer->push_back(event);
}

std::vector<InputEvent> AsyncInput::SwapBuffersAndConsume() {
    std::vector<InputEvent> eventsToProcess;
    
    {
        // Sezione critica lampo: scambiamo solo i puntatori
        std::lock_guard<std::mutex> lock(m_swapMutex);
        std::swap(m_writeBuffer, m_readBuffer);
    }
    
    // Ora il readBuffer contiene ciò che il writeBuffer aveva fino a un attimo fa.
    // L'OS sta già scrivendo sul nuovo writeBuffer libero, quindi possiamo processare con calma.
    eventsToProcess = std::move(*m_readBuffer);
    
    // Svuota il buffer appena letto così alla prossima passata sarà pulito.
    // L'uso di clear() mantiene la capacity() intatta (nessuna deallocazione).
    m_readBuffer->clear();
    
    return eventsToProcess;
}

} // namespace fw
