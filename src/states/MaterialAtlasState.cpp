#include "pch.h"
#include "MaterialAtlasState.h"
#include "SharedContext.h"
#include "StateManager.h"
#include "DeviceManager.h"
#include "FAIRWORLD.h"
#include "imgui.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace fw {

// =============================================================================
// HELPER FORMATTING & CONVERSION FUNCTIONS
// =============================================================================

static std::string ToLowerString(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

static std::string FormatDouble(double val, int precision = 3) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << val;
    std::string s = ss.str();
    if (s.find('.') != std::string::npos) {
        s.erase(s.find_last_not_of('0') + 1, std::string::npos);
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

static std::string FormatPressureHumanReadable(double pressurePa) {
    if (std::abs(pressurePa) >= 1e9) {
        return FormatDouble(pressurePa / 1e9, 2) + " GPa";
    }
    if (std::abs(pressurePa) >= 1e6) {
        return FormatDouble(pressurePa / 1e6, 2) + " MPa";
    }
    if (std::abs(pressurePa) >= 1e3) {
        return FormatDouble(pressurePa / 1e3, 2) + " kPa";
    }
    return FormatDouble(pressurePa, 1) + " Pa";
}

static std::string FormatTemperatureHumanReadable(double kelvin) {
    double celsius = MaterialAuthoringWorkflow::CelsiusFromKelvin(kelvin);
    return FormatDouble(celsius, 2) + " °C (" + FormatDouble(kelvin, 2) + " K)";
}

static void RenderBadge(const char* text, const ImVec4& bgColor, const ImVec4& textColor) {
    ImVec2 padding(6.0f, 2.0f);
    ImVec2 textSize = ImGui::CalcTextSize(text);
    ImVec2 badgeSize(textSize.x + padding.x * 2.0f, textSize.y + padding.y * 2.0f);

    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + badgeSize.x, p0.y + badgeSize.y);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(p0, p1, ImGui::ColorConvertFloat4ToU32(bgColor), 3.0f);
    drawList->AddText(ImVec2(p0.x + padding.x, p0.y + padding.y), ImGui::ColorConvertFloat4ToU32(textColor), text);

    ImGui::Dummy(badgeSize);
}

// =============================================================================
// MATERIAL ATLAS STATE IMPLEMENTATION (MA-2: ATLAS & INTERACTIVE AUTHORING)
// =============================================================================

MaterialAtlasState::MaterialAtlasState(SharedContext* context)
    : m_context(context)
{
    std::cout << "[MaterialAtlasState] Costruito (MA-2: Interactive Scientific Material Atlas & Editor).\n";
}

MaterialAtlasState::~MaterialAtlasState() {
    std::cout << "[MaterialAtlasState] Distrutto. Isolamento memoria garantito.\n";
}

bool MaterialAtlasState::Init() {
    std::cout << "[MaterialAtlasState] Inizializzazione modulo completata.\n";
    if (m_context && m_context->deviceManager) {
        m_context->deviceManager->requireFreeCursor = true;
    }

    // Inizializza il workflow di authoring riutilizzando il registry esistente (P0.3C1)
    if (m_context && m_context->scientificMaterialRegistry) {
        m_authoringWorkflow = std::make_unique<MaterialAuthoringWorkflow>(*m_context->scientificMaterialRegistry);
        
        // Carica il catalogo personalizzato se presente su disco
        if (std::filesystem::exists(m_customCatalogPath)) {
            std::string err;
            if (!m_authoringWorkflow->ReloadCatalogFromDisk(m_customCatalogPath, &err)) {
                std::cerr << "[MaterialAtlasState] Avviso caricamento catalogo custom: " << err << "\n";
            }
        }
    }

    InvalidateCatalogCache();
    return true;
}

void MaterialAtlasState::OnEnter() {
    if (m_context && m_context->deviceManager) {
        m_context->deviceManager->requireFreeCursor = true;
    }
    InvalidateCatalogCache();
}

void MaterialAtlasState::OnExit() {
}

void MaterialAtlasState::SetStatusNotification(const std::string& msg, bool isError, float duration) {
    m_statusNotification = msg;
    m_statusIsError = isError;
    m_statusTimer = duration;
}

void MaterialAtlasState::Update(float dt) {
    if (m_context && m_context->engine) {
        m_context->engine->Update(dt);
    }

    // Gestione timer notifiche di stato temporanee
    if (m_statusTimer > 0.0f) {
        m_statusTimer -= dt;
        if (m_statusTimer <= 0.0f) {
            m_statusNotification.clear();
        }
    }

    // Navigazione ESC: se in modalità authoring con modifiche, richiede conferma; altrimenti torna all'Hub
    bool escPressed = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (escPressed && m_context && m_context->stateManager) {
        static bool s_escWasDown = false;
        if (!s_escWasDown) {
            s_escWasDown = true;
            if (m_authoringMode && m_authoringWorkflow && m_authoringWorkflow->IsDirty()) {
                m_showDiscardConfirmModal = true;
            } else if (m_authoringMode) {
                m_authoringMode = false;
                if (m_authoringWorkflow) m_authoringWorkflow->DiscardSession();
            } else {
                m_context->stateManager->PopState();
            }
            return;
        }
    } else {
        static bool s_escWasDown = false;
        s_escWasDown = false;
    }

    // Invalida la cache se la revisione monotona del registro scientifico è mutata
    if (m_context && m_context->scientificMaterialRegistry) {
        uint64_t rev = m_context->scientificMaterialRegistry->GetRevision();
        if (rev != m_lastRegistryRevision) {
            InvalidateCatalogCache();
        }
    }
}

void MaterialAtlasState::InvalidateCatalogCache() {
    m_cacheDirty = true;
}

void MaterialAtlasState::RebuildCatalogCache() {
    m_cachedCatalog.clear();
    m_cacheDirty = false;

    if (!m_context || !m_context->scientificMaterialRegistry) {
        return;
    }

    auto snap = m_context->scientificMaterialRegistry->GetSnapshotHandle();
    if (!snap.IsValid()) {
        return;
    }

    m_lastRegistryRevision = snap->revision;
    const auto& allMats = snap->GetAllMaterials();
    m_cachedCatalog.reserve(allMats.size());

    std::string queryLower = ToLowerString(m_searchQuery);

    for (const auto& [id, def] : allMats) {
        // Filtro Categoria (ScientificKind)
        if (m_filterCategory > 0) {
            auto reqKind = static_cast<ScientificKind>(m_filterCategory);
            if (def.kind != reqKind) {
                continue;
            }
        }

        // Filtro Origine (Built-in vs Custom)
        bool isCustomMat = (def.id >= CUSTOM_SCIENTIFIC_MATERIAL_ID_BASE || def.isCustom);
        if (m_filterOrigin == 1 && isCustomMat) {
            continue; // Solo Built-in (<1000)
        }
        if (m_filterOrigin == 2 && !isCustomMat) {
            continue; // Solo Custom (>=1000)
        }

        // Filtro Stato Ciclo di Vita
        if (m_filterLifecycle == 1 && def.lifecycleState != MaterialLifecycleState::Published) {
            continue;
        }
        if (m_filterLifecycle == 2 && def.lifecycleState != MaterialLifecycleState::Draft) {
            continue;
        }
        if (m_filterLifecycle == 3 && def.lifecycleState != MaterialLifecycleState::Deprecated) {
            continue;
        }

        // Filtro Disponibilità Block Maker
        if (m_filterBlockMaker == 1 && !def.enabledInBlockMaker) {
            continue;
        }
        if (m_filterBlockMaker == 2 && def.enabledInBlockMaker) {
            continue;
        }

        // Filtro Ricerca Testuale (Nome, ID numerico, URI canonico, formula chimica)
        if (!queryLower.empty()) {
            std::string nameLower = ToLowerString(def.displayName);
            std::string canonLower = ToLowerString(def.canonicalId);
            std::string idStr = std::to_string(def.id);
            std::string formulaLower = ToLowerString(def.chemicalFormula);

            bool match = (nameLower.find(queryLower) != std::string::npos) ||
                         (canonLower.find(queryLower) != std::string::npos) ||
                         (idStr.find(queryLower) != std::string::npos) ||
                         (formulaLower.find(queryLower) != std::string::npos);

            if (!match) {
                continue;
            }
        }

        CachedCatalogItem item;
        item.id = def.id;
        item.canonicalId = def.canonicalId;
        item.displayName = def.displayName;
        item.chemicalFormula = def.chemicalFormula;
        item.kind = def.kind;
        item.aggregationState = def.aggregationState;
        item.lifecycleState = def.lifecycleState;
        item.isCustom = isCustomMat;
        item.enabledInBlockMaker = def.enabledInBlockMaker;

        m_cachedCatalog.push_back(std::move(item));
    }

    // Ordinamento vista
    switch (m_sortOrder) {
        case 0: // ID Crescente
            std::sort(m_cachedCatalog.begin(), m_cachedCatalog.end(),
                      [](const CachedCatalogItem& a, const CachedCatalogItem& b) { return a.id < b.id; });
            break;
        case 1: // ID Decrescente
            std::sort(m_cachedCatalog.begin(), m_cachedCatalog.end(),
                      [](const CachedCatalogItem& a, const CachedCatalogItem& b) { return a.id > b.id; });
            break;
        case 2: // Nome A-Z
            std::sort(m_cachedCatalog.begin(), m_cachedCatalog.end(),
                      [](const CachedCatalogItem& a, const CachedCatalogItem& b) {
                          return ToLowerString(a.displayName) < ToLowerString(b.displayName);
                      });
            break;
        case 3: // Nome Z-A
            std::sort(m_cachedCatalog.begin(), m_cachedCatalog.end(),
                      [](const CachedCatalogItem& a, const CachedCatalogItem& b) {
                          return ToLowerString(a.displayName) > ToLowerString(b.displayName);
                      });
            break;
        default:
            break;
    }

    // Selezione attiva di default
    if (m_selectedMaterialId != INVALID_SCIENTIFIC_MATERIAL_ID) {
        if (!m_context->scientificMaterialRegistry->HasMaterial(m_selectedMaterialId)) {
            m_selectedMaterialId = INVALID_SCIENTIFIC_MATERIAL_ID;
        }
    }

    if (m_selectedMaterialId == INVALID_SCIENTIFIC_MATERIAL_ID && !m_cachedCatalog.empty()) {
        m_selectedMaterialId = m_cachedCatalog[0].id;
    }
}

void MaterialAtlasState::Render() {
    if (m_cacheDirty) {
        RebuildCatalogCache();
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));

    // Tema cyber-slate scientifico
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.09f, 0.11f, 0.14f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.20f, 0.25f, 0.32f, 1.0f));

    if (ImGui::Begin("FAIRWORLD_MATERIAL_ATLAS_ROOT", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
    {
        RenderTopBar();

        ImGui::Separator();
        ImGui::Spacing();

        // Area di lavoro a due pannelli orizzontali adattivi
        ImVec2 contentAvail = ImGui::GetContentRegionAvail();
        float leftWidth = std::max(380.0f, contentAvail.x * 0.32f);
        float rightWidth = contentAvail.x - leftWidth - 16.0f;
        float paneHeight = contentAvail.y;

        // Pannello A: Catalogo Materiali
        RenderCatalogPane(leftWidth, paneHeight);

        ImGui::SameLine(0, 16.0f);

        // Pannello B: Dettaglio Scientifico (Inspector o Interactive Editor)
        RenderDetailPane(rightWidth, paneHeight);

        // Rendering delle finestre modali interattive
        RenderModals();

        ImGui::End();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

// =============================================================================
// SUB-RENDERERS: TOP BAR & STATUS INDICATOR
// =============================================================================

void MaterialAtlasState::RenderTopBar() {
    ImVec2 avail = ImGui::GetContentRegionAvail();

    // 1. Titolo e Badge Modulo
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.75f, 1.0f), "FAIRWORLD");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.70f, 0.75f, 0.82f, 1.0f), "—");
    ImGui::SameLine();
    ImGui::SetWindowFontScale(1.15f);
    ImGui::TextColored(ImVec4(0.95f, 0.98f, 1.0f, 1.0f), "SCIENTIFIC MATERIAL ATLAS");
    ImGui::SetWindowFontScale(1.0f);

    ImGui::SameLine(0, 12.0f);
    if (m_authoringMode) {
        RenderBadge("MA-2 : AUTHORING SESSION ATTIVA", ImVec4(0.50f, 0.32f, 0.05f, 1.0f), ImVec4(1.0f, 0.90f, 0.40f, 1.0f));
    } else {
        RenderBadge("MA-2 : CATALOG & AUTHORING", ImVec4(0.15f, 0.35f, 0.40f, 1.0f), ImVec4(0.60f, 0.95f, 0.90f, 1.0f));
    }

    // 2. Indicatore di Stato a 3 Livelli (P0.3C2)
    ImGui::SameLine(0, 10.0f);
    if (m_authoringWorkflow && m_authoringWorkflow->IsDirty()) {
        RenderBadge("WORKING COPY MODIFICATA (*)", ImVec4(0.60f, 0.35f, 0.05f, 1.0f), ImVec4(1.0f, 0.85f, 0.30f, 1.0f));
    } else if (m_authoringWorkflow && m_authoringWorkflow->IsCatalogUnsavedOnDisk()) {
        RenderBadge("REGISTRO NON SALVATO SU DISCO (*)", ImVec4(0.65f, 0.20f, 0.15f, 1.0f), ImVec4(1.0f, 0.70f, 0.60f, 1.0f));
    } else {
        RenderBadge("CATALOGO SINCRONIZZATO", ImVec4(0.10f, 0.35f, 0.20f, 1.0f), ImVec4(0.40f, 0.95f, 0.60f, 1.0f));
    }

    // 3. Notifica temporanea di stato (se presente)
    if (!m_statusNotification.empty()) {
        ImGui::SameLine(0, 14.0f);
        ImVec4 noteCol = m_statusIsError ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f) : ImVec4(0.45f, 0.95f, 0.60f, 1.0f);
        ImGui::TextColored(noteCol, "[ %s ]", m_statusNotification.c_str());
    }

    // 4. Pulsanti di azione TopBar (a destra)
    float btnWidth = 145.0f;
    float btnHeight = 30.0f;
    float rightPos = avail.x - (btnWidth * 3 + 24.0f);

    if (rightPos > 620.0f) {
        ImGui::SameLine(rightPos);
    }

    // Bottone Nuovo Materiale Custom
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.40f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.52f, 0.38f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.14f, 0.32f, 0.24f, 1.0f));
    if (ImGui::Button("+ Nuovo Materiale", ImVec2(btnWidth, btnHeight))) {
        m_showNewMaterialModal = true;
    }
    ImGui::PopStyleColor(3);

    // Bottone Salva Catalogo su Disco
    ImGui::SameLine(0, 8.0f);
    bool hasUnsaved = m_authoringWorkflow && m_authoringWorkflow->IsCatalogUnsavedOnDisk();
    if (hasUnsaved) {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.60f, 0.25f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.32f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.45f, 0.18f, 0.10f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.25f, 0.32f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.34f, 0.42f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.20f, 0.26f, 1.0f));
    }

    if (ImGui::Button("Salva Catalogo", ImVec2(btnWidth, btnHeight))) {
        if (m_authoringWorkflow) {
            std::string saveErr;
            bool ok = m_authoringWorkflow->SaveCatalogToDisk(m_customCatalogPath, &saveErr, false);
            if (ok) {
                SetStatusNotification("Catalogo salvato con successo su disco!");
            } else {
                if (saveErr.find("Lost update") != std::string::npos || saveErr.find("concorrenza") != std::string::npos || saveErr.find("Conflitto") != std::string::npos) {
                    m_lostUpdateError = saveErr;
                    m_showLostUpdateModal = true;
                } else {
                    SetStatusNotification("Errore di salvataggio: " + saveErr, true);
                }
            }
        }
    }
    ImGui::PopStyleColor(3);

    // Bottone Torna all'Hub
    ImGui::SameLine(0, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.28f, 0.38f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.40f, 0.55f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.22f, 0.30f, 1.0f));
    if (ImGui::Button("<-- Torna all'Hub", ImVec2(btnWidth, btnHeight))) {
        if (m_authoringMode && m_authoringWorkflow && m_authoringWorkflow->IsDirty()) {
            m_showDiscardConfirmModal = true;
        } else {
            if (m_context && m_context->stateManager) {
                m_context->stateManager->PopState();
            }
        }
    }
    ImGui::PopStyleColor(3);
}

