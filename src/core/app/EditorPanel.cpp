#include "pch.h"
#include "EditorPanel.h"
#include "RenderManager.h"
#include "World.h"
#include "CharacterStats.h"
#include "Player.h"
#include "MobManager.h"
#include "SharedContext.h"
#include "StateManager.h"
#include "Components.h"
#include <imgui.h>
#include "ImGuizmo.h"
#include <windows.h>
#include <commdlg.h>
#include <math.h>
#include <algorithm>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

void EditorPanel::Draw(AssetManager& assets, World& world, RenderManager* renderer, MobManager* mobManager, Player* player, const RenderViewData& cameraView, SharedContext* context) {
    // Il menu radiale TAB è stato rimosso.
    // Questa funzione disegna solo le tab dell'editor.
    // Deve essere chiamata dall'interno di un BeginTabBar esistente (nel menu di pausa).

    // Forza la selezione del tab se m_activeTab è valido
    static int forceSelectTab = -1;
    if (m_activeTab != -1) {
        forceSelectTab = m_activeTab;
        m_activeTab = -1;
    }

    // --- IMGUIZMO INTEGRATION ---
    if (context && context->stateManager) {
        entt::registry* registry = context->stateManager->GetActiveRegistry();
        if (registry) {
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();

            ImVec2 windowPos = ImGui::GetWindowPos();
            ImVec2 windowSize = ImGui::GetWindowSize();
            ImGuizmo::SetRect(windowPos.x, windowPos.y, windowSize.x, windowSize.y);

            const float* viewMatrix = glm::value_ptr(cameraView.viewMatrix);
            const float* projMatrix = glm::value_ptr(cameraView.projectionMatrix);

            // Cerchiamo un'entità con TransformComponent e NameComponent (per non prendere la telecamera)
            auto view = registry->view<TransformComponent, NameComponent>();
            entt::entity selectedEntity = entt::null;
            for (auto entity : view) {
                if (view.get<NameComponent>(entity).name != "MainCamera") {
                    selectedEntity = entity;
                    break;
                }
            }

            if (selectedEntity != entt::null) {
                auto& transform = registry->get<TransformComponent>(selectedEntity);
                
                glm::mat4 t = glm::translate(glm::mat4(1.0f), glm::vec3(transform.x, transform.y, transform.z));
                glm::mat4 r = glm::mat4_cast(transform.rotation);
                // Assume scale 1.0f per ora, per semplicità
                glm::mat4 objectMatrix = t * r;

                ImGuizmo::Manipulate(
                    viewMatrix, 
                    projMatrix, 
                    ImGuizmo::TRANSLATE, 
                    ImGuizmo::WORLD,     
                    glm::value_ptr(objectMatrix)
                );

                if (ImGuizmo::IsUsing()) {
                    glm::vec3 scale;
                    glm::quat rotation;
                    glm::vec3 translation;
                    glm::vec3 skew;
                    glm::vec4 perspective;
                    glm::decompose(objectMatrix, scale, rotation, translation, skew, perspective);
                    
                    transform.x = translation.x;
                    transform.y = translation.y;
                    transform.z = translation.z;
                    transform.rotation = rotation;
                }
            }
        }
    }

    if (ImGui::BeginTabBar("EditorTabs", ImGuiTabBarFlags_None)) {
        
        ImGuiTabItemFlags flags0 = (forceSelectTab == 0) ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("📦 Blocchi", nullptr, flags0)) {
            DrawBlocksTab(assets, renderer);
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags flags1 = (forceSelectTab == 1) ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("🧟 Mob", nullptr, flags1)) {
            DrawMobsTab(assets, mobManager, cameraView);
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags flags7 = (forceSelectTab == 7) ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("🧑 Player", nullptr, flags7)) {
            if (player && mobManager) DrawPlayerTab(*player, *mobManager);
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags flags2 = (forceSelectTab == 2) ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("🎨 Texture Painter", nullptr, flags2)) {
            DrawTexturePainterTab(assets, renderer);
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags flags8 = (forceSelectTab == 8) ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("🧊 Model Sculptor", nullptr, flags8)) {
            m_modelEditor.Draw();
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags flags3 = (forceSelectTab == 3) ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("🌍 Mondo", nullptr, flags3)) {
            DrawWorldTab(world);
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags flags4 = (forceSelectTab == 4) ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("⚙️ Engine", nullptr, flags4)) {
            DrawEngineTab(renderer);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    forceSelectTab = -1;
}



void EditorPanel::DrawTexturePainterTab(AssetManager& assets, RenderManager* renderer) {

    ImGui::Text("Disegna Texture (16x16)");
    ImGui::Separator();
    
    static float brushColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    ImGui::ColorEdit4("Colore Pennello", brushColor);
    
    // Pixel grid: inizializzati a BIANCO OPACO (non trasparente!)
    // Bianco * vertex_color = vertex_color => default visivo corretto
    static ImVec4 pixels[16 * 16];
    static bool init = true;
    if (init) {
        for (int i = 0; i < 256; i++) pixels[i] = ImVec4(1, 1, 1, 1); // bianco opaco
        init = false;
    }
    
    ImGui::BeginChild("Painter", ImVec2(320, 320), true);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float sz = 20.0f; // cell size
    
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            ImVec2 pmin = ImVec2(p.x + x * sz, p.y + y * sz);
            ImVec2 pmax = ImVec2(pmin.x + sz, pmin.y + sz);
            
            draw_list->AddRectFilled(pmin, pmax, ImColor(pixels[y*16+x]));
            draw_list->AddRect(pmin, pmax, IM_COL32(100, 100, 100, 50));
            
            if (ImGui::IsMouseHoveringRect(pmin, pmax)) {
                if (ImGui::IsMouseDown(0)) { // Paint
                    pixels[y*16+x] = ImVec4(brushColor[0], brushColor[1], brushColor[2], brushColor[3]);
                }
                if (ImGui::IsMouseDown(1)) { // Erase
                    pixels[y*16+x] = ImVec4(0, 0, 0, 0);
                }
            }
        }
    }
    ImGui::EndChild();
    
    ImGui::SameLine();
    ImGui::BeginChild("PainterTools", ImVec2(0, 320), false);
    if (ImGui::Button("Svuota (Gomma globale)", ImVec2(150, 30))) {
        for (int i=0; i<256; i++) pixels[i] = ImVec4(0,0,0,0);
    }
    if (ImGui::Button("Riempi col Colore", ImVec2(150, 30))) {
        for (int i=0; i<256; i++) pixels[i] = ImVec4(brushColor[0], brushColor[1], brushColor[2], brushColor[3]);
    }
    
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    
    static char filename[64] = "custom_block.png";
    ImGui::InputText("Nome file", filename, IM_ARRAYSIZE(filename));

    ImGui::Text("Salva nei preset per Slot 4:");
    static int targetLayer = 1; // 1 = Grass (default), corrisponde a BlockType::Grass
    for (int i = 1; i <= 5; i++) {
        char btnLabel[32];
        snprintf(btnLabel, sizeof(btnLabel), "Slot %d", i);
        if (ImGui::Button(btnLabel)) {
            snprintf(filename, sizeof(filename), "custom%d.png", i);
            targetLayer = 4; // Imposta automaticamente a Layer 4 (Wood/Custom)
        }
        if (i < 5) ImGui::SameLine();
    }
    
    ImGui::Spacing();
    ImGui::SliderInt("Layer GPU (ID blocco)", &targetLayer, 0, 9);
    ImGui::SameLine();
    // Legenda rapida dei layer
    const char* layerNames[] = {"0","Grass","Dirt","Stone","Wood","Sand","Water","Lava","Leaves","Spawner"};
    if (targetLayer >= 0 && targetLayer <= 9)
        ImGui::TextColored(ImVec4(1,1,0,1), "%s", layerNames[targetLayer]);
    
    if (ImGui::Button("Salva e Applica su Vulkan", ImVec2(200, 40))) {
        // Converti ImVec4 array in uint8_t array (RGBA)
        uint8_t rgba[16 * 16 * 4];
        for (int i = 0; i < 256; i++) {
            rgba[i*4 + 0] = (uint8_t)(pixels[i].x * 255.0f);
            rgba[i*4 + 1] = (uint8_t)(pixels[i].y * 255.0f);
            rgba[i*4 + 2] = (uint8_t)(pixels[i].z * 255.0f);
            rgba[i*4 + 3] = (uint8_t)(pixels[i].w * 255.0f);
        }
        // 1. Salva il PNG su disco
        assets.SaveTexturePNG(filename, 16, 16, rgba);
        // 2. Aggiorna il Texture Array sulla GPU in tempo reale!
        if (renderer) {
            renderer->UpdateTextureLayer((uint32_t)targetLayer, rgba, 16, 16);
        }
    }
    ImGui::EndChild();
}

