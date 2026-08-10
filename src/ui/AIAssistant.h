#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <future>
#include <atomic>
#include <mutex>

// Forward declarations
class World;
class MobManager;
class Player;

enum class BlockType : uint8_t; // Forward declare enum

struct GhostBlock {
    glm::ivec3 pos;
    BlockType type;
};

enum class AIState {
    Idle,
    WaitingForApproval
};

class AIAssistant {
public:
    AIAssistant();

    AIState GetState() const { return m_state; }
    std::vector<GhostBlock>& GetPreviewBlocks() { return m_previewBlocks; }
    
    bool IsPreviewDirty() const { return m_previewDirty; }
    void ClearPreviewDirty() { m_previewDirty = false; }
    void SetPreviewDirty() { m_previewDirty = true; }
    
    // Elabora la richiesta del giocatore (es. "crea casa", "spawn zombie")
    // e agisce direttamente sul mondo o sul MobManager.
    // Ritorna la risposta testuale dell'AI da stampare in chat.
    std::string ProcessPlayerMessage(const std::string& message, World& world, MobManager& mobManager, Player& player, const glm::vec3& playerPos, const glm::vec3& playerFront);

private:
    AIState m_state = AIState::Idle;
    std::vector<GhostBlock> m_previewBlocks;
    bool m_previewDirty = false;

    // Concurrency
    std::future<void> m_aiFuture;
    std::atomic<bool> m_isThinking{false};
    std::mutex m_mutex;
    std::string m_asyncResponse;
    bool m_hasAsyncResponse = false;
    
    // Server query function
    void QueryAIServer(std::string prompt, glm::vec3 playerPos);

    // Builder actions
    void BuildHouse(glm::vec3 pos);
    void BuildWall(glm::vec3 pos);
    void BuildTower(glm::vec3 pos);
    void SpawnEntity(MobManager& mobManager, const std::string& entityType, glm::vec3 pos);

    // Helpers
    void AddGhostBlock(int x, int y, int z, BlockType type);
    void ConfirmHologram(World& world);
    void CancelHologram();
};