// =============================================================================
// SUB-RENDERERS: CATALOG PANE
// =============================================================================

void MaterialAtlasState::RenderCatalogPane(float width, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.14f, 0.18f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));

    if (ImGui::BeginChild("CatalogPane", ImVec2(width, height), true)) {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.75f, 1.0f), "A — CATALOGO MATERIALI");
        ImGui::Spacing();

        // 1. Barra di ricerca
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextWithHint("##SearchQuery", "Cerca per nome, formula, ID, URI...", m_searchQuery, sizeof(m_searchQuery))) {
            InvalidateCatalogCache();
        }

        ImGui::Spacing();

        // 2. Filtri
        ImGui::Columns(2, "CatalogFilters", false);
        ImGui::SetColumnWidth(0, width * 0.48f);

        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.72f, 1.0f), "Categoria:");
        const char* categories[] = {
            "Tutte", "Element", "Compound", "Mineral", "Rock", "Mixture", "Alloy", "Organic", "Composite"
        };
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##FilterCat", &m_filterCategory, categories, IM_ARRAYSIZE(categories))) {
            InvalidateCatalogCache();
        }

        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.72f, 1.0f), "Stato:");
        const char* lifecycles[] = { "Tutti", "Published", "Draft", "Deprecated" };
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##FilterLife", &m_filterLifecycle, lifecycles, IM_ARRAYSIZE(lifecycles))) {
            InvalidateCatalogCache();
        }

        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.72f, 1.0f), "Origine:");
        const char* origins[] = { "Tutti", "Built-in (<1000)", "Custom (>=1000)" };
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##FilterOrig", &m_filterOrigin, origins, IM_ARRAYSIZE(origins))) {
            InvalidateCatalogCache();
        }

        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.72f, 1.0f), "Ordinamento:");
        const char* sortOrders[] = { "ID Crescente", "ID Decrescente", "Nome A-Z", "Nome Z-A" };
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##SortOrder", &m_sortOrder, sortOrders, IM_ARRAYSIZE(sortOrders))) {
            InvalidateCatalogCache();
        }

        ImGui::Columns(1);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 3. Elenco scrollabile virtualizzato con ImGuiListClipper
        if (ImGui::BeginChild("CatalogListScroll", ImVec2(0, 0), false)) {
            if (m_cachedCatalog.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.70f, 1.0f), "Nessun materiale corrisponde ai filtri.");
            } else {
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(m_cachedCatalog.size()));

                while (clipper.Step()) {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                        const auto& item = m_cachedCatalog[i];
                        bool isSelected = (item.id == m_selectedMaterialId);
                        bool isBeingEdited = m_authoringMode && m_authoringWorkflow && 
                                             (m_authoringWorkflow->GetWorkingCopy().id == item.id);

                        ImGui::PushID(static_cast<int>(item.id));

                        if (isBeingEdited) {
                            ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.40f, 0.28f, 0.08f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.50f, 0.35f, 0.12f, 1.0f));
                        } else if (isSelected) {
                            ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.18f, 0.35f, 0.45f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.24f, 0.42f, 0.55f, 1.0f));
                        } else {
                            ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.14f, 0.17f, 0.22f, 0.7f));
                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.20f, 0.25f, 0.32f, 1.0f));
                        }

                        char labelBuf[128];
                        snprintf(labelBuf, sizeof(labelBuf), "##MatItem_%u", item.id);

                        if (ImGui::Selectable(labelBuf, isSelected, ImGuiSelectableFlags_AllowOverlap, ImVec2(0, 48.0f))) {
                            if (!m_authoringMode || (m_authoringWorkflow && !m_authoringWorkflow->IsDirty())) {
                                m_selectedMaterialId = item.id;
                                if (m_authoringMode && m_authoringWorkflow) {
                                    m_authoringMode = false;
                                    m_authoringWorkflow->DiscardSession();
                                }
                            } else {
                                SetStatusNotification("Sessione di authoring attiva con modifiche non committate!", true);
                            }
                        }

                        ImGui::PopStyleColor(2);

                        // Contenuto sovrapposto (Badge e Denominazione)
                        ImGui::SameLine(10.0f);
                        ImGui::BeginGroup();
                        
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
                        ImGui::TextColored(isSelected ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.85f, 0.90f, 0.95f, 1.0f),
                                           "%s", item.displayName.c_str());
                        
                        if (!item.chemicalFormula.empty()) {
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.55f, 0.70f, 0.85f, 0.9f), "[%s]", item.chemicalFormula.c_str());
                        }

                        char idTag[32];
                        snprintf(idTag, sizeof(idTag), "#%u", item.id);
                        RenderBadge(idTag, ImVec4(0.18f, 0.22f, 0.28f, 1.0f), ImVec4(0.70f, 0.75f, 0.82f, 1.0f));

                        ImGui::SameLine(0, 4.0f);
                        if (item.isCustom) {
                            RenderBadge("CUSTOM", ImVec4(0.55f, 0.35f, 0.05f, 1.0f), ImVec4(1.0f, 0.85f, 0.40f, 1.0f));
                        } else {
                            RenderBadge("BUILT-IN", ImVec4(0.12f, 0.35f, 0.45f, 1.0f), ImVec4(0.40f, 0.85f, 0.95f, 1.0f));
                        }

                        ImGui::SameLine(0, 4.0f);
                        if (item.lifecycleState == MaterialLifecycleState::Published) {
                            RenderBadge("PUBLISHED", ImVec4(0.12f, 0.40f, 0.22f, 1.0f), ImVec4(0.45f, 0.95f, 0.60f, 1.0f));
                        } else if (item.lifecycleState == MaterialLifecycleState::Draft) {
                            RenderBadge("DRAFT", ImVec4(0.45f, 0.40f, 0.10f, 1.0f), ImVec4(0.95f, 0.90f, 0.35f, 1.0f));
                        } else {
                            RenderBadge("DEPRECATED", ImVec4(0.45f, 0.15f, 0.15f, 1.0f), ImVec4(0.95f, 0.50f, 0.50f, 1.0f));
                        }

                        ImGui::SameLine(0, 4.0f);
                        RenderBadge(ScientificKindToString(item.kind), ImVec4(0.20f, 0.22f, 0.28f, 1.0f), ImVec4(0.75f, 0.80f, 0.88f, 1.0f));

                        ImGui::EndGroup();

                        ImGui::PopID();
                    }
                }
                clipper.End();
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(1);
}