void EditorPanel::DrawBlocksTab(AssetManager& assets, RenderManager* renderer) {
    auto& blocks = assets.GetBlocks();
    
    // Layout a due colonne: Lista a sinistra, Dettagli a destra
    ImGui::Columns(2, "BlocksColumns", true);
    ImGui::SetColumnWidth(0, 150.0f);

    ImGui::Text("Lista Blocchi");
    ImGui::BeginChild("BlockList", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
    for (size_t i = 0; i < blocks.size(); i++) {
        bool is_selected = (m_selectedBlock == i);
        if (ImGui::Selectable(blocks[i].name.c_str(), is_selected)) {
            m_selectedBlock = (int)i;
        }
        if (is_selected) {
            ImGui::SetItemDefaultFocus();
        }
    }
    ImGui::EndChild();
    
    ImGui::NextColumn();

    if (!blocks.empty() && m_selectedBlock >= 0 && m_selectedBlock < (int)blocks.size()) {
        BlockDef& activeBlock = blocks[m_selectedBlock];

        ImGui::Text("Dettagli: %s", activeBlock.name.c_str());
        ImGui::Separator();

        // Variabili buffer temporanee per ImGui
        char nameBuf[128];
        strcpy_s(nameBuf, activeBlock.name.c_str());
        if (ImGui::InputText("Nome", nameBuf, IM_ARRAYSIZE(nameBuf))) {
            activeBlock.name = nameBuf;
        }

        // Texture con pulsante Browse
        auto drawTextureRow = [this](const char* label, std::string& texPath) {
            char pathBuf[256];
            strcpy_s(pathBuf, texPath.c_str());
            ImGui::PushItemWidth(200.0f);
            if (ImGui::InputText(label, pathBuf, IM_ARRAYSIZE(pathBuf))) {
                texPath = pathBuf;
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            std::string btnLabel = std::string("Sfoglia##") + label;
            if (ImGui::Button(btnLabel.c_str())) {
                std::string newPath = BrowseForFile("Image Files\0*.png;*.jpg\0All Files\0*.*\0");
                if (!newPath.empty()) {
                    texPath = newPath;
                }
            }
        };

        drawTextureRow("Tex Top", activeBlock.tex_top);
        drawTextureRow("Tex Side", activeBlock.tex_side);
        drawTextureRow("Tex Bottom", activeBlock.tex_bottom);

        ImGui::SliderFloat("Durezza", &activeBlock.hardness, 0.0f, 10.0f);
        ImGui::Checkbox("Trasparente", &activeBlock.transparent);
        if (activeBlock.transparent) {
            ImGui::SliderFloat("Alpha", &activeBlock.alpha, 0.0f, 1.0f);
        }

        ImGui::Spacing();
        if (ImGui::Button("Salva JSON e Ricarica (GPU)", ImVec2(250, 30))) {
            assets.SaveBlocksJson();
            if (renderer) {
                renderer->LoadBlockTextures("assets/", assets.GetBlocks());
            }
        }
    }

    ImGui::Columns(1);
}

void EditorPanel::DrawWorldTab(World& world) {
    ImGui::Text("Impostazioni di generazione procedurale");
    if (ImGui::Button("Rigenera Terreno")) {
        world.InitWorld();
    }
}

void EditorPanel::DrawPlayerTab(Player& player, MobManager& mobManager) {
    ImGui::TextColored(ImVec4(0.2f, 0.7f, 1.0f, 1.0f), "Statistiche Giocatore: %s", player.name.c_str());
    ImGui::Separator();
    
    ImGui::Text("Livello: %d", player.stats.level);
    ImGui::Text("EXP: %d / %d", player.stats.currentExp, player.stats.nextLevelExp);
    ImGui::Text("Punti Statistica Liberi: %d", player.freeStatPoints);
    ImGui::Separator();

    // Attributi Primari con pulsante [+] se ci sono punti disponibili
    auto drawAttribute = [&](const char* label, const char* attrName, int value) {
        ImGui::Text("%s: %d", label, value);
        if (player.freeStatPoints > 0) {
            ImGui::SameLine(200);
            std::string btnLabel = "+##" + std::string(attrName);
            if (ImGui::Button(btnLabel.c_str())) {
                player.SpendPoint(attrName);
                player.SaveToJson("assets/player.json");
            }
        }
    };

    drawAttribute("VIT (Vitalità)", "vit", player.stats.GetVIT());
    drawAttribute("STR (Forza)", "str", player.stats.GetSTR());
    drawAttribute("DEX (Destrezza)", "dex", player.stats.GetDEX());
    drawAttribute("INT (Intelligenza)", "int", player.stats.GetINT());
    drawAttribute("RES (Resistenza)", "res", player.stats.GetRES());
    drawAttribute("LUK (Fortuna)", "luk", player.stats.GetLUK());

    ImGui::Separator();
    ImGui::Text("HP Massimi: %d", player.stats.GetMaxHP());
    ImGui::Text("Danno Fisico Base: %d", player.stats.GetTotalPhysicalDamage());
    ImGui::Text("Difesa Fisica: %d", player.stats.GetPhysicalDef());

    ImGui::Spacing();
    if (ImGui::Button("Salva Statistiche")) {
        player.SaveToJson("assets/player.json");
    }
    ImGui::SameLine();
    if (ImGui::Button("Despawna Tutti i Mob")) {
        mobManager.DespawnAll();
    }
}

void EditorPanel::DrawMobsTab(AssetManager& assets, MobManager* mobManager, const RenderViewData& cameraView) {
    auto& mobs = assets.GetMobs();

    // ── Layout a due colonne ──────────────────────────────────────────
    ImGui::Columns(2, "MobsColumns", true);
    ImGui::SetColumnWidth(0, 185.0f);

    // ══════════════════════════════════════════════════════════════════
    //  COLONNA SINISTRA — Lista + controlli
    // ══════════════════════════════════════════════════════════════════
    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f),
        "Mob Registrati (%d)", (int)mobs.size());
    ImGui::Separator();

    ImGui::BeginChild("MobList", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 3.0f));
    for (size_t i = 0; i < mobs.size(); i++) {
        bool is_selected = (m_selectedMob == (int)i);

        // Colore nome in base alla fazione
        ImVec4 nameCol = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        if      (mobs[i].faction == "Monster")  nameCol = ImVec4(1.0f, 0.50f, 0.50f, 1.0f);
        else if (mobs[i].faction == "Undead")   nameCol = ImVec4(0.70f, 0.45f, 1.00f, 1.0f);
        else if (mobs[i].faction == "Boss")     nameCol = ImVec4(1.0f, 0.80f, 0.05f, 1.0f);
        else if (mobs[i].faction == "Friendly") nameCol = ImVec4(0.40f, 1.00f, 0.40f, 1.0f);
        else if (mobs[i].faction == "NPC")      nameCol = ImVec4(0.60f, 0.90f, 1.00f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Text, nameCol);
        std::string label = mobs[i].displayName + "##" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), is_selected))
            m_selectedMob = (int)i;
        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("ID: %s", mobs[i].id.c_str());
            ImGui::Text("Fazione: %s", mobs[i].faction.c_str());
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.90f,0.25f,0.25f,1), "VIT %d", mobs[i].stats.vit);
            ImGui::SameLine(); ImGui::TextColored(ImVec4(0.90f,0.50f,0.10f,1), "  STR %d", mobs[i].stats.str);
            ImGui::SameLine(); ImGui::TextColored(ImVec4(0.20f,0.80f,0.40f,1), "  DEX %d", mobs[i].stats.dex);
            ImGui::TextColored(ImVec4(0.20f,0.50f,0.95f,1), "INT %d", mobs[i].stats.intl);
            ImGui::SameLine(); ImGui::TextColored(ImVec4(0.65f,0.30f,0.90f,1), "  RES %d", mobs[i].stats.res);
            ImGui::SameLine(); ImGui::TextColored(ImVec4(0.90f,0.80f,0.10f,1), "  LUK %d", mobs[i].stats.luk);
            ImGui::EndTooltip();
        }
        if (is_selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::Button("+ Nuovo Mob", ImVec2(-1, 0))) {
        MobTemplate nm;
        nm.id          = "new_mob_" + std::to_string(mobs.size());
        nm.displayName = "Nuovo Mob";
        mobs.push_back(nm);
        m_selectedMob = (int)mobs.size() - 1;
    }
    if (!mobs.empty() && m_selectedMob >= 0 && m_selectedMob < (int)mobs.size()) {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.08f, 0.08f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.78f, 0.15f, 0.15f, 1.0f));
        if (ImGui::Button("- Elimina Selezionato", ImVec2(-1, 0))) {
            mobs.erase(mobs.begin() + m_selectedMob);
            m_selectedMob = std::max(0, m_selectedMob - 1);
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::NextColumn();

    // ══════════════════════════════════════════════════════════════════
    //  COLONNA DESTRA — Inspector
    // ══════════════════════════════════════════════════════════════════
    if (mobs.empty() || m_selectedMob < 0 || m_selectedMob >= (int)mobs.size()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Seleziona un mob dalla lista");
        ImGui::TextDisabled("o creane uno nuovo.");
        ImGui::Columns(1);
        return;
    }

    MobTemplate& mob = mobs[m_selectedMob];

    // Intestazione
    ImGui::TextColored(ImVec4(0.30f, 0.85f, 1.0f, 1.0f), "%s", mob.displayName.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("  Lv.%d  |  %s", mob.stats.level, mob.faction.c_str());
    ImGui::Separator();

    ImGui::BeginChild("MobInspector",
        ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.0f), false);

    // ── Sezione 1: Identificativi ─────────────────────────────────────
    if (ImGui::CollapsingHeader("  Identificativi", ImGuiTreeNodeFlags_DefaultOpen)) {
        char idBuf[128];   strcpy_s(idBuf,   mob.id.c_str());
        char namBuf[128];  strcpy_s(namBuf,  mob.displayName.c_str());
        if (ImGui::InputText("ID Univoco##mid",    idBuf,  IM_ARRAYSIZE(idBuf)))  mob.id = idBuf;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Usato per spawning e save.\nEs: goblin_warrior_01");
        if (ImGui::InputText("Nome Visualizzato##mdn", namBuf, IM_ARRAYSIZE(namBuf))) mob.displayName = namBuf;

        const char* factions[] = { "Monster", "Undead", "NPC", "Friendly", "Boss" };
        int fi = 0;
        for (int f = 0; f < IM_ARRAYSIZE(factions); f++)
            if (mob.faction == factions[f]) { fi = f; break; }
        if (ImGui::Combo("Fazione##mfac", &fi, factions, IM_ARRAYSIZE(factions)))
            mob.faction = factions[fi];
    }

    ImGui::Spacing();

    // ── Sezione 2: Attributi Primari + Preview Live ───────────────────
    if (ImGui::CollapsingHeader("  Attributi Primari & Statistiche Derivate",
        ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderInt("Livello##mlv", &mob.stats.level, 1, 100);
        ImGui::Spacing();

        // Macro locale: slider colorato per attributo
        auto colorSlider = [](const char* label, int* val,
            ImVec4 grab, ImVec4 grabActive, ImVec4 bg, ImVec4 bgHov)
        {
            ImGui::PushStyleColor(ImGuiCol_SliderGrab,       grab);
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, grabActive);
            ImGui::PushStyleColor(ImGuiCol_FrameBg,          bg);
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,   bgHov);
            ImGui::SliderInt(label, val, 1, 100);
            ImGui::PopStyleColor(4);
        };

        // VIT — Rosso
        colorSlider("VIT  Vitalita'##mvit", &mob.stats.vit,
            {0.90f,0.25f,0.25f,1}, {1.00f,0.35f,0.35f,1},
            {0.20f,0.07f,0.07f,1}, {0.26f,0.10f,0.10f,1});
        // STR — Arancione
        colorSlider("STR  Forza##mstr", &mob.stats.str,
            {0.90f,0.50f,0.10f,1}, {1.00f,0.62f,0.20f,1},
            {0.20f,0.12f,0.04f,1}, {0.26f,0.16f,0.06f,1});
        // DEX — Verde
        colorSlider("DEX  Destrezza##mdex", &mob.stats.dex,
            {0.20f,0.80f,0.40f,1}, {0.30f,0.95f,0.52f,1},
            {0.06f,0.18f,0.09f,1}, {0.08f,0.23f,0.12f,1});
        // INT — Blu
        colorSlider("INT  Intelligenza##mint", &mob.stats.intl,
            {0.20f,0.50f,0.95f,1}, {0.30f,0.62f,1.00f,1},
            {0.05f,0.10f,0.22f,1}, {0.07f,0.14f,0.29f,1});
        // RES — Viola
        colorSlider("RES  Resistenza##mres", &mob.stats.res,
            {0.65f,0.30f,0.90f,1}, {0.78f,0.42f,1.00f,1},
            {0.14f,0.07f,0.21f,1}, {0.18f,0.10f,0.27f,1});
        // LUK — Oro
        colorSlider("LUK  Fortuna##mluk", &mob.stats.luk,
            {0.90f,0.80f,0.10f,1}, {1.00f,0.92f,0.25f,1},
            {0.20f,0.17f,0.03f,1}, {0.26f,0.22f,0.05f,1});

        // ── Preview Live (dirty flag: si ricalcola solo se gli attr. cambiano) ─
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.30f, 1.0f),
            "  Statistiche Derivate — preview live");
        ImGui::Spacing();

        const CharacterStats cs = CharacterStats::FromAttributes(
            mob.stats.level,
            mob.stats.vit,  mob.stats.str,  mob.stats.dex,
            mob.stats.intl, mob.stats.res,  mob.stats.luk);

        // Griglia 3 colonne
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f,0.25f,0.30f,1.0f));
        ImGui::Columns(3, "DerivedGrid", false);
        ImGui::SetColumnWidth(0, 150.0f);
        ImGui::SetColumnWidth(1, 150.0f);

        // ── Risorse
        ImGui::TextColored({0.90f,0.25f,0.25f,1}, "HP Max");
        ImGui::Text("%d", cs.GetMaxHP()); ImGui::NextColumn();
        ImGui::TextColored({0.20f,0.50f,0.95f,1}, "MP Max");
        ImGui::Text("%d", cs.GetMaxMP()); ImGui::NextColumn();
        ImGui::TextColored({0.30f,0.90f,0.80f,1}, "Stamina");
        ImGui::Text("%d", cs.GetMaxStamina()); ImGui::NextColumn();
        ImGui::Separator();

        // ── Offensiva
        ImGui::TextColored({0.90f,0.50f,0.10f,1}, "Att. Fisico");
        ImGui::Text("%d +arma", cs.GetPhysicalAtk()); ImGui::NextColumn();
        ImGui::TextColored({0.65f,0.30f,0.90f,1}, "Att. Magico");
        ImGui::Text("%d +staff", cs.GetMagicalAtk()); ImGui::NextColumn();
        ImGui::TextColored({0.90f,0.80f,0.10f,1}, "Critico");
        ImGui::Text("%.1f%%", cs.GetCritChance() * 100.0f); ImGui::NextColumn();
        ImGui::Separator();

        // ── Difensiva
        ImGui::TextColored({0.65f,0.65f,0.75f,1}, "Dif. Fisica");
        ImGui::Text("%d +arm.", cs.GetPhysicalDef()); ImGui::NextColumn();
        ImGui::TextColored({0.65f,0.30f,0.90f,1}, "Dif. Magica");
        ImGui::Text("%d", cs.GetMagicalDef()); ImGui::NextColumn();
        ImGui::TextColored({0.20f,0.80f,0.40f,1}, "Schivata");
        ImGui::Text("%.1f%%", cs.GetEvasionRate() * 100.0f); ImGui::NextColumn();
        ImGui::Separator();

        // ── Velocita' & Riflessi
        ImGui::TextColored({0.20f,0.80f,0.40f,1}, "Precisione");
        ImGui::Text("%.0f%%", cs.GetHitAccuracy() * 100.0f); ImGui::NextColumn();
        ImGui::TextColored({1.00f,0.60f,0.20f,1}, "Vel. Attacco");
        ImGui::Text("x%.2f", cs.GetAtkSpeed()); ImGui::NextColumn();
        ImGui::TextColored({0.30f,0.90f,0.80f,1}, "Vel. Mossa");
        ImGui::Text("%.1f u/s", cs.GetMoveSpeed()); ImGui::NextColumn();
        ImGui::Separator();

        // ── LUK & Poise
        ImGui::TextColored({0.90f,0.80f,0.10f,1}, "Drop Rate");
        ImGui::Text("x%.2f", cs.GetDropMultiplier()); ImGui::NextColumn();
        ImGui::TextColored({0.90f,0.25f,0.25f,1}, "Poise");
        ImGui::Text("%d", cs.GetPoise());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Quanti danni assorbi prima dello stagger");
        ImGui::NextColumn();
        ImGui::NextColumn();

        ImGui::Columns(1);
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();

    // ── Sezione 3: AI ─────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("  AI & Intelligenza Artificiale")) {
        const char* behaviors[] = { "idle", "patrol", "chase_player", "flee_player" };
        int bi = 0;
        for (int b = 0; b < IM_ARRAYSIZE(behaviors); b++)
            if (mob.ai.behavior == behaviors[b]) { bi = b; break; }
        if (ImGui::Combo("Comportamento##mbeh", &bi, behaviors, IM_ARRAYSIZE(behaviors)))
            mob.ai.behavior = behaviors[bi];

        ImGui::Spacing();
        ImGui::TextDisabled("Movimento:");
        ImGui::SliderFloat("Vel. Cammino (PATROL)##mwk",  &mob.ai.walkSpeed,  0.1f, 15.0f, "%.1f u/s");
        ImGui::SliderFloat("Vel. Corsa   (CHASE)##mrn",   &mob.ai.runSpeed,   0.1f, 30.0f, "%.1f u/s");
        ImGui::SliderFloat("Vel. Rotazione##mtrn",         &mob.ai.turnSpeed,  1.0f, 30.0f, "%.0f deg/s");

        ImGui::Spacing();
        ImGui::TextDisabled("Sensi:");
        ImGui::SliderFloat("Raggio Rilevamento##mdet",    &mob.ai.detectionRadius, 1.0f,  60.0f, "%.1f u");
        ImGui::SliderFloat("Campo Visivo (FOV)##mfov",    &mob.ai.fieldOfView,     30.0f, 360.0f, "%.0f deg");
        ImGui::SliderFloat("Raggio Abbandono##mlos",      &mob.ai.loseSightRadius, 1.0f, 100.0f, "%.1f u");
        if (mob.ai.loseSightRadius <= mob.ai.detectionRadius) {
            ImGui::SameLine();
            ImGui::TextColored({1.0f,0.3f,0.3f,1}, " WARN: loseSight <= detection!");
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Combattimento:");
        ImGui::SliderFloat("Raggio Attacco##matk",        &mob.ai.attackRange,    0.5f, 25.0f, "%.1f u");
        ImGui::SliderFloat("Cooldown Attacco##mcd",       &mob.ai.attackCooldown, 0.1f, 10.0f, "%.2f s");
    }

    ImGui::Spacing();

    // ── Sezione 4: Fisica ─────────────────────────────────────────────
    if (ImGui::CollapsingHeader("  Fisica & Collider")) {
        ImGui::TextDisabled("Capsule Collider:");
        ImGui::SliderFloat("Raggio##mcr",          &mob.physics.colliderRadius,      0.1f,   3.0f, "%.2f m");
        ImGui::SliderFloat("Altezza##mch",          &mob.physics.colliderHeight,      0.2f,   5.0f, "%.2f m");
        ImGui::SliderFloat("Offset Verticale##mco", &mob.physics.colliderOffsetY,    -1.0f,   2.0f, "%.2f m");
        ImGui::Spacing();
        ImGui::TextDisabled("Massa & Knockback:");
        ImGui::SliderFloat("Massa (kg)##mms",             &mob.physics.mass,                1.0f,1000.0f, "%.0f kg");
        ImGui::SliderFloat("Resistenza Knockback##mkb",   &mob.physics.knockbackResistance, 0.0f,   1.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("0.0 = vola via dal colpo\n1.0 = completamente immobile");
    }

    ImGui::Spacing();

    // ── Sezione 5: Risorse ────────────────────────────────────────────
    if (ImGui::CollapsingHeader("  Risorse Grafiche & Audio")) {
        auto drawRes = [this](const char* label, std::string& path, const char* filter) {
            char buf[256]; strcpy_s(buf, path.c_str());
            ImGui::PushItemWidth(180.0f);
            std::string uid = std::string("##r_") + label;
            if (ImGui::InputText(uid.c_str(), buf, IM_ARRAYSIZE(buf))) path = buf;
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            std::string btn = std::string("...##b_") + label;
            if (ImGui::Button(btn.c_str())) {
                std::string p = BrowseForFile(filter);
                if (!p.empty()) path = p;
            }
        };
        drawRes("Modello 3D",   mob.resources.modelPath,
            "3D Models\0*.obj;*.fbx;*.gltf\0All Files\0*.*\0");
        drawRes("Texture",      mob.resources.texturePath,
            "Images\0*.png;*.jpg\0All Files\0*.*\0");
        char anim[128]; strcpy_s(anim, mob.resources.animatorControllerID.c_str());
        if (ImGui::InputText("Animator ID##mani", anim, IM_ARRAYSIZE(anim)))
            mob.resources.animatorControllerID = anim;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Es: humanoid_default, humanoid_caster, giant_beast");
        drawRes("Suono Hit",   mob.resources.onHitSound,
            "Audio\0*.wav;*.ogg\0All Files\0*.*\0");
        drawRes("Suono Morte", mob.resources.onDeathSound,
            "Audio\0*.wav;*.ogg\0All Files\0*.*\0");
    }

    ImGui::Spacing();

    // ── Sezione 6: Loot ───────────────────────────────────────────────
    if (ImGui::CollapsingHeader("  Loot & Progressione")) {
        ImGui::SliderInt("EXP Droppata##mexp", &mob.stats.expYield, 0, 10000);

        // Quanto pesa questa EXP sulla curva del player Lv.1?
        CharacterStats ref;
        ref.CalculateNextLevelExp();
        float pct = (float)mob.stats.expYield / (float)ref.nextLevelExp * 100.0f;
        ImGui::TextColored({0.80f,0.80f,0.30f,1},
            "  = %.1f%% dell'EXP per Lv.1 -> 2", pct);

        char drop[128]; strcpy_s(drop, mob.stats.dropTableID.c_str());
        if (ImGui::InputText("Drop Table ID##mdr", drop, IM_ARRAYSIZE(drop)))
            mob.stats.dropTableID = drop;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Riferimento a tabella drop esterna.\nEs: loot_goblin_tier1");
    }

    ImGui::EndChild();

    if (mobManager) {
        ImGui::Separator();
        if (ImGui::Button("Spawna davanti al Player", ImVec2(-1, 35))) {
            if (!mobs.empty() && m_selectedMob >= 0 && m_selectedMob < mobs.size()) {
                glm::vec3 spawnPos = cameraView.cameraPosition + cameraView.cameraFront * 3.0f;
                mobManager->Spawn(mobs[m_selectedMob], spawnPos);
            }
        }
    }

    // ── Pulsante Salva ────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.12f, 0.50f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.72f, 0.18f, 1.0f));
    if (ImGui::Button("  Salva mobs.json", ImVec2(-1, 35)))
        assets.SaveMobsJson();
    ImGui::PopStyleColor(2);

    ImGui::Columns(1);
}

void EditorPanel::DrawEngineTab(RenderManager* renderer) {
    ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "Impostazioni Grafiche dell'Engine");
    ImGui::Separator();
    ImGui::Spacing();

    if (renderer) {
        float fov = renderer->GetFov();
        ImGui::Text("Regolazione Campo Visivo (FOV):");
        if (ImGui::SliderFloat("##fov_slider", &fov, 30.0f, 120.0f, "%.0f gradi")) {
            renderer->SetFov(fov);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset FOV")) {
            renderer->SetFov(45.0f);
        }
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(Il valore di default e' 45 gradi)");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Errore: RenderManager non disponibile.");
    }
}

std::string EditorPanel::BrowseForFile(const char* filter) {
    OPENFILENAMEA ofn;
    CHAR szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(OPENFILENAMEA));
    ofn.lStructSize = sizeof(OPENFILENAMEA);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(ofn.lpstrFile);
    }
    return std::string("");
}
