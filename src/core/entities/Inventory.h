#pragma once
#include <string>
#include <vector>
#include "json.hpp"

enum class ItemType {
    None,
    Block,
    Weapon,
    Consumable
};

struct InventoryItem {
    ItemType type = ItemType::None;
    int blockType = 0;           // Se è un blocco (cast a BlockType)
    std::string stringId = "";   // Se è un'arma o un consumabile (es. "assets/models/sword.vox")
    int count = 0;               // Quantità
    int maxStack = 64;           // Massimo impilabile (es. armi 1, blocchi 64)
    float weightKg = 0.5f;      // Peso per singolo pezzo [kg] (default: oggetto generico 500g)

    bool IsEmpty() const { return type == ItemType::None || count <= 0; }
    void Clear() { type = ItemType::None; blockType = 0; stringId = ""; count = 0; weightKg = 0.5f; }
    
    // Peso totale dello stack
    float GetStackWeight() const { return IsEmpty() ? 0.0f : weightKg * count; }
};

class Inventory {
public:
    static const int HOTBAR_SIZE = 10;
    static const int INVENTORY_SIZE = 40; // 10 hotbar + 30 storage

    InventoryItem slots[INVENTORY_SIZE];

    Inventory();

    // Aggiunge l'oggetto nel primo slot libero o in uno stack esistente
    // Ritorna true se è riuscito ad aggiungere (tutto o in parte)
    bool AddItem(const InventoryItem& item);

    // Rimuove count oggetti dallo slot indicato
    void RemoveItem(int slotIndex, int count);
    
    // Scambia due slot (utile per il drag and drop)
    void SwapSlots(int slotA, int slotB);
    
    // Sposta una quantità parziale (es. 1 o metà) da slotA a slotB
    void MovePartial(int slotA, int slotB, int amount);

    void SaveToJson(nlohmann::json& j) const;
    void LoadFromJson(const nlohmann::json& j);
    
    // Inizializza l'inventario con alcuni blocchi di base (per test)
    void GiveStarterItems();

    // Ordina l'inventario compattando gli stack
    void Sort();
    
    // Svuota uno slot specifico (Cestino)
    void ClearSlot(int slotIndex) {
        if (slotIndex >= 0 && slotIndex < INVENTORY_SIZE) slots[slotIndex].Clear();
    }

    // Controlla se l'inventario è completamente pieno (nessuno slot vuoto né espandibile)
    bool IsFull() const {
        for (int i = 0; i < INVENTORY_SIZE; i++) {
            if (slots[i].IsEmpty()) return false;
            if (slots[i].count < slots[i].maxStack) return false; // Ha spazio residuo
        }
        return true;
    }

    // Peso totale di tutto il contenuto dell'inventario [kg]
    float GetInventoryWeightKg() const {
        float total = 0.0f;
        for (int i = 0; i < INVENTORY_SIZE; i++) {
            total += slots[i].GetStackWeight();
        }
        return total;
    }

    // Capacità di carico massima [kg] in funzione della Forza del personaggio
    // Formula: 20 kg base + STR * 3 kg
    //   STR 10 (base)   → 50 kg
    //   STR 30          → 110 kg
    //   STR 50          → 170 kg
    static float GetCarryCapacityKg(int strAttribute) {
        return 20.0f + strAttribute * 3.0f;
    }

    // Frazione di carico attuale [0..1+]: >1.0 significa sovraccaricat
    float GetEncumbranceRatio(int strAttribute) const {
        float capacity = GetCarryCapacityKg(strAttribute);
        return capacity > 0.0f ? GetInventoryWeightKg() / capacity : 0.0f;
    }
};