// =============================================================================
// SUB-RENDERERS: DETAIL PANE & AUTHORING WORKFLOW
// =============================================================================

void MaterialAtlasState::RenderAuthoringToolbar(const ScientificMaterialDef* currentDef) {
    if (!currentDef) return;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.12f, 0.16f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::BeginChild("AuthoringToolbarChild", ImVec2(0, 44.0f), true);

    bool isCustom = currentDef->isCustom || currentDef->id >= CUSTOM_SCIENTIFIC_MATERIAL_ID_BASE;

    // 1. Modifica Materiale Custom
    if (isCustom) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.40f, 0.55f, 1.0f));
        if (ImGui::Button("Modifica Materiale", ImVec2(140, 28))) {
            if (m_authoringWorkflow) {
                std::string err;
                if (m_authoringWorkflow->BeginEditExisting(currentDef->id, &err)) {
                    m_authoringMode = true;
                    m_showValidationFeedback = false;
                } else {
                    SetStatusNotification("Errore avvio modifica: " + err, true);
                }
            }
        }
        ImGui::PopStyleColor();
    } else {
        // Built-in protetto da scrittura
        ImGui::BeginDisabled();
        ImGui::Button("Modifica (Protetta)", ImVec2(140, 28));
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("I materiali built-in sono protetti. Usa 'Clona come Custom' per creare una variante modificabile.");
        }
    }

    // 2. Clona come Custom
    ImGui::SameLine(0, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.35f, 0.30f, 1.0f));
    if (ImGui::Button("Clona come Custom", ImVec2(140, 28))) {
        m_showCloneModal = true;
        std::string candidateName = currentDef->displayName + " (Copia)";
        std::string candidateCanon = currentDef->canonicalId + "_clone";
        strncpy(m_cloneDisplayName, candidateName.c_str(), sizeof(m_cloneDisplayName));
        strncpy(m_cloneCanonicalId, candidateCanon.c_str(), sizeof(m_cloneCanonicalId));
    }
    ImGui::PopStyleColor();

    // 3. Controlli Lifecycle veloci (per materiali custom)
    if (isCustom) {
        if (currentDef->lifecycleState == MaterialLifecycleState::Draft) {
            ImGui::SameLine(0, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.25f, 1.0f));
            if (ImGui::Button("Pubblica", ImVec2(90, 28))) {
                if (m_authoringWorkflow) {
                    std::string err;
                    if (m_authoringWorkflow->PublishMaterialDirect(currentDef->id, &err)) {
                        InvalidateCatalogCache();
                        SetStatusNotification("Materiale pubblicato con successo!");
                    } else {
                        SetStatusNotification("Pubblicazione fallita: " + err, true);
                    }
                }
            }
            ImGui::PopStyleColor();
        }

        if (currentDef->lifecycleState != MaterialLifecycleState::Deprecated) {
            ImGui::SameLine(0, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.20f, 0.20f, 1.0f));
            if (ImGui::Button("Depreca", ImVec2(85, 28))) {
                m_materialToDeprecate = currentDef->id;
                m_showDeprecateConfirmModal = true;
            }
            ImGui::PopStyleColor();
        }

        // Toggle disponibilità Block Maker
        ImGui::SameLine(0, 8.0f);
        if (currentDef->enabledInBlockMaker) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.32f, 0.42f, 1.0f));
            if (ImGui::Button("In Block Maker: SI", ImVec2(145, 28))) {
                if (m_authoringWorkflow) {
                    std::string err;
                    m_authoringWorkflow->SetEnabledInBlockMakerDirect(currentDef->id, false, &err);
                    InvalidateCatalogCache();
                }
            }
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.30f, 0.35f, 1.0f));
            if (ImGui::Button("In Block Maker: NO", ImVec2(145, 28))) {
                if (m_authoringWorkflow) {
                    std::string err;
                    if (!m_authoringWorkflow->SetEnabledInBlockMakerDirect(currentDef->id, true, &err)) {
                        SetStatusNotification("Abilitazione fallita: " + err, true);
                    } else {
                        InvalidateCatalogCache();
                    }
                }
            }
            ImGui::PopStyleColor();
        }
    } else {
        ImGui::SameLine(0, 12.0f);
        RenderBadge("BUILT-IN STANDARD (SOLA LETTURA)", ImVec4(0.18f, 0.22f, 0.28f, 1.0f), ImVec4(0.60f, 0.85f, 0.95f, 1.0f));
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void MaterialAtlasState::RenderDetailPane(float width, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.14f, 0.18f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));

    if (ImGui::BeginChild("DetailPane", ImVec2(width, height), true)) {
        if (m_authoringMode && m_authoringWorkflow && m_authoringWorkflow->HasActiveSession()) {
            // MODALITÀ B: INTERACTIVE SCIENTIFIC EDITOR (P0.3C2)
            RenderAuthoringEditorPane(width, height);
        } else {
            // MODALITÀ A: READ-ONLY INSPECTOR + TOOLBAR DI AZIONE
            if (m_selectedMaterialId == INVALID_SCIENTIFIC_MATERIAL_ID || !m_context || !m_context->scientificMaterialRegistry) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Nessun materiale selezionato.");
            } else if (!m_context->scientificMaterialRegistry->HasMaterial(m_selectedMaterialId)) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.85f, 0.4f, 0.4f, 1.0f), "Materiale ID %u non trovato nel registry.", m_selectedMaterialId);
            } else {
                const auto& def = m_context->scientificMaterialRegistry->GetMaterial(m_selectedMaterialId);

                // Toolbar rapida di authoring
                RenderAuthoringToolbar(&def);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Scheda Scientifica
                RenderScientificIdentity(def);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                RenderScientificClassification(def);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                RenderScientificComposition(def);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                RenderPhysicalPropertiesTable(def);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (!def.extendedProperties.Empty()) {
                    RenderExtendedPropertiesSection(def);
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                }

                RenderLifecycleAndProvenance(def);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Appearance PBR (ortogonale e disaccoppiata)
                RenderAppearanceSection(def);
            }
        }
    }
    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(1);
}

