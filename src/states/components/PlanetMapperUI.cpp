#include "pch.h"
#include "PlanetMapperUI.h"
#include "imgui.h"
#include "WorldProjectManager.h"
#include "FAIRWORLD.h"
#include "DeviceManager.h"
#include "RenderManager.h"
#include "SphericalLOD.h"
#include "CubeSphereMapping.h"
#include "AppBaseState.h"
#include <algorithm>

PlanetMapperUI::PlanetMapperUI() {}

void PlanetMapperUI::DoSave(fw::WorldProjectManager* pm) {
    if (!pm) return;
    bool ok = pm->SaveProject();
    m_saveFlashTimer = 2.5f;
    m_saveFlashMsg = ok ? "✅ SALVATO!" : "❌ ERRORE SALVATAGGIO";
}

void PlanetMapperUI::Update(float dt) {
    if (m_saveFlashTimer > 0.0f) {
        m_saveFlashTimer -= dt;
    }
}

PlanetMapperUIResult PlanetMapperUI::Draw(SharedContext* context, 
                                          int& activePlanetIndex, 
                                          int& activeTemplateIndex, 
                                          float& orbitDistance, 
                                          float orbitYaw, 
                                          float orbitPitch,
                                          const fw::RaycastHit& lastRayHit,
                                          fw::SphericalLODSystem& lodSystem) {
    PlanetMapperUIResult result;
    if (!context || !context->projectManager) return result;

    auto& doc = context->projectManager->GetDocumentMutable();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float leftWidth = viewport->Size.x * 0.35f;
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(ImVec2(leftWidth, viewport->Size.y));
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | 
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    ImGui::Begin("PlanetMapperLeft", nullptr, windowFlags);
    
    // Header standard AppBaseState -> passiamo true per mostrare che siamo in Planet Mapper?
    // In AppBaseState c'è DrawMotherHeader. Visto che PlanetMapperUI non eredita da AppBaseState, 
    // potremmo aver bisogno di includere l'header o passare un callback per uscire all'Hub.
    // Usiamo una logica stand-alone per l'header o segnaliamo l'uscita.
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.1f, 0.3f, 0.6f, 1.0f));
    if (ImGui::Button("⬅ RITORNA ALL'HUB", ImVec2(150, 30))) {
        result.goToHubState = true;
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), " PLANET MAPPER - GLOBO SFERICO & SISTEMA SOLARE");
    ImGui::Separator();

    ImGui::BeginChild("PlanetControls", ImVec2(0, -135.0f), true);

    if (ImGui::Button("➕ Nuovo Pianeta", ImVec2(-1, 30))) {
        fw::PlanetMap newPlanet;
        newPlanet.name = "Pianeta " + std::to_string(doc.planets.size() + 1);
        newPlanet.planetSize = fw::PlanetSize::Medium;
        newPlanet.isFlat = false;
        newPlanet.axialTilt = 0.0f;
        newPlanet.yearLength = 365.0f;
        doc.planets.push_back(newPlanet);
        activePlanetIndex = (int)doc.planets.size() - 1;
        result.requestRebuildRoots = true;
        result.requestSave = true;
    }
    ImGui::Spacing();

    if (!doc.planets.empty()) {
        if (ImGui::BeginCombo("Pianeta Attivo", doc.planets[activePlanetIndex].name.c_str())) {
            for (int i = 0; i < (int)doc.planets.size(); ++i) {
                bool isSelected = (activePlanetIndex == i);
                if (ImGui::Selectable(doc.planets[i].name.c_str(), isSelected)) {
                    activePlanetIndex = i;
                    result.requestRebuildRoots = true;
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::Separator();
    }

    if (!doc.planets.empty() && activePlanetIndex >= 0 && activePlanetIndex < (int)doc.planets.size()) {
        auto& p = doc.planets[activePlanetIndex];
        char nameBuf[128];
        strncpy_s(nameBuf, p.name.c_str(), sizeof(nameBuf));
        if (ImGui::InputText("Nome Pianeta", nameBuf, sizeof(nameBuf))) {
            p.name = nameBuf;
            result.requestSave = true; // Potremmo non salvare a ogni carattere, ma teniamolo semplice
        }

        const char* sizeNames[] = { "Tiny", "Small", "Medium", "Large", "Huge", "Gigantic" };
        int currentSizeIndex = (int)p.planetSize;
        if (ImGui::Combo("Grandezza Pianeta", &currentSizeIndex, sizeNames, IM_ARRAYSIZE(sizeNames))) {
            p.planetSize = (fw::PlanetSize)currentSizeIndex;
            activeTemplateIndex = -1;
            
            // Auto-Repair al cambio dimensione
            auto valResult = fw::MapDocument::ValidateAndRepairDocument(doc);
            if (valResult.changed) {
                m_saveFlashMsg = "OOB H=" + std::to_string(valResult.outOfBoundsHidden) + " R=" + std::to_string(valResult.outOfBoundsRestored);
                m_saveFlashTimer = 3.0f;
            }
            
            result.requestRebuildRoots = true;
            result.requestSave = true;
        }
        
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.2f, 1.0f), "Parametri Astronomici (Sistema Solare)");
        if (ImGui::SliderFloat("Inclinazione Asse (Gradi)", &p.axialTilt, -90.0f, 90.0f, "%.2f")) {
            result.requestSave = true;
        }
        if (ImGui::SliderFloat("Durata Anno (Giorni)", &p.yearLength, 10.0f, 1000.0f, "%.0f")) {
            result.requestSave = true;
        }
        ImGui::Spacing();

        int N_lato = fw::PlanetMath::GetFaceResolution(p.planetSize);
        float S = 16.0f;
        float R = fw::PlanetMath::GetPlanetRadius(p.planetSize);
        int C_totale = 6 * (int)(N_lato * N_lato);
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Legge della Superficie Sferica (C = 4*PI*R^2 / S^2)");
        ImGui::Text("Dimensione Chunk Base: %.1f m | Risoluzione Faccia: %d x %d", S, (int)N_lato, (int)N_lato);
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Chunk Totali Generati: %d | Cursore Zoom Distanza: %.1f", C_totale, orbitDistance);
        ImGui::SameLine();
        if (ImGui::Button("Zoom +", ImVec2(60, 20))) orbitDistance = std::max(10.0f, orbitDistance - 25.0f);
        ImGui::SameLine();
        if (ImGui::Button("Zoom -", ImVec2(60, 20))) orbitDistance += 25.0f;
    }
    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Catalogo Chunk dalla Libreria (Progettati col Chunk Editor):");
    if (doc.terrainLibrary.empty()) {
        ImGui::TextDisabled("Nessun modello chunk presente nella libreria.");
    } else {
        ImGui::BeginChild("TemplateList", ImVec2(0, 140), true);
        int matchCount = 0;
        fw::PlanetSize currentPlanetSize = fw::PlanetSize::Small;
        if (!doc.planets.empty() && activePlanetIndex >= 0 && activePlanetIndex < (int)doc.planets.size()) {
            currentPlanetSize = doc.planets[activePlanetIndex].planetSize;
        }
        for (int i = 0; i < (int)doc.terrainLibrary.size(); ++i) {
            if (doc.terrainLibrary[i].planetSize != currentPlanetSize) continue;
            matchCount++;
            bool isSelected = (activeTemplateIndex == i);
            if (ImGui::Selectable((std::to_string(i+1) + ". " + doc.terrainLibrary[i].name + " [" + doc.terrainLibrary[i].id + "]").c_str(), isSelected)) {
                activeTemplateIndex = i;
            }
        }
        if (matchCount == 0) ImGui::TextDisabled("Nessun modello compatibile.");
        ImGui::EndChild();
    }
    ImGui::Spacing();

    if (ImGui::Button("📊 APRI TABELLA CHUNKS EXCEL (COLLOCAMENTO)", ImVec2(-1, 35))) {
        m_showPlacementTable = true;
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("📍 Punti di Inizio (Spawn Points)")) {
        if (!doc.planets.empty() && activePlanetIndex >= 0 && activePlanetIndex < (int)doc.planets.size()) {
            auto& p = doc.planets[activePlanetIndex];
            
            if (ImGui::Button("➕  Aggiungi Punto di Inizio", ImVec2(-1, 25))) {
                fw::SpawnPoint sp;
                sp.name = "Spawn " + std::to_string(p.spawnPoints.size() + 1);
                
                if (lastRayHit.hit) {
                    sp.faceIndex = lastRayHit.faceIndex;
                    float localU = (lastRayHit.uv.x * 2.0f) - 1.0f;
                    float localV = (lastRayHit.uv.y * 2.0f) - 1.0f;
                    float R = fw::PlanetMath::GetPlanetRadius(p.planetSize);
                    sp.localX = localU * R;
                    sp.localZ = localV * R;
                } else {
                    sp.faceIndex = 0; sp.localX = 0.0f; sp.localZ = 0.0f;
                }
                
                p.spawnPoints.push_back(sp);
                result.requestSave = true;
            }
            ImGui::Spacing();
            
            for (int i = 0; i < (int)p.spawnPoints.size(); ++i) {
                auto& sp = p.spawnPoints[i];
                ImGui::PushID(i);
                
                char spName[64];
                strncpy_s(spName, sp.name.c_str(), sizeof(spName));
                if (ImGui::InputText("Nome", spName, sizeof(spName))) sp.name = spName;
                
                const char* faceNames[] = { "+Z (Nord)", "-Z (Sud)", "+X (Est)", "-X (Ovest)", "+Y (Top/Cielo)", "-Y (Bottom/Nucleo)" };
                ImGui::Combo("Faccia Base", &sp.faceIndex, faceNames, IM_ARRAYSIZE(faceNames));
                
                float R = fw::PlanetMath::GetPlanetRadius(p.planetSize);
                ImGui::SliderFloat("Offset X", &sp.localX, -R, R, "%.1f");
                ImGui::SliderFloat("Offset Z", &sp.localZ, -R, R, "%.1f");
                ImGui::SliderFloat("Offset Y", &sp.heightOffset, 0.0f, R + 300.0f, "%.1f");
                
                float c[4] = { sp.color.r, sp.color.g, sp.color.b, sp.color.a };
                if (ImGui::ColorEdit4("Colore", c)) sp.color = glm::vec4(c[0], c[1], c[2], c[3]);
                
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.1f, 1.0f));
                if (ImGui::Button("Rimuovi", ImVec2(80, 20))) {
                    p.spawnPoints.erase(p.spawnPoints.begin() + i);
                    result.requestSave = true;
                    ImGui::PopStyleColor(); ImGui::PopID(); break;
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.5f, 0.2f, 1.0f));
                if (ImGui::Button("\xF0\x9F\x92\xBE Salva", ImVec2(-1, 20))) result.requestSave = true;
                ImGui::PopStyleColor();
                ImGui::Separator();
                ImGui::PopID();
            }
        }
    }

    ImGui::EndChild();

    ImGui::BeginChild("BottomBar", ImVec2(0, 130.0f), true);
    if (m_saveFlashTimer > 0.0f) {
        ImVec4 flashColor = (m_saveFlashMsg[0] == '\xE2') ? ImVec4(0.2f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.3f, 0.2f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, flashColor);
        ImGui::Text("%s  (saves/map/world_map.json)", m_saveFlashMsg.c_str());
        ImGui::PopStyleColor();
    }
    
    if (ImGui::Button("\xF0\x9F\x92\xBE SALVA MONDO E MAPPA 3D", ImVec2(-1, 30))) {
        result.requestSave = true;
        m_showSaveConfirmPopup = true;
    }
    if (ImGui::Button("🚀 ESPLORA MAPPA IN PRIMA PERSONA", ImVec2(-1, 30))) {
        result.documentChanged = true;
    }
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.3f, 1.0f));
    if (ImGui::Button("🌍 CARICA MAPPA E APRI IN FAIRWORLD PLAY", ImVec2(-1, 30))) {
        result.requestSave = true;
        result.goToPlayState = true;
    }
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::End();

    // --- Overlay Globe ---
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + leftWidth + 20.0f, viewport->Pos.y + 20.0f));
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::Begin("OverlayGlobe", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), ">>> GLOBO SFERICO LOD 3D (ANTEPRIMA PIANETA) <<<");
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 0.8f), "Drag mouse: ruota  |  Rotellina: zoom  |  W/S: zoom  |  A/D: ruota");
    ImGui::Separator();
    
    float pitchRad = glm::radians(orbitPitch);
    float yawRad   = glm::radians(orbitYaw);
    glm::vec3 camNorm;
    camNorm.x = cos(pitchRad) * sin(yawRad);
    camNorm.y = sin(pitchRad);
    camNorm.z = cos(pitchRad) * cos(yawRad);
    
    float latDeg = glm::degrees(asin(std::clamp(camNorm.y, -1.0f, 1.0f)));
    float lonDeg = glm::degrees(atan2(camNorm.x, camNorm.z));
    
    float camAx = std::abs(camNorm.x), camAy = std::abs(camNorm.y), camAz = std::abs(camNorm.z);
    const char* faceName = "";
    if      (camAz >= camAx && camAz >= camAy) faceName = camNorm.z > 0 ? "NORD (+Z)" : "SUD (-Z)";
    else if (camAx >= camAy && camAx >= camAz) faceName = camNorm.x > 0 ? "EST (+X)"  : "OVEST (-X)";
    else                                        faceName = camNorm.y > 0 ? "POLO NORD (+Y)" : "POLO SUD (-Y)";
    
    float surfaceDist = orbitDistance;
    if (!doc.planets.empty() && activePlanetIndex >= 0 && activePlanetIndex < (int)doc.planets.size()) {
        surfaceDist = orbitDistance - fw::PlanetMath::GetPlanetRadius(doc.planets[activePlanetIndex].planetSize);
    }
    
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "POSIZIONE CAMERA:");
    ImGui::Text("  Latitudine: %.1f deg  |  Longitudine: %.1f deg", latDeg, lonDeg);
    ImGui::Text("  Faccia:     %s", faceName);
    ImGui::Text("  Dist. dalla superficie: %.1f m  |  Orbita: %.1f m", surfaceDist, orbitDistance);
    
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "LOD (distanza divisione chunk):");
    float lodMul = lodSystem.GetDistanceMultiplier();
    if (ImGui::SliderFloat("Moltiplicatore LOD", &lodMul, 1.0f, 8.0f, "%.1fx")) {
        lodSystem.SetDistanceMultiplier(lodMul);
    }
    ImGui::End();

    if (m_showSaveConfirmPopup) {
        ImGui::OpenPopup("PianetaSalvato");
        m_showSaveConfirmPopup = false;
    }
    if (ImGui::BeginPopupModal("PianetaSalvato", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Mappa planetaria salvata con successo.");
        if (ImGui::Button("OK", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (m_showPlacementTable && !doc.planets.empty() && activePlanetIndex >= 0 && activePlanetIndex < (int)doc.planets.size()) {
        auto& currentPlanet = doc.planets[activePlanetIndex];
        int N_lato = fw::PlanetMath::GetFaceResolution(currentPlanet.planetSize);

        ImGui::SetNextWindowSize(ImVec2(1000, 600), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Tabella Collocamento Chunks - Excel Style", &m_showPlacementTable)) {
            
            // --- GLOBAL PLANET TOOLS ---
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "STRUMENTI GLOBALI PIANETA");
            ImGui::BeginGroup();
            if (ImGui::Button("Riempi Tutto il Pianeta", ImVec2(180, 30))) {
                if (activeTemplateIndex >= 0 && activeTemplateIndex < (int)doc.terrainLibrary.size()) {
                    std::map<int, int> gridLookup;
                    for (int i = 0; i < (int)currentPlanet.chunkInstances.size(); ++i) {
                        if (currentPlanet.chunkInstances[i].isGridAligned && currentPlanet.chunkInstances[i].isActive) {
                            int key = currentPlanet.chunkInstances[i].faceIndex * 1000000 + currentPlanet.chunkInstances[i].gridY * 1000 + currentPlanet.chunkInstances[i].gridX;
                            gridLookup[key] = i;
                        }
                    }
                    
                    int addedChunks = 0;
                    for (int f = 0; f < 6; ++f) {
                        for (int row = 0; row < N_lato; ++row) {
                            for (int col = 0; col < N_lato; ++col) {
                                int key = f * 1000000 + row * 1000 + col;
                                if (gridLookup.find(key) == gridLookup.end()) {
                                    fw::PlanetChunkInstance addInst;
                                    addInst.name = "Chunk_" + std::to_string(f) + "_" + std::to_string(col) + "_" + std::to_string(row);
                                    addInst.templateId = doc.terrainLibrary[activeTemplateIndex].id;
                                    addInst.isGridAligned = true;
                                    addInst.faceIndex = f;
                                    addInst.gridX = col;
                                    addInst.gridY = row;
                                    addInst.isActive = true;
                                    
                                    float cx = (col + 0.5f) / N_lato * 2.0f - 1.0f;
                                    float cy = 1.0f - (row + 0.5f) / N_lato * 2.0f;
                                    glm::vec3 dir(0.0f);
                                    switch(f) {
                                        case 0: dir = glm::vec3(cx, cy, 1.0f); break;
                                        case 1: dir = glm::vec3(-cx, cy, -1.0f); break;
                                        case 2: dir = glm::vec3(1.0f, cy, -cx); break;
                                        case 3: dir = glm::vec3(-1.0f, cy, cx); break;
                                        case 4: dir = glm::vec3(cx, 1.0f, -cy); break;
                                        case 5: dir = glm::vec3(cx, -1.0f, cy); break;
                                    }
                                    dir = glm::normalize(dir);
                                    addInst.eulerAngles.x = glm::degrees(asin(dir.y));
                                    addInst.eulerAngles.y = glm::degrees(atan2(dir.z, dir.x));
                                    addInst.angularRadius = (glm::pi<float>() / 2.0f) / N_lato * 0.6f;
                                    currentPlanet.chunkInstances.push_back(addInst);
                                    addedChunks++;
                                }
                            }
                        }
                    }
                    if (addedChunks > 0) {
                        fw::MapDocument::ValidateAndRepairDocument(doc); // Validate
                        currentPlanet.structuralDirty = true;
                        result.documentChanged = true;
                        result.requestRebuildRoots = true;
                        m_saveFlashMsg = "\xE2\x9C\x94 " + std::to_string(addedChunks) + " Chunk aggiunti.";
                        m_saveFlashTimer = 3.0f;
                    } else {
                        m_saveFlashMsg = "\xE2\x9C\x94 Pianeta gia' completo (0 mod)";
                        m_saveFlashTimer = 3.0f;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Svuota Tutto il Pianeta", ImVec2(180, 30))) {
                currentPlanet.chunkInstances.clear();
                currentPlanet.structuralDirty = true;
                result.documentChanged = true;
                result.requestRebuildRoots = true;
            }
            ImGui::EndGroup();
            ImGui::Separator();
            // --- END GLOBAL PLANET TOOLS ---

            std::map<int, int> gridLookup;
            for (int i = 0; i < (int)currentPlanet.chunkInstances.size(); ++i) {
                if (currentPlanet.chunkInstances[i].isGridAligned && currentPlanet.chunkInstances[i].isActive) {
                    int key = currentPlanet.chunkInstances[i].faceIndex * 1000000 + currentPlanet.chunkInstances[i].gridY * 1000 + currentPlanet.chunkInstances[i].gridX;
                    gridLookup[key] = i;
                }
            }

            int toDeleteIndex = -1;
            bool shouldPushToAdd = false;
            fw::PlanetChunkInstance toAdd;

            if (ImGui::BeginTabBar("FacesTabBar")) {
                const char* faceNames[] = { "+Z (Nord)", "-Z (Sud)", "+X (Est)", "-X (Ovest)", "+Y (Top/Cielo)", "-Y (Bottom/Nucleo)" };
                for (int f = 0; f < 6; ++f) {
                    if (ImGui::BeginTabItem(faceNames[f])) {
                        ImGui::Text("Faccia %d - Risoluzione Griglia: %d x %d Cella", f, N_lato, N_lato);
                        ImGui::Spacing();
                        
                        ImGui::BeginChild(std::string("GridScroll_" + std::to_string(f)).c_str(), ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
                        ImGuiListClipper clipper;
                        clipper.Begin(N_lato, 35.0f);
                        while (clipper.Step()) {
                            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                                for (int col = 0; col < N_lato; ++col) {
                                    int key = f * 1000000 + row * 1000 + col;
                                    auto it = gridLookup.find(key);
                                    ImGui::PushID(key);
                                    if (col > 0) ImGui::SameLine(0, 2.0f);

                                    if (it != gridLookup.end()) {
                                        auto& inst = currentPlanet.chunkInstances[it->second];
                                        std::string shortName = "CH";
                                        for (const auto& t : doc.terrainLibrary) {
                                            if (t.id == inst.templateId) { shortName = t.name.substr(0, std::min<size_t>(t.name.size(), 4)); break; }
                                        }
                                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
                                        if (ImGui::Button(shortName.c_str(), ImVec2(45, 30))) {
                                            toDeleteIndex = it->second;
                                        }
                                        ImGui::PopStyleColor();
                                    } else {
                                        if (ImGui::Button("---", ImVec2(45, 30))) {
                                            if (activeTemplateIndex >= 0 && activeTemplateIndex < (int)doc.terrainLibrary.size()) {
                                                toAdd.name = "Chunk_" + std::to_string(f) + "_" + std::to_string(col) + "_" + std::to_string(row);
                                                toAdd.templateId = doc.terrainLibrary[activeTemplateIndex].id;
                                                toAdd.isGridAligned = true;
                                                toAdd.faceIndex = f;
                                                toAdd.gridX = col;
                                                toAdd.gridY = row;
                                                
                                                float cx = (col + 0.5f) / N_lato * 2.0f - 1.0f;
                                                float cy = 1.0f - (row + 0.5f) / N_lato * 2.0f;
                                                glm::vec3 dir(0.0f);
                                                switch(f) {
                                                    case 0: dir = glm::vec3(cx, cy, 1.0f); break;
                                                    case 1: dir = glm::vec3(-cx, cy, -1.0f); break;
                                                    case 2: dir = glm::vec3(1.0f, cy, -cx); break;
                                                    case 3: dir = glm::vec3(-1.0f, cy, cx); break;
                                                    case 4: dir = glm::vec3(cx, 1.0f, -cy); break;
                                                    case 5: dir = glm::vec3(cx, -1.0f, cy); break;
                                                }
                                                dir = glm::normalize(dir);
                                                toAdd.eulerAngles.x = glm::degrees(asin(dir.y));
                                                toAdd.eulerAngles.y = glm::degrees(atan2(dir.z, dir.x));
                                                toAdd.angularRadius = (glm::pi<float>() / 2.0f) / N_lato * 0.6f;
                                                shouldPushToAdd = true;
                                            }
                                        }
                                    }
                                    ImGui::PopID();
                                }
                            }
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }

            if (toDeleteIndex >= 0) {
                currentPlanet.chunkInstances.erase(currentPlanet.chunkInstances.begin() + toDeleteIndex);
                currentPlanet.MarkStructuralChange();
                result.documentChanged = true;
            }
            if (shouldPushToAdd) {
                currentPlanet.chunkInstances.push_back(toAdd);
                currentPlanet.MarkStructuralChange();
                result.documentChanged = true;
            }
        }
        ImGui::End();
    }
    
    // Execute save operation at the very end of UI interactions if requested
    if (result.requestSave) {
        DoSave(context->projectManager);
    }
    
    return result;
}
