#include "pch.h"
#include "Inventory.h"
#include <iostream>
#include <algorithm>
#include "BlockMaterial.h"

Inventory::Inventory() {
    for (int i = 0; i < INVENTORY_SIZE; i++) {
        slots[i].Clear();
    }
}

bool Inventory::AddItem(const InventoryItem& item) {
    if (item.IsEmpty()) return false;

    int remaining = item.count;

    // 1. Cerca uno slot esistente con lo stesso tipo (che non sia pieno)
    for (int i = 0; i < INVENTORY_SIZE; i++) {
        if (slots[i].type == item.type && 
            slots[i].blockType == item.blockType && 
            slots[i].stringId == item.stringId &&
            slots[i].count < slots[i].maxStack) {
            
            int space = slots[i].maxStack - slots[i].count;
            if (remaining <= space) {
                slots[i].count += remaining;
                return true;
            } else {
                slots[i].count = slots[i].maxStack;
                remaining -= space;
            }
        }
    }

    // 2. Se avanza qualcosa, cerca slot vuoti
    for (int i = 0; i < INVENTORY_SIZE && remaining > 0; i++) {
        if (slots[i].IsEmpty()) {
            slots[i] = item;
            if (remaining <= slots[i].maxStack) {
                slots[i].count = remaining;
                return true;
            } else {
                slots[i].count = slots[i].maxStack;
                remaining -= slots[i].maxStack;
            }
        }
    }

    return remaining < item.count; // Ritorna true se almeno un pezzo è stato aggiunto
}

void Inventory::RemoveItem(int slotIndex, int count) {
    if (slotIndex < 0 || slotIndex >= INVENTORY_SIZE) return;
    
    if (!slots[slotIndex].IsEmpty()) {
        slots[slotIndex].count -= count;
        if (slots[slotIndex].count <= 0) {
            slots[slotIndex].Clear();
        }
    }
}

void Inventory::SwapSlots(int slotA, int slotB) {
    if (slotA < 0 || slotA >= INVENTORY_SIZE || slotB < 0 || slotB >= INVENTORY_SIZE) return;
    if (slotA == slotB) return;
    
    // Se i due slot sono dello stesso tipo e blocco, prova a fare un merge
    if (!slots[slotA].IsEmpty() && !slots[slotB].IsEmpty() && 
        slots[slotA].type == slots[slotB].type && 
        slots[slotA].blockType == slots[slotB].blockType &&
        slots[slotA].stringId == slots[slotB].stringId) {
        
        int space = slots[slotB].maxStack - slots[slotB].count;
        if (space > 0) {
            if (slots[slotA].count <= space) {
                slots[slotB].count += slots[slotA].count;
                slots[slotA].Clear();
            } else {
                slots[slotB].count += space;
                slots[slotA].count -= space;
            }
            return;
        }
    }
    
    InventoryItem temp = slots[slotA];
    slots[slotA] = slots[slotB];
    slots[slotB] = temp;
}

void Inventory::MovePartial(int slotA, int slotB, int amount) {
    if (slotA < 0 || slotA >= INVENTORY_SIZE || slotB < 0 || slotB >= INVENTORY_SIZE) return;
    if (slotA == slotB) return;
    if (slots[slotA].IsEmpty() || amount <= 0) return;
    
    amount = std::min(amount, slots[slotA].count); // Non spostare più di quanti ce ne siano

    if (slots[slotB].IsEmpty()) {
        // Slot destinazione vuoto, creiamo un nuovo stack
        slots[slotB] = slots[slotA];
        slots[slotB].count = amount;
        slots[slotA].count -= amount;
        if (slots[slotA].count <= 0) slots[slotA].Clear();
    } else if (slots[slotA].type == slots[slotB].type && 
               slots[slotA].blockType == slots[slotB].blockType &&
               slots[slotA].stringId == slots[slotB].stringId) {
        // Stesso tipo, merge parziale
        int space = slots[slotB].maxStack - slots[slotB].count;
        int toMove = std::min(amount, space);
        slots[slotB].count += toMove;
        slots[slotA].count -= toMove;
        if (slots[slotA].count <= 0) slots[slotA].Clear();
    }
}