// =============================================================================
// INTERACTIVE AUTHORING EDITOR IMPLEMENTATION (P0.3C2)
// =============================================================================

void MaterialAtlasState::RenderAuthoringEditorPane(float width, float height) {
    if (!m_authoringWorkflow) return;

    auto& wc = m_authoringWorkflow->GetWorkingCopy();

    // 1. Barra Comandi Sessione
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.12f, 0.16f, 1.0f));
    ImGui::BeginChild("EditorActionHeader", ImVec2(0, 48.0f), true);

    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "B — EDITOR MATERIALE CUSTOM");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.70f, 0.75f, 0.82f, 1.0f), "[ Working Copy in Memoria ]");

    float btnW = 120.0f;
    float startX = ImGui::GetContentRegionAvail().x - (btnW * 3 + 16.0f);
    if (startX > 300.0f) ImGui::SameLine(startX);

    // Bottone Valida
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.35f, 0.45f, 1.0f));
    if (ImGui::Button("Valida", ImVec2(btnW, 28))) {
        m_validationResult = m_authoringWorkflow->ValidateWorkingCopy();
        m_showValidationFeedback = true;
    }
    ImGui::PopStyleColor();

    // Bottone Applica (Commit nel Registry)
    ImGui::SameLine(0, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.25f, 1.0f));
    if (ImGui::Button("Applica Modifiche", ImVec2(btnW, 28))) {
        std::string commitErr;
        if (m_authoringWorkflow->CommitToRegistry(&commitErr, m_customCatalogPath)) {
            m_selectedMaterialId = m_authoringWorkflow->GetWorkingCopy().id;
            m_authoringMode = false;
            InvalidateCatalogCache();
            SetStatusNotification("Modifiche committate con successo nel registro!");
        } else {
            SetStatusNotification("Commit fallito: " + commitErr, true);
        }
    }
    ImGui::PopStyleColor();

    // Bottone Annulla
    ImGui::SameLine(0, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.40f, 0.20f, 0.20f, 1.0f));
    if (ImGui::Button("Annulla", ImVec2(btnW, 28))) {
        if (m_authoringWorkflow->IsDirty()) {
            m_showDiscardConfirmModal = true;
        } else {
            m_authoringWorkflow->DiscardSession();
            m_authoringMode = false;
        }
    }
    ImGui::PopStyleColor();

    ImGui::EndChild();
    ImGui::PopStyleColor();

    // 2. Feedback validazione (se attivato)
    if (m_showValidationFeedback) {
        ImGui::Spacing();
        if (m_validationResult.isValid) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.25f, 0.15f, 1.0f));
            ImGui::BeginChild("ValOkBox", ImVec2(0, 36.0f), true);
            ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.60f, 1.0f), "[VALIDAZIONE CONFORME] Tutti i parametri SI, l'URI canonico e la consistenza dati sono validi.");
            ImGui::EndChild();
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.25f, 0.10f, 0.10f, 1.0f));
            float boxH = 28.0f + static_cast<float>(m_validationResult.errors.size()) * 18.0f;
            ImGui::BeginChild("ValErrBox", ImVec2(0, boxH), true);
            ImGui::TextColored(ImVec4(1.0f, 0.40f, 0.40f, 1.0f), "[VALIDAZIONE NON CONFORME] Rilevati i seguenti errori prima del commit:");
            for (const auto& err : m_validationResult.errors) {
                ImGui::BulletText("%s", err.c_str());
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 3. Sezioni dell'Editor Scientifico
    RenderEditorIdentitySection(wc);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    RenderEditorClassificationSection(wc);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    RenderEditorCompositionSection(wc);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    RenderEditorPhysicalPropertiesTable(wc);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    RenderEditorLifecycleBar(wc);
}

void MaterialAtlasState::RenderEditorIdentitySection(ScientificMaterialDef& wc) {
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.75f, 1.0f), "1. IDENTITÀ DEL MATERIALE SCIENTIFICO");
    ImGui::Spacing();

    char nameBuf[128];
    strncpy(nameBuf, wc.displayName.c_str(), sizeof(nameBuf));
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::InputText("Nome Visualizzato##EdDisplayName", nameBuf, sizeof(nameBuf))) {
        wc.displayName = nameBuf;
        m_authoringWorkflow->MarkDirty();
    }

    char canonBuf[128];
    strncpy(canonBuf, wc.canonicalId.c_str(), sizeof(canonBuf));
    ImGui::SetNextItemWidth(320.0f);
    bool isNew = (m_authoringWorkflow->GetSessionState() == AuthoringSessionState::EditingNew);
    if (!isNew) ImGui::BeginDisabled();
    if (ImGui::InputText("ID Canonico (URI)##EdCanonicalId", canonBuf, sizeof(canonBuf))) {
        wc.canonicalId = canonBuf;
        m_authoringWorkflow->MarkDirty();
    }
    if (!isNew) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(Immutabile dopo la prima registrazione)");
    }

    char formulaBuf[64];
    strncpy(formulaBuf, wc.chemicalFormula.c_str(), sizeof(formulaBuf));
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputText("Formula Chimica##EdFormula", formulaBuf, sizeof(formulaBuf))) {
        wc.chemicalFormula = formulaBuf;
        m_authoringWorkflow->MarkDirty();
    }

    char descBuf[512];
    strncpy(descBuf, wc.description.c_str(), sizeof(descBuf));
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextMultiline("Descrizione / Note##EdDesc", descBuf, sizeof(descBuf), ImVec2(0, 56.0f))) {
        wc.description = descBuf;
        m_authoringWorkflow->MarkDirty();
    }
}

void MaterialAtlasState::RenderEditorClassificationSection(ScientificMaterialDef& wc) {
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.75f, 1.0f), "2. CLASSIFICAZIONE ONTO-SCIENTIFICA");
    ImGui::Spacing();

    ImGui::Columns(2, "EdClassCols", false);

    // Categoria ScientificKind
    const char* kindNames[] = {
        "Unknown", "Element", "Compound", "Mineral", "Rock", "Mixture", "Alloy", "Organic", "Composite"
    };
    int currentKindIdx = static_cast<int>(wc.kind);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("Categoria Ontologica##EdKind", &currentKindIdx, kindNames, IM_ARRAYSIZE(kindNames))) {
        wc.kind = static_cast<ScientificKind>(currentKindIdx);
        m_authoringWorkflow->MarkDirty();
    }

    ImGui::NextColumn();

    // Stato di Aggregazione Standard
    const char* aggNames[] = {
        "Unknown", "Solid", "Liquid", "Gas", "Plasma", "Supercritical", "Multiphase"
    };
    int currentAggIdx = static_cast<int>(wc.aggregationState);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("Stato di Aggregazione Standard##EdAgg", &currentAggIdx, aggNames, IM_ARRAYSIZE(aggNames))) {
        wc.aggregationState = static_cast<AggregationState>(currentAggIdx);
        m_authoringWorkflow->MarkDirty();
    }

    ImGui::Columns(1);

    char classBuf[128];
    strncpy(classBuf, wc.classification.c_str(), sizeof(classBuf));
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::InputText("Classificazione Geologica/Chimica Specifica##EdClassSpec", classBuf, sizeof(classBuf))) {
        wc.classification = classBuf;
        m_authoringWorkflow->MarkDirty();
    }
}

