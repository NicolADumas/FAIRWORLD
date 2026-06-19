#include "pch.h"
#include "Inventory.h"
#include <iostream>
#include <algorithm>

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
    InventoryItem grass; grass.type = ItemType::Block; grass.blockType = 1; grass.count = 64; AddItem(grass);
    InventoryItem dirt; dirt.type = ItemType::Block; dirt.blockType = 2; dirt.count = 64; AddItem(dirt);
    InventoryItem stone; stone.type = ItemType::Block; stone.blockType = 3; stone.count = 64; AddItem(stone);
    InventoryItem wood; wood.type = ItemType::Block; wood.blockType = 4; wood.count = 64; AddItem(wood);
    InventoryItem sand; sand.type = ItemType::Block; sand.blockType = 5; sand.count = 64; AddItem(sand);
    InventoryItem water; water.type = ItemType::Block; water.blockType = 6; water.count = 64; AddItem(water);
    InventoryItem lava; lava.type = ItemType::Block; lava.blockType = 7; lava.count = 64; AddItem(lava);
    InventoryItem leaves; leaves.type = ItemType::Block; leaves.blockType = 8; leaves.count = 64; AddItem(leaves);
    InventoryItem spawner; spawner.type = ItemType::Block; spawner.blockType = 9; spawner.count = 10; AddItem(spawner);
    InventoryItem light; light.type = ItemType::Block; light.blockType = 10; light.count = 64; AddItem(light);
    InventoryItem mushroom; mushroom.type = ItemType::Block; mushroom.blockType = 11; mushroom.count = 64; AddItem(mushroom);
    InventoryItem ore; ore.type = ItemType::Block; ore.blockType = 12; ore.count = 64; AddItem(ore);
}