void Inventory::Sort() {
    // Sposta tutti gli slot vuoti alla fine e compatta gli stack dello stesso tipo
    for (int i = 0; i < INVENTORY_SIZE; i++) {
        if (!slots[i].IsEmpty()) {
            for (int j = i + 1; j < INVENTORY_SIZE; j++) {
                if (!slots[j].IsEmpty() && 
                    slots[i].type == slots[j].type && 
                    slots[i].blockType == slots[j].blockType &&
                    slots[i].stringId == slots[j].stringId) {
                    
                    int space = slots[i].maxStack - slots[i].count;
                    if (space > 0) {
                        int amount = std::min(space, slots[j].count);
                        slots[i].count += amount;
                        slots[j].count -= amount;
                        if (slots[j].count <= 0) slots[j].Clear();
                    }
                }
            }
        }
    }

    // Compatta (muove verso l'inizio)
    int insertPos = 0;
    for (int i = 0; i < INVENTORY_SIZE; i++) {
        if (!slots[i].IsEmpty()) {
            if (i != insertPos) {
                slots[insertPos] = slots[i];
                slots[i].Clear();
            }
            insertPos++;
        }
    }
}

void Inventory::SaveToJson(nlohmann::json& j) const {
    nlohmann::json items = nlohmann::json::array();
    for (int i = 0; i < INVENTORY_SIZE; i++) {
        nlohmann::json itemObj;
        itemObj["type"] = (int)slots[i].type;
        itemObj["blockType"] = slots[i].blockType;
        itemObj["stringId"] = slots[i].stringId;
        itemObj["count"] = slots[i].count;
        itemObj["maxStack"] = slots[i].maxStack;
        items.push_back(itemObj);
    }
    j["inventory"] = items;
}

void Inventory::LoadFromJson(const nlohmann::json& j) {
    if (j.contains("inventory") && j["inventory"].is_array()) {
        int i = 0;
        for (const auto& itemObj : j["inventory"]) {
            if (i >= INVENTORY_SIZE) break;
            slots[i].type = (ItemType)itemObj.value("type", 0);
            slots[i].blockType = itemObj.value("blockType", 0);
            slots[i].stringId = itemObj.value("stringId", "");
            slots[i].count = itemObj.value("count", 0);
            slots[i].maxStack = itemObj.value("maxStack", 64);
            if (slots[i].type == ItemType::Block) {
                slots[i].weightKg = GetBlockMaterial((BlockType)slots[i].blockType).mass;
            } else {
                slots[i].weightKg = itemObj.value("weightKg", 0.5f);
            }
            if (slots[i].count <= 0) {
                slots[i].Clear();
            }
            i++;
        }
    } else {
        // Se non c'è inventario (primo avvio), dai oggetti iniziali
        GiveStarterItems();
    }
}

void Inventory::GiveStarterItems() {
    // Aggiungi un po' di blocchi iniziali
    auto addBlock = [&](int bType) {
        InventoryItem item;
        item.type = ItemType::Block;
        item.blockType = bType;
        item.count = 64;
        item.weightKg = GetBlockMaterial((BlockType)bType).mass;
        AddItem(item);
    };

    addBlock(1);  // Grass
    addBlock(2);  // Dirt
    addBlock(3);  // Stone
    addBlock(4);  // Wood
    addBlock(5);  // Sand
    addBlock(6);  // Water
    addBlock(7);  // Lava
    addBlock(8);  // Leaves
    
    InventoryItem spawner; spawner.type = ItemType::Block; spawner.blockType = 9; spawner.count = 10; spawner.weightKg = GetBlockMaterial(BlockType::MobSpawner).mass; AddItem(spawner);
    addBlock(10); // LightSource
    addBlock(11); // Mushroom
    addBlock(12); // Ore
    
    // Aggiungi una spada di default nello slot 0 (l'Hotbar parte da qui)
    InventoryItem sword;
    sword.type = ItemType::Weapon;
    sword.stringId = "assets/models/sword.vox";
    sword.count = 1;
    sword.maxStack = 1;
    sword.weightKg = 1.5f;
    AddItem(sword);
}