void MaterialAtlasState::RenderEditorCompositionSection(ScientificMaterialDef& wc) {
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.75f, 1.0f), "3. COMPOSIZIONE SCIENTIFICA STRUTTURATA");
    ImGui::Spacing();

    const char* compStates[] = { "Unknown", "Complete", "Partial", "Approximate", "TraceElementsOnly" };
    int compIdx = static_cast<int>(wc.composition.completeness);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo("Completezza Composizione##EdCompState", &compIdx, compStates, IM_ARRAYSIZE(compStates))) {
        wc.composition.completeness = static_cast<CompositionCompleteness>(compIdx);
        m_authoringWorkflow->MarkDirty();
    }

    ImGui::SameLine(0, 16.0f);
    const char* basisNames[] = { "Mass", "Volume", "Mole", "Atom" };
    int basisIdx = static_cast<int>(wc.composition.defaultBasis);
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::Combo("Frazione di Riferimento##EdBasis", &basisIdx, basisNames, IM_ARRAYSIZE(basisNames))) {
        wc.composition.defaultBasis = static_cast<FractionBasis>(basisIdx);
        m_authoringWorkflow->MarkDirty();
    }

    // Tabella componenti
    if (ImGui::BeginTable("EdCompTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Formula / Nome", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableSetupColumn("Canonical ID", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableSetupColumn("Frazione", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Ruolo", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Azione", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();

        for (size_t c = 0; c < wc.composition.components.size(); ++c) {
            auto& comp = wc.composition.components[c];
            ImGui::PushID(static_cast<int>(c));

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char fBuf[64];
            strncpy(fBuf, comp.displayName.c_str(), sizeof(fBuf));
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##CompName", fBuf, sizeof(fBuf))) {
                comp.displayName = fBuf;
                m_authoringWorkflow->MarkDirty();
            }

            ImGui::TableNextColumn();
            char cidBuf[128];
            strncpy(cidBuf, comp.canonicalId.c_str(), sizeof(cidBuf));
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##CompCid", cidBuf, sizeof(cidBuf))) {
                comp.canonicalId = cidBuf;
                m_authoringWorkflow->MarkDirty();
            }

            ImGui::TableNextColumn();
            double frac = comp.quantity.exact;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputDouble("##CompFrac", &frac, 0.01, 0.05, "%.3f")) {
                comp.quantity.exact = frac;
                comp.quantity.kind = QuantityKind::Exact;
                m_authoringWorkflow->MarkDirty();
            }

            ImGui::TableNextColumn();
            char rBuf[64];
            strncpy(rBuf, comp.role.c_str(), sizeof(rBuf));
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##CompRole", rBuf, sizeof(rBuf))) {
                comp.role = rBuf;
                m_authoringWorkflow->MarkDirty();
            }

            ImGui::TableNextColumn();
            if (ImGui::Button("Rimuovi##DelComp")) {
                wc.composition.components.erase(wc.composition.components.begin() + c);
                m_authoringWorkflow->MarkDirty();
                ImGui::PopID();
                break;
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    if (ImGui::Button("+ Aggiungi Componente##AddCompBtn")) {
        ScientificComponent newC;
        newC.displayName = "Nuovo Elemento";
        newC.quantity.kind = QuantityKind::Exact;
        newC.quantity.exact = 0.10;
        wc.composition.components.push_back(std::move(newC));
        m_authoringWorkflow->MarkDirty();
    }
}

void MaterialAtlasState::RenderEditorPhysicalPropertiesTable(ScientificMaterialDef& wc) {
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.75f, 1.0f), "4. PROPRIETÀ FISICHE (12 GRANDEZZE SI STANDARD)");
    ImGui::Spacing();
    ImGui::TextDisabled("Modifica il valore e lo stato delle 12 grandezze fisiche SI. I valori non conformi verranno segnalati.");

    struct PropRow {
        const char* canonicalName;
        const char* label;
        const char* defaultUnit;
        ScientificQuantity& q;
        bool isTemperature;
    };

    PropRow props[12] = {
        { ScientificPropertyNames::Density, "Densità", "kg/m^3", wc.density, false },
        { ScientificPropertyNames::Porosity, "Porosità", "", wc.porosity, false },
        { ScientificPropertyNames::CompressiveStrength, "Resistenza a Compressione", "Pa", wc.compressiveStrength, false },
        { ScientificPropertyNames::TensileStrength, "Resistenza a Trazione", "Pa", wc.tensileStrength, false },
        { ScientificPropertyNames::ElasticModulus, "Modulo di Young", "Pa", wc.elasticModulus, false },
        { ScientificPropertyNames::StaticFriction, "Attrito Statico", "", wc.staticFriction, false },
        { ScientificPropertyNames::DynamicFriction, "Attrito Dinamico", "", wc.dynamicFriction, false },
        { ScientificPropertyNames::Restitution, "Restituzione", "", wc.restitution, false },
        { ScientificPropertyNames::SpecificHeatCapacity, "Capacità Termica Specifica", "J/(kg*K)", wc.specificHeatCapacity, false },
        { ScientificPropertyNames::ThermalConductivity, "Conducibilità Termica", "W/(m*K)", wc.thermalConductivity, false },
        { ScientificPropertyNames::ThermalExpansion, "Espansione Termica", "1/K", wc.thermalExpansion, false },
        { ScientificPropertyNames::MeltingPoint, "Punto di Fusione", "°C", wc.meltingPoint, true }
    };

    if (ImGui::BeginTable("EdPropsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Proprietà [Unità]", ImGuiTableColumnFlags_WidthStretch, 0.28f);
        ImGui::TableSetupColumn("Stato", ImGuiTableColumnFlags_WidthFixed, 115.0f);
        ImGui::TableSetupColumn("Valore SI", ImGuiTableColumnFlags_WidthStretch, 0.22f);
        ImGui::TableSetupColumn("Qualità Dati", ImGuiTableColumnFlags_WidthFixed, 125.0f);
        ImGui::TableSetupColumn("Fonte / Riferimento", ImGuiTableColumnFlags_WidthStretch, 0.25f);
        ImGui::TableHeadersRow();

        for (int p = 0; p < 12; ++p) {
            auto& row = props[p];
            ImGui::PushID(p);

            ImGui::TableNextRow();

            // Colonna 0: Nome Proprietà & Unità
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.label);
            if (row.defaultUnit[0] != '\0') {
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", row.defaultUnit);
            }

            // Colonna 1: Stato Proprietà (Known / Unknown / NotApplicable)
            ImGui::TableNextColumn();
            const char* stateNames[] = { "Known", "Unknown", "NotApplicable" };
            int stIdx = static_cast<int>(row.q.state);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##StateCombo", &stIdx, stateNames, IM_ARRAYSIZE(stateNames))) {
                row.q.state = static_cast<PropertyState>(stIdx);
                m_authoringWorkflow->MarkDirty();
            }

            // Colonna 2: Valore Scalare
            ImGui::TableNextColumn();
            if (row.q.state == PropertyState::Known) {
                if (row.isTemperature) {
                    double valC = MaterialAuthoringWorkflow::CelsiusFromKelvin(row.q.value);
                    ImGui::SetNextItemWidth(100.0f);
                    if (ImGui::InputDouble("##ValInputC", &valC, 0.0, 0.0, "%.2f")) {
                        row.q.value = MaterialAuthoringWorkflow::KelvinFromCelsius(valC);
                        row.q.unit = "K";
                        m_authoringWorkflow->MarkDirty();
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%.2f K)", row.q.value);
                } else {
                    double val = row.q.value;
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputDouble("##ValInput", &val, 0.0, 0.0, "%.4g")) {
                        row.q.value = val;
                        if (row.q.unit.empty()) row.q.unit = row.defaultUnit;
                        m_authoringWorkflow->MarkDirty();
                    }
                }
            } else if (row.q.state == PropertyState::NotApplicable) {
                ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "Not Applicable");
            } else {
                ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.25f, 1.0f), "Unknown");
            }

            // Colonna 3: Qualità del Dato
            ImGui::TableNextColumn();
            if (row.q.state == PropertyState::Known) {
                const char* qNames[] = { "Estimated", "Experimental", "PeerReviewed", "SyntheticStandard" };
                int qIdx = static_cast<int>(row.q.quality);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##QCombo", &qIdx, qNames, IM_ARRAYSIZE(qNames))) {
                    row.q.quality = static_cast<DataQuality>(qIdx);
                    m_authoringWorkflow->MarkDirty();
                }
            } else {
                ImGui::TextDisabled("—");
            }

            // Colonna 4: Fonte
            ImGui::TableNextColumn();
            if (row.q.state == PropertyState::Known) {
                char srcBuf[128];
                strncpy(srcBuf, row.q.source.c_str(), sizeof(srcBuf));
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputText("##SrcInput", srcBuf, sizeof(srcBuf))) {
                    row.q.source = srcBuf;
                    m_authoringWorkflow->MarkDirty();
                }
            } else {
                ImGui::TextDisabled("—");
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

void MaterialAtlasState::RenderEditorLifecycleBar(ScientificMaterialDef& wc) {
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.75f, 1.0f), "5. STATO DI DISPONIBILITÀ & CICLO DI VITA");
    ImGui::Spacing();

    ImGui::Text("Stato Corrente: ");
    ImGui::SameLine();
    if (wc.lifecycleState == MaterialLifecycleState::Published) {
        RenderBadge("PUBLISHED", ImVec4(0.12f, 0.40f, 0.22f, 1.0f), ImVec4(0.45f, 0.95f, 0.60f, 1.0f));
    } else if (wc.lifecycleState == MaterialLifecycleState::Draft) {
        RenderBadge("DRAFT", ImVec4(0.45f, 0.40f, 0.10f, 1.0f), ImVec4(0.95f, 0.90f, 0.35f, 1.0f));
    } else {
        RenderBadge("DEPRECATED", ImVec4(0.45f, 0.15f, 0.15f, 1.0f), ImVec4(0.95f, 0.50f, 0.50f, 1.0f));
    }

    ImGui::SameLine(0, 16.0f);
    if (ImGui::Checkbox("Abilitato per l'uso in Block Maker##EdBmEnable", &wc.enabledInBlockMaker)) {
        m_authoringWorkflow->MarkDirty();
    }
}

// =============================================================================
// SUB-RENDERERS: MODAL DIALOGS (P0.3C2)
// =============================================================================

void MaterialAtlasState::RenderModals() {
    // 1. Modale Nuovo Materiale Custom
    if (m_showNewMaterialModal) {
        ImGui::OpenPopup("Nuovo Materiale Custom##NewMatModal");
        m_showNewMaterialModal = false;
    }

    if (ImGui::BeginPopupModal("Nuovo Materiale Custom##NewMatModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Inserisci i parametri iniziali per la nuova definizione scientifica:");
        ImGui::Spacing();

        ImGui::InputText("Nome Visualizzato##NewName", m_newDisplayName, sizeof(m_newDisplayName));
        ImGui::InputText("URI Canonico##NewCanon", m_newCanonicalId, sizeof(m_newCanonicalId));

        const char* kindNames[] = {
            "Unknown", "Element", "Compound", "Mineral", "Rock", "Mixture", "Alloy", "Organic", "Composite"
        };
        ImGui::Combo("Categoria Ontologica##NewKind", &m_newKindIndex, kindNames, IM_ARRAYSIZE(kindNames));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Crea Draft", ImVec2(120, 28))) {
            if (m_authoringWorkflow) {
                std::string err;
                auto kind = static_cast<ScientificKind>(m_newKindIndex);
                if (m_authoringWorkflow->BeginCreateCustom(m_newCanonicalId, m_newDisplayName, kind, &err)) {
                    m_authoringMode = true;
                    m_showValidationFeedback = false;
                    ImGui::CloseCurrentPopup();
                } else {
                    SetStatusNotification("Creazione fallita: " + err, true);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Annulla", ImVec2(90, 28))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // 2. Modale Clona come Custom
    if (m_showCloneModal) {
        ImGui::OpenPopup("Clona come Custom##CloneMatModal");
        m_showCloneModal = false;
    }

    if (ImGui::BeginPopupModal("Clona come Custom##CloneMatModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Crea una nuova variante custom a partire dal materiale selezionato:");
        ImGui::Spacing();

        ImGui::InputText("Nuovo Nome##CloneName", m_cloneDisplayName, sizeof(m_cloneDisplayName));
        ImGui::InputText("Nuovo URI Canonico##CloneCanon", m_cloneCanonicalId, sizeof(m_cloneCanonicalId));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Crea Clone", ImVec2(120, 28))) {
            if (m_authoringWorkflow) {
                std::string err;
                if (m_authoringWorkflow->BeginCloneAsCustom(m_selectedMaterialId, m_cloneCanonicalId, m_cloneDisplayName, &err)) {
                    m_authoringMode = true;
                    m_showValidationFeedback = false;
                    ImGui::CloseCurrentPopup();
                } else {
                    SetStatusNotification("Clonazione fallita: " + err, true);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Annulla", ImVec2(90, 28))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // 3. Modale Gestione Conflitto Lost Update (Optimistic Concurrency)
    if (m_showLostUpdateModal) {
        ImGui::OpenPopup("Conflitto Concorrenza (Lost Update)##LostUpModal");
        m_showLostUpdateModal = false;
    }

    if (ImGui::BeginPopupModal("Conflitto Concorrenza (Lost Update)##LostUpModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
        ImGui::Text("[ATTENZIONE: CONFLITTO DI MODIFICA ESTERNA]");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        ImGui::TextWrapped("Il file catalogo su disco e' stato modificato da un altro processo o istanza dopo l'ultimo caricamento.");
        ImGui::TextWrapped("Salvare senza ricaricare cancellerebbe le modifiche altrui.");
        ImGui::Spacing();
        ImGui::TextDisabled("Dettaglio diagnostico: %s", m_lostUpdateError.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Ricarica da Disco (Annulla locali)", ImVec2(240, 32))) {
            if (m_authoringWorkflow) {
                std::string err;
                if (m_authoringWorkflow->ReloadCatalogFromDisk(m_customCatalogPath, &err)) {
                    InvalidateCatalogCache();
                    SetStatusNotification("Catalogo ricaricato con successo da disco!");
                    ImGui::CloseCurrentPopup();
                } else {
                    SetStatusNotification("Ricaricamento fallito: " + err, true);
                }
            }
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.20f, 0.15f, 1.0f));
        if (ImGui::Button("Forza Sovrascrittura (Perdi esterne)", ImVec2(240, 32))) {
            if (m_authoringWorkflow) {
                std::string err;
                if (m_authoringWorkflow->SaveCatalogToDisk(m_customCatalogPath, &err, true)) {
                    SetStatusNotification("Catalogo sovrascritto forzatamente!");
                    ImGui::CloseCurrentPopup();
                } else {
                    SetStatusNotification("Sovrascrittura forzata fallita: " + err, true);
                }
            }
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (ImGui::Button("Annulla", ImVec2(90, 32))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // 4. Modale Conferma Deprecazione
    if (m_showDeprecateConfirmModal) {
        ImGui::OpenPopup("Conferma Deprecazione Materiale##DeprModal");
        m_showDeprecateConfirmModal = false;
    }

    if (ImGui::BeginPopupModal("Conferma Deprecazione Materiale##DeprModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Sei sicuro di voler deprecare formalmente il materiale ID %u?", m_materialToDeprecate);
        ImGui::Spacing();
        ImGui::TextDisabled("Il materiale verra' ritirato dal catalogo attivo e disabilitato in Block Maker,");
        ImGui::TextDisabled("ma sara' preservato intatto su disco per garantire la compatibilita' degli asset storici.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.0f));
        if (ImGui::Button("Conferma Deprecazione", ImVec2(180, 28))) {
            if (m_authoringWorkflow) {
                std::string err;
                if (m_authoringWorkflow->DeprecateMaterialDirect(m_materialToDeprecate, &err)) {
                    InvalidateCatalogCache();
                    SetStatusNotification("Materiale deprecato con successo.");
                    ImGui::CloseCurrentPopup();
                } else {
                    SetStatusNotification("Deprecazione fallita: " + err, true);
                }
            }
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (ImGui::Button("Annulla", ImVec2(90, 28))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // 5. Modale Conferma Scarto Modifiche
    if (m_showDiscardConfirmModal) {
        ImGui::OpenPopup("Scarta Modifiche##DiscardModal");
        m_showDiscardConfirmModal = false;
    }

    if (ImGui::BeginPopupModal("Scarta Modifiche##DiscardModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Ci sono modifiche non committate nella working copy.");
        ImGui::Text("Sei sicuro di voler scartare tutte le modifiche non applicate?");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.0f));
        if (ImGui::Button("Scarta Modifiche", ImVec2(150, 28))) {
            if (m_authoringWorkflow) {
                m_authoringWorkflow->DiscardSession();
            }
            m_authoringMode = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (ImGui::Button("Continua a Modificare", ImVec2(160, 28))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// =============================================================================
// SUB-RENDERERS: INSPECTOR SECTIONS (PRESERVATE DAL CONTRATTO P0.3C0)
// =============================================================================

void MaterialAtlasState::RenderScientificIdentity(const ScientificMaterialDef& def) {
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.75f, 1.0f), "B — SCHEDA SCIENTIFICA (INSPECTOR)");
    ImGui::Spacing();

    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", def.displayName.c_str());
    ImGui::SetWindowFontScale(1.0f);

    if (!def.chemicalFormula.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.95f, 1.0f), "[Formula: %s]", def.chemicalFormula.c_str());
    }

    ImGui::Spacing();

    char idTag[32];
    snprintf(idTag, sizeof(idTag), "ID #%u", def.id);
    RenderBadge(idTag, ImVec4(0.18f, 0.22f, 0.28f, 1.0f), ImVec4(0.70f, 0.75f, 0.82f, 1.0f));

    ImGui::SameLine(0, 6.0f);
    if (def.isCustom || def.id >= CUSTOM_SCIENTIFIC_MATERIAL_ID_BASE) {
        RenderBadge("CUSTOM (UTENTE)", ImVec4(0.55f, 0.35f, 0.05f, 1.0f), ImVec4(1.0f, 0.85f, 0.40f, 1.0f));
    } else {
        RenderBadge("BUILT-IN (STANDARD)", ImVec4(0.12f, 0.35f, 0.45f, 1.0f), ImVec4(0.40f, 0.85f, 0.95f, 1.0f));
    }

    ImGui::SameLine(0, 6.0f);
    if (def.lifecycleState == MaterialLifecycleState::Published) {
        RenderBadge("PUBLISHED", ImVec4(0.12f, 0.40f, 0.22f, 1.0f), ImVec4(0.45f, 0.95f, 0.60f, 1.0f));
    } else if (def.lifecycleState == MaterialLifecycleState::Draft) {
        RenderBadge("DRAFT", ImVec4(0.45f, 0.40f, 0.10f, 1.0f), ImVec4(0.95f, 0.90f, 0.35f, 1.0f));
    } else {
        RenderBadge("DEPRECATED", ImVec4(0.45f, 0.15f, 0.15f, 1.0f), ImVec4(0.95f, 0.50f, 0.50f, 1.0f));
    }

    ImGui::SameLine(0, 6.0f);
    RenderBadge(ScientificKindToString(def.kind), ImVec4(0.20f, 0.22f, 0.28f, 1.0f), ImVec4(0.75f, 0.80f, 0.88f, 1.0f));

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.72f, 1.0f), "URI Canonico:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "%s", def.canonicalId.c_str());

    if (!def.description.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", def.description.c_str());
    }
}

void MaterialAtlasState::RenderScientificClassification(const ScientificMaterialDef& def) {
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.75f, 1.0f), "CLASSIFICAZIONE SCIENTIFICA");
    ImGui::Spacing();

    if (ImGui::BeginTable("ClassificationTable", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Attributo Ontologico", ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableSetupColumn("Valore Scientifico", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Categoria Fondamentale");
        ImGui::TableNextColumn();
        ImGui::Text("%s", ScientificKindToString(def.kind));

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Stato di Aggregazione (Standard)");
        ImGui::TableNextColumn();
        ImGui::Text("%s", AggregationStateToString(def.aggregationState));

        if (!def.classification.empty()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Classificazione Tassonomica");
            ImGui::TableNextColumn();
            ImGui::Text("%s", def.classification.c_str());
        }

        ImGui::EndTable();
    }
}

void MaterialAtlasState::RenderScientificComposition(const ScientificMaterialDef& def) {
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.75f, 1.0f), "COMPOSIZIONE SCIENTIFICA");
    ImGui::Spacing();

    if (def.composition.IsEmpty()) {
        ImGui::TextColored(ImVec4(0.60f, 0.65f, 0.72f, 1.0f),
                           "Composizione chimica elementare o di miscela non definita o sostanza pura elementare.");
        return;
    }

    if (ImGui::BeginTable("CompositionTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Componente / Sostanza", ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("Formula Chimica", ImGuiTableColumnFlags_WidthStretch, 0.25f);
        ImGui::TableSetupColumn("Frazione", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Ruolo", ImGuiTableColumnFlags_WidthStretch, 0.25f);
        ImGui::TableHeadersRow();

        for (const auto& comp : def.composition.components) {
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(comp.displayName.empty() ? comp.canonicalId.c_str() : comp.displayName.c_str());

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(comp.formula.empty() ? "—" : comp.formula.c_str());

            ImGui::TableNextColumn();
            if (comp.quantity.kind == QuantityKind::Exact) {
                ImGui::Text("%.2f%%", comp.quantity.exact * 100.0);
            } else if (comp.quantity.kind == QuantityKind::Range) {
                ImGui::Text("%.1f - %.1f%%", comp.quantity.min * 100.0, comp.quantity.max * 100.0);
            } else {
                ImGui::TextUnformatted("Presente");
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(comp.role.empty() ? "—" : comp.role.c_str());
        }

        ImGui::EndTable();
    }
}

void MaterialAtlasState::RenderPhysicalPropertiesTable(const ScientificMaterialDef& def) {
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.75f, 1.0f), "PROPRIETÀ FISICHE (12 GRANDEZZE SI STANDARD)");
    ImGui::Spacing();

    struct PropRow {
        const char* name;
        const char* label;
        const ScientificQuantity& q;
        const char* defaultUnit;
        bool isPressure;
        bool isTemperature;
    };

    PropRow props[12] = {
        { ScientificPropertyNames::Density, "Densità", def.density, "kg/m^3", false, false },
        { ScientificPropertyNames::Porosity, "Porosità", def.porosity, "", false, false },
        { ScientificPropertyNames::CompressiveStrength, "Resistenza a Compressione", def.compressiveStrength, "Pa", true, false },
        { ScientificPropertyNames::TensileStrength, "Resistenza a Trazione", def.tensileStrength, "Pa", true, false },
        { ScientificPropertyNames::ElasticModulus, "Modulo di Young (Elasticità)", def.elasticModulus, "Pa", true, false },
        { ScientificPropertyNames::StaticFriction, "Attrito Statico", def.staticFriction, "", false, false },
        { ScientificPropertyNames::DynamicFriction, "Attrito Dinamico", def.dynamicFriction, "", false, false },
        { ScientificPropertyNames::Restitution, "Restituzione", def.restitution, "", false, false },
        { ScientificPropertyNames::SpecificHeatCapacity, "Capacità Termica Specifica", def.specificHeatCapacity, "J/(kg*K)", false, false },
        { ScientificPropertyNames::ThermalConductivity, "Conducibilità Termica", def.thermalConductivity, "W/(m*K)", false, false },
        { ScientificPropertyNames::ThermalExpansion, "Espansione Termica Lineare", def.thermalExpansion, "1/K", false, false },
        { ScientificPropertyNames::MeltingPoint, "Punto di Fusione", def.meltingPoint, "°C", false, true }
    };

    if (ImGui::BeginTable("PhysicalPropsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Grandezza Fisica", ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("Valore SI Formattato", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableSetupColumn("Stato / Qualità", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Fonte / Riferimento", ImGuiTableColumnFlags_WidthStretch, 0.25f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < 12; ++i) {
            const auto& row = props[i];
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.label);
            if (row.defaultUnit[0] != '\0') {
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", row.defaultUnit);
            }

            ImGui::TableNextColumn();
            if (row.q.state == PropertyState::Known) {
                if (row.isPressure) {
                    ImGui::Text("%s", FormatPressureHumanReadable(row.q.value).c_str());
                } else if (row.isTemperature) {
                    ImGui::Text("%s", FormatTemperatureHumanReadable(row.q.value).c_str());
                } else {
                    std::string uStr = row.q.unit.empty() ? row.defaultUnit : row.q.unit;
                    std::string uSuffix = uStr.empty() ? "" : (" " + uStr);
                    ImGui::Text("%.4g%s", row.q.value, uSuffix.c_str());
                }
            } else if (row.q.state == PropertyState::NotApplicable) {
                ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "Non Applicabile");
            } else {
                ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.25f, 1.0f), "Unknown");
            }

            ImGui::TableNextColumn();
            if (row.q.state == PropertyState::Known) {
                RenderBadge(DataQualityToString(row.q.quality), ImVec4(0.18f, 0.25f, 0.35f, 1.0f), ImVec4(0.70f, 0.85f, 1.0f, 1.0f));
            } else if (row.q.state == PropertyState::NotApplicable) {
                RenderBadge("N/A", ImVec4(0.20f, 0.20f, 0.22f, 1.0f), ImVec4(0.60f, 0.60f, 0.65f, 1.0f));
            } else {
                RenderBadge("NON DEFINITO", ImVec4(0.35f, 0.25f, 0.10f, 1.0f), ImVec4(0.95f, 0.80f, 0.35f, 1.0f));
            }

            ImGui::TableNextColumn();
            if (row.q.state == PropertyState::Known && !row.q.source.empty()) {
                ImGui::TextDisabled("%s", row.q.source.c_str());
            } else {
                ImGui::TextDisabled("—");
            }
        }

        ImGui::EndTable();
    }
}

void MaterialAtlasState::RenderExtendedPropertiesSection(const ScientificMaterialDef& def) {
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.75f, 1.0f), "PROPRIETÀ SCIENTIFICHE ESTESE (STORAGE FLAT)");
    ImGui::Spacing();

    const auto& entries = def.extendedProperties.GetEntries();
    if (ImGui::BeginTable("ExtPropsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Proprietà Estesa / URI", ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("Valore Formattato", ImGuiTableColumnFlags_WidthStretch, 0.25f);
        ImGui::TableSetupColumn("Stato Epistemico / Applicabilità", ImGuiTableColumnFlags_WidthStretch, 0.20f);
        ImGui::TableSetupColumn("Livello / Provenienza", ImGuiTableColumnFlags_WidthStretch, 0.20f);
        ImGui::TableHeadersRow();

        for (const auto& entry : entries) {
            ImGui::TableNextRow();

            const auto& obs = entry.observationCollection;
            const ScientificPropertyDefinition* propDef = nullptr;
            if (m_context && m_context->scientificMaterialRegistry) {
                propDef = m_context->scientificMaterialRegistry->GetPropertyDefinition(entry.propertyUri);
            }

            // Colonna 0: Proprietà Estesa / URI
            ImGui::TableNextColumn();
            if (propDef && !propDef->displayName.empty()) {
                ImGui::TextUnformatted(propDef->displayName.c_str());
                ImGui::TextDisabled("%s", entry.propertyUri.c_str());
            } else {
                ImGui::TextUnformatted(entry.propertyUri.c_str());
            }

            // Colonna 1: Valore Formattato (rispetta scrupolosamente Applicability ed EpistemicState)
            ImGui::TableNextColumn();
            if (obs.applicability == ApplicabilityState::NotApplicable) {
                ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "Non Applicabile");
            } else if (obs.epistemicState == EpistemicState::Unknown) {
                ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.25f, 1.0f), "Unknown");
            } else if (obs.measurements.empty()) {
                if (obs.applicability == ApplicabilityState::Conditional) {
                    ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.50f, 1.0f), "Condizionale (nessuna misura)");
                } else {
                    ImGui::TextDisabled("—");
                }
            } else {
                // Formattazione della prima misura (o indicatore di misure multiple)
                const auto& m = obs.measurements[0];
                std::string unitStr = !m.unit.empty() ? m.unit : (propDef ? propDef->canonicalUnit : "");
                std::string unitSuffix = unitStr.empty() ? "" : (" " + unitStr);

                if (m.valueNature == ValueNature::ExactScalar || m.valueNature == ValueNature::NominalScalar || m.valueNature == ValueNature::Calculated) {
                    if (m.scalarValue.has_value()) {
                        ImGui::Text("%.4g%s", *m.scalarValue, unitSuffix.c_str());
                    } else {
                        ImGui::TextDisabled("—");
                    }
                } else if (m.valueNature == ValueNature::Range) {
                    if (m.rangeValue.has_value()) {
                        ImGui::Text("%.3g - %.3g%s", m.rangeValue->first, m.rangeValue->second, unitSuffix.c_str());
                    } else {
                        ImGui::TextDisabled("—");
                    }
                } else if (m.valueNature == ValueNature::QualitativeDescriptor) {
                    ImGui::TextUnformatted(m.qualitativeDescriptor.c_str());
                } else {
                    ImGui::TextDisabled("—");
                }

                if (obs.measurements.size() > 1) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(+%u)", static_cast<unsigned int>(obs.measurements.size() - 1));
                }
            }

            // Colonna 2: Stato Epistemico / Applicabilità
            ImGui::TableNextColumn();
            ImGui::Text("%s", EpistemicStateToString(obs.epistemicState));
            if (obs.applicability != ApplicabilityState::Applicable) {
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", ApplicabilityStateToString(obs.applicability));
            }

            // Colonna 3: Livello Fisico / Provenienza Citazionale
            ImGui::TableNextColumn();
            std::string levelStr = propDef ? PhysicalLevelToString(propDef->level) : "—";
            std::string citationStr = "";
            if (!obs.measurements.empty()) {
                const auto& m = obs.measurements[0];
                if (!m.bibliographicCitation.empty()) {
                    citationStr = m.bibliographicCitation;
                } else if (!m.doiOrIsbn.empty()) {
                    citationStr = m.doiOrIsbn;
                } else {
                    citationStr = EvidenceLevelToString(m.evidenceLevel);
                }
            }
            if (!citationStr.empty()) {
                ImGui::Text("%s | %s", levelStr.c_str(), citationStr.c_str());
            } else {
                ImGui::Text("%s", levelStr.c_str());
            }
        }

        ImGui::EndTable();
    }
}

void MaterialAtlasState::RenderLifecycleAndProvenance(const ScientificMaterialDef& def) {
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.75f, 1.0f), "PROVENIENZA & CICLO DI VITA");
    ImGui::Spacing();

    if (ImGui::BeginTable("LifecycleTable", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Proprietà", ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableSetupColumn("Dettaglio", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Stato del Ciclo di Vita");
        ImGui::TableNextColumn();
        if (def.lifecycleState == MaterialLifecycleState::Published) {
            RenderBadge("PUBLISHED", ImVec4(0.12f, 0.40f, 0.22f, 1.0f), ImVec4(0.45f, 0.95f, 0.60f, 1.0f));
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.70f, 0.75f, 0.82f, 1.0f), "Validato formalmente e disponibile per la produzione");
        } else if (def.lifecycleState == MaterialLifecycleState::Draft) {
            RenderBadge("DRAFT", ImVec4(0.45f, 0.40f, 0.10f, 1.0f), ImVec4(0.95f, 0.90f, 0.35f, 1.0f));
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.70f, 0.75f, 0.82f, 1.0f), "In lavorazione (bozza editabile)");
        } else {
            RenderBadge("DEPRECATED", ImVec4(0.45f, 0.15f, 0.15f, 1.0f), ImVec4(0.95f, 0.50f, 0.50f, 1.0f));
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.70f, 0.75f, 0.82f, 1.0f), "Ritirato formalmente, conservato per compatibilità storica");
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Disponibilità Block Maker");
        ImGui::TableNextColumn();
        if (def.enabledInBlockMaker) {
            RenderBadge("ABILITATO", ImVec4(0.15f, 0.35f, 0.50f, 1.0f), ImVec4(0.40f, 0.85f, 1.0f, 1.0f));
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.70f, 0.75f, 0.82f, 1.0f), "Selezionabile come materiale scientifico in Block Maker");
        } else {
            RenderBadge("NON ABILITATO", ImVec4(0.30f, 0.30f, 0.35f, 1.0f), ImVec4(0.70f, 0.70f, 0.75f, 1.0f));
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.70f, 0.75f, 0.82f, 1.0f), "Escluso dalla selezione attiva di Block Maker");
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Origine del Materiale");
        ImGui::TableNextColumn();
        if (def.isCustom || def.id >= CUSTOM_SCIENTIFIC_MATERIAL_ID_BASE) {
            RenderBadge("CUSTOM (UTENTE)", ImVec4(0.55f, 0.35f, 0.05f, 1.0f), ImVec4(1.0f, 0.85f, 0.40f, 1.0f));
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.70f, 0.75f, 0.82f, 1.0f), "Materiale scientifico personalizzato salvato in catalogo");
        } else {
            RenderBadge("BUILT-IN (STANDARD)", ImVec4(0.12f, 0.35f, 0.45f, 1.0f), ImVec4(0.40f, 0.85f, 0.95f, 1.0f));
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.70f, 0.75f, 0.82f, 1.0f), "Materiale standard nativo integrato nel motore");
        }

        ImGui::EndTable();
    }
}

void MaterialAtlasState::RenderAppearanceSection(const ScientificMaterialDef& def) {
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.75f, 1.0f), "C — PROPRIETÀ GRAFICHE / PBR (APPEARANCE)");
    ImGui::Spacing();

    // Disclaimer disaccoppiamento visivo
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.40f, 0.50f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.13f, 0.18f, 1.0f));
    ImGui::BeginChild("PbrDisclaimerBox", ImVec2(0, 52.0f), true);
    ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.95f, 1.0f), "[ DISACCOPPIAMENTO VISIVO ]");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.75f, 0.80f, 0.88f, 1.0f),
                       "L'aspetto grafico PBR e' disaccoppiato dall'identita' scientifica del materiale.");
    ImGui::TextColored(ImVec4(0.60f, 0.65f, 0.72f, 1.0f),
                       "Un materiale scientifico puo' essere referenziato da zero o piu' definizioni visive MaterialAppearance.");
    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    ImGui::Spacing();

    if (!m_context || !m_context->appearanceRegistry) {
        ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.70f, 1.0f), "MaterialAppearanceRegistry non disponibile.");
        return;
    }

    const auto& appearances = m_context->appearanceRegistry->GetAllAppearances();
    std::vector<const MaterialAppearance*> matchingApps;

    for (const auto& [appId, app] : appearances) {
        if (app.suggestedScientificMaterialId == def.id || appId == def.id) {
            matchingApps.push_back(&app);
        }
    }

    if (matchingApps.empty()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f, 0.13f, 0.16f, 1.0f));
        ImGui::BeginChild("NoAppearanceBox", ImVec2(0, 60.0f), true);
        ImGui::TextColored(ImVec4(0.80f, 0.75f, 0.50f, 1.0f),
                           "Nessuna Appearance PBR esplicitamente associata a questo materiale scientifico.");
        ImGui::TextColored(ImVec4(0.60f, 0.65f, 0.72f, 1.0f),
                           "Questo materiale puo' essere referenziato da una nuova definizione visiva in Block Maker.");
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    for (const auto* app : matchingApps) {
        ImGui::PushID(static_cast<int>(app->id));

        ImGui::TextColored(ImVec4(0.95f, 0.95f, 1.0f, 1.0f), "Appearance: %s", app->displayName.c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1.0f), "[ID: %u | Canonical: %s]", app->id, app->canonicalId.c_str());

        if (ImGui::BeginTable("AppearanceParamsTable", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Parametro PBR", ImGuiTableColumnFlags_WidthFixed, 180.0f);
            ImGui::TableSetupColumn("Valore / Campione", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Albedo / Base Color");
            ImGui::TableNextColumn();
            ImVec4 col(app->baseColorFallback.r, app->baseColorFallback.g, app->baseColorFallback.b, app->alphaFallback);
            ImGui::ColorButton("##AlbedoSwatch", col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoPicker, ImVec2(24, 18));
            ImGui::SameLine();
            ImGui::Text("RGB (%.2f, %.2f, %.2f) | Alpha: %.2f",
                        app->baseColorFallback.r, app->baseColorFallback.g, app->baseColorFallback.b, app->alphaFallback);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Roughness (Rugosità)");
            ImGui::TableNextColumn();
            ImGui::ProgressBar(app->roughnessFallback, ImVec2(180.0f, 16.0f), FormatDouble(app->roughnessFallback, 2).c_str());

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Metallic (Metallicità)");
            ImGui::TableNextColumn();
            ImGui::ProgressBar(app->metallicFallback, ImVec2(180.0f, 16.0f), FormatDouble(app->metallicFallback, 2).c_str());

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Emissione PBR");
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", app->emissiveStrength);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Mappa Albedo");
            ImGui::TableNextColumn();
            ImGui::Text("%s", app->albedoPath.empty() ? "(Nessuna / Tinta costante)" : app->albedoPath.c_str());

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Mappa Normali");
            ImGui::TableNextColumn();
            ImGui::Text("%s", app->normalPath.empty() ? "(Nessuna / Flat)" : app->normalPath.c_str());

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Mappa ORM");
            ImGui::TableNextColumn();
            ImGui::Text("%s", app->ormPath.empty() ? "(Nessuna)" : app->ormPath.c_str());

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Mappa Emissiva");
            ImGui::TableNextColumn();
            ImGui::Text("%s", app->emissivePath.empty() ? "(Nessuna)" : app->emissivePath.c_str());

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::PopID();
    }
}

} // namespace fw
