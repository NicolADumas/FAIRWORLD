#include "pch.h"
#include "World.h"
#include "AIAssistant.h"
#include "PerlinNoise.h"
#include "BlockMaterial.h"

// Offset per le 6 facce di un cubo (in ordine: +Y, -Y, -X, +X, -Z, +Z)
static const glm::vec3 FACE_VERTS[6][4] = {
    {{0,1,0},{1,1,0},{1,1,1},{0,1,1}}, // Top
    {{0,0,1},{1,0,1},{1,0,0},{0,0,0}}, // Bottom
    {{0,0,1},{0,1,1},{0,1,0},{0,0,0}}, // Left
    {{1,0,0},{1,1,0},{1,1,1},{1,0,1}}, // Right
    {{0,0,0},{0,1,0},{1,1,0},{1,0,0}}, // Front
    {{1,0,1},{1,1,1},{0,1,1},{0,0,1}}, // Back
};

// Direzione del vicino per ogni faccia
static const glm::ivec3 FACE_DIRS[6] = {
    { 0, 1, 0}, { 0,-1, 0}, {-1, 0, 0},
    { 1, 0, 0}, { 0, 0,-1}, { 0, 0, 1},
};

void Chunk::UpdateHeatCapacity() {
    double totalCapacity = 0.0;
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            for (int y = 0; y < CHUNK_HEIGHT; y++) {
                BlockType b = (BlockType)blocks[x][y][z];
                if (b != BlockType::Air) {
                    const auto& mat = GetBlockMaterial(b);
                    // C_tot = sum(m * c)
                    totalCapacity += (mat.mass * mat.heatCapacitySp);
                }
            }
        }
    }
    // Evita divisioni per zero se il chunk e completamente vuoto
    if (totalCapacity < 1000.0) totalCapacity = 1000.0;
    
    // Per evitare ere glaciali istantanee quando calcoliamo la nuova 
    // gigantesca capacita termica del chunk, preserviamo la temperatura 
    // attuale scalando l'energia interna, anziche il contrario!
    heatCapacity = totalCapacity;
    internalEnergy = temperature * heatCapacity;
}

World::World() {
    // 1. Inizializza i pianeti del Sistema Solare
    // Terra
    PlanetDef earth;
    earth.type = PlanetType::EarthPrime;
    earth.gravity = 9.81f;
    earth.baseTemp = 293.15f;
    earth.skyDay = glm::vec3(0.4f, 0.6f, 0.9f);
    earth.skyNight = glm::vec3(0.02f, 0.05f, 0.1f);
    earth.a = 1000.0; // Raggio dell'orbita visivo (scala fittizia per lo skybox)
    earth.e = 0.0167;
    earth.T = 360.0; // 360 secondi per un'orbita completa
    earth.M0 = 0.0;
    m_solarSystem.push_back(earth);

    // Marte
    PlanetDef mars;
    mars.type = PlanetType::MarsDesolation;
    mars.gravity = 3.72f;
    mars.baseTemp = 213.15f;
    mars.skyDay = glm::vec3(0.8f, 0.4f, 0.2f);
    mars.skyNight = glm::vec3(0.1f, 0.02f, 0.02f);
    mars.a = 1524.0;
    mars.e = 0.0934;
    mars.T = 360.0 * 1.88; // T^2 proporzionale a a^3
    mars.M0 = 1.0;
    m_solarSystem.push_back(mars);

    // Glacies (Simile a Giove/Europa)
    PlanetDef glacies;
    glacies.type = PlanetType::Glacies;
    glacies.gravity = 9.0f;
    glacies.baseTemp = 173.15f;
    glacies.skyDay = glm::vec3(0.05f, 0.05f, 0.15f);
    glacies.skyNight = glm::vec3(0.01f, 0.01f, 0.05f);
    glacies.a = 5200.0;
    glacies.e = 0.048;
    glacies.T = 360.0 * 11.86;
    glacies.M0 = 2.0;
    m_solarSystem.push_back(glacies);

    ChangePlanet(PlanetType::EarthPrime);
}

const PlanetDef* World::GetCurrentPlanet() const {
    for (const auto& p : m_solarSystem) {
        if (p.type == m_currentPlanetType) return &p;
    }
    return &m_solarSystem[0];
}

void World::ChangePlanet(PlanetType newPlanet) {
    m_currentPlanetType = newPlanet;
    InitWorld();
}

static double m_solarSystemTime = 0.0;

void World::SimulateOrbits(float deltaSeconds) {
    m_solarSystemTime += deltaSeconds;

    for (auto& planet : m_solarSystem) {
        // 1. Calcola l'Anomalia Media M
        double M = planet.M0 + (2.0 * 3.1415926535 / planet.T) * m_solarSystemTime;
        
        // 2. Risoluzione dell'Equazione di Keplero: M = E - e*sin(E) tramite Newton-Raphson
        double E = M; // Stima iniziale
        for (int i = 0; i < 10; ++i) {
            double f = E - planet.e * sin(E) - M;
            double df = 1.0 - planet.e * cos(E);
            E = E - f / df;
        }

        // 3. Calcolo dell'Anomalia Vera (theta)
        // tan(theta/2) = sqrt((1+e)/(1-e)) * tan(E/2)
        double theta = 2.0 * atan(sqrt((1.0 + planet.e) / (1.0 - planet.e)) * tan(E / 2.0));
        
        // 4. Distanza radiale r(theta) = a * (1 - e^2) / (1 + e * cos(theta))
        double r = planet.a * (1.0 - planet.e * planet.e) / (1.0 + planet.e * cos(theta));
        
        // 5. Posizione eliocentrica (nel piano 2D dell'eclittica, Y=0 o Z=0 a seconda del sistema di coordinate)
        // Usiamo X e Z per l'orbita.
        planet.currentPosition = glm::vec3(r * cos(theta), 0.0f, r * sin(theta));
    }
}

void World::InitWorld() {
    m_chunks.clear();
    // Genera 16x16 chunk (da -8 a +7 in X e Z)
    for (int cx = -8; cx < 8; cx++) {
        for (int cz = -8; cz < 8; cz++) {
            auto chunk = std::make_unique<Chunk>(cx, cz);
            GenerateChunk(chunk.get());
            ChunkCoord coord{cx, cz};
            m_chunks[coord] = std::move(chunk);
        }
    }

    // Secondo passaggio: Alberi (Così i blocchi possono sconfinare nei chunk vicini)
    if (m_currentPlanetType == PlanetType::EarthPrime) {
        PerlinNoise pn(12345);
        for (int cx = -8; cx < 8; cx++) {
            for (int cz = -8; cz < 8; cz++) {
                for (int x = 0; x < CHUNK_SIZE; x++) {
                    for (int z = 0; z < CHUNK_SIZE; z++) {
                        double gx = cx * CHUNK_SIZE + x;
                        double gz = cz * CHUNK_SIZE + z;
                        
                        // Solo su erba, densità dipendente dal rumore
                        double treeNoise = pn.noise(gx * 0.5, 0, gz * 0.5);
                        if (treeNoise > 0.85) {
                            // Trova la Y dell'erba
                            for (int y = CHUNK_HEIGHT - 1; y > 0; y--) {
                                if (GetBlock(gx, y, gz) == BlockType::Grass) {
                                    PlaceTree(gx, y + 1, gz);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Terzo passaggio: Stargate (Per test)
    SetBlock(5, 30, 5, BlockType::StargateFrame);
    SetBlock(5, 31, 5, BlockType::StargatePortal);
    SetBlock(5, 32, 5, BlockType::StargateFrame);
}

void World::AdvanceTime(float deltaSeconds) {
    // 1 giorno FAIRWORLD = 3 ore reali (10800 secondi)
    constexpr float SECONDS_PER_DAY = 10800.0f;
    m_timeOfDay += deltaSeconds / SECONDS_PER_DAY;
    if (m_timeOfDay >= 1.0f) {
        m_timeOfDay -= 1.0f;
    }
}

void World::SimulateWaterTick() {
    // Automata cellulare per fluidodinamica a blocchi
    std::vector<std::tuple<int, int, int, BlockType>> updates;
    
    // Per ottimizzare, iteriamo solo sui chunk caricati
    for (const auto& pair : m_chunks) {
        Chunk* chunk = pair.second.get();
        for (int x = 0; x < CHUNK_SIZE; x++) {
            for (int z = 0; z < CHUNK_SIZE; z++) {
                for (int y = 1; y < CHUNK_HEIGHT; y++) {
                    if (chunk->blocks[x][y][z] == (uint8_t)BlockType::Water) {
                        int gx = chunk->cx * CHUNK_SIZE + x;
                        int gz = chunk->cz * CHUNK_SIZE + z;
                        
                        // 1. Prova a scendere
                        if (GetBlock(gx, y - 1, gz) == BlockType::Air) {
                            updates.push_back({gx, y - 1, gz, BlockType::Water});
                        } 
                        // 2. Espandi ai lati se sotto c'è solido (non aria, non acqua)
                        else {
                            BlockType b = GetBlock(gx, y - 1, gz);
                            if (b != BlockType::Air && b != BlockType::Water) {
                                if (GetBlock(gx + 1, y, gz) == BlockType::Air) updates.push_back({gx + 1, y, gz, BlockType::Water});
                                if (GetBlock(gx - 1, y, gz) == BlockType::Air) updates.push_back({gx - 1, y, gz, BlockType::Water});
                                if (GetBlock(gx, y, gz + 1) == BlockType::Air) updates.push_back({gx, y, gz + 1, BlockType::Water});
                                if (GetBlock(gx, y, gz - 1) == BlockType::Air) updates.push_back({gx, y, gz - 1, BlockType::Water});
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Applica gli update e segna i chunk sporchi
    for (const auto& u : updates) {
        SetBlock(std::get<0>(u), std::get<1>(u), std::get<2>(u), std::get<3>(u));
    }
}

void World::SimulateThermodynamicsTick() {
    // 1. Scambio di calore per conduzione e aggiornamento energia interna
    for (auto& pair : m_chunks) {
        Chunk* chunk = pair.second.get();
        if (!chunk) continue;
        
        // Scambia con i vicini per conduzione termica (Legge di Fourier semplificata)
        const int dx[] = {1, -1, 0, 0};
        const int dz[] = {0, 0, 1, -1};
        
        for (int i=0; i<4; i++) {
            ChunkCoord neighborC = {chunk->cx + dx[i], chunk->cz + dz[i]};
            auto it = m_chunks.find(neighborC);
            if (it != m_chunks.end() && it->second) {
                Chunk* neighbor = it->second.get();
                // \Delta Q = k * \Delta T. k è in J/K per tick
                double diff = chunk->temperature - neighbor->temperature;
                if (std::abs(diff) > 0.01) {
                    double heatTransfer = 1000.0 * diff; // 1000 J/K
                    chunk->internalEnergy -= heatTransfer;
                    neighbor->internalEnergy += heatTransfer;
                }
            }
        }
    }
    
    // 2. Transizioni di fase e Sorgenti di calore
    const double LATENT_HEAT = 500000.0; // Joule richiesti per la transizione di un blocco
    std::vector<std::tuple<int, int, int, BlockType>> updates;
    
    for (auto& pair : m_chunks) {
        Chunk* chunk = pair.second.get();
        if (!chunk) continue;
        
        chunk->UpdateTemperature();
        
        bool hasLava = false;
        
        if (chunk->temperature < 273.15) {
            // Raffreddamento: l'acqua solidifica
            for (int x = 0; x < CHUNK_SIZE; x++) {
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    for (int y = 0; y < CHUNK_HEIGHT; y++) {
                        if (chunk->blocks[x][y][z] == (uint8_t)BlockType::Water) {
                            chunk->latentHeatAccumulator -= 15000.0; 
                            if (chunk->latentHeatAccumulator < -LATENT_HEAT) {
                                updates.push_back({chunk->cx * CHUNK_SIZE + x, y, chunk->cz * CHUNK_SIZE + z, BlockType::Ice});
                                chunk->latentHeatAccumulator += LATENT_HEAT;
                            }
                        } else if (chunk->blocks[x][y][z] == (uint8_t)BlockType::Lava) {
                            hasLava = true;
                        }
                    }
                }
            }
        } else {
            // Riscaldamento: il ghiaccio fonde
            for (int x = 0; x < CHUNK_SIZE; x++) {
                for (int z = 0; z < CHUNK_SIZE; z++) {
                    for (int y = 0; y < CHUNK_HEIGHT; y++) {
                        if (chunk->blocks[x][y][z] == (uint8_t)BlockType::Ice) {
                            chunk->latentHeatAccumulator += 15000.0;
                            if (chunk->latentHeatAccumulator > LATENT_HEAT) {
                                updates.push_back({chunk->cx * CHUNK_SIZE + x, y, chunk->cz * CHUNK_SIZE + z, BlockType::Water});
                                chunk->latentHeatAccumulator -= LATENT_HEAT;
                            }
                        } else if (chunk->blocks[x][y][z] == (uint8_t)BlockType::Lava) {
                            hasLava = true;
                        }
                    }
                }
            }
        }
        
        if (hasLava) {
            // La lava è una potente sorgente di energia (magma ad alta temperatura)
            chunk->internalEnergy += 100000.0; // Aggiunge Joule al chunk, portandolo fino al limite
            if (chunk->temperature > 1200.0) {
                // Raggiunto l'equilibrio con la lava
                chunk->internalEnergy = 1200.0 * chunk->heatCapacity;
            }
        } else {
            // Se non c'è lava, tendenza naturale verso la temperatura base del pianeta
            double diff = GetCurrentPlanet()->baseTemp - chunk->temperature;
            chunk->internalEnergy += diff * 100.0; // Riscaldamento/Raffreddamento atmosferico
        }
        
        chunk->UpdateTemperature(); // Ricalcola dopo gli input/output
    }
    
    // Applica transizioni di fase
    for (const auto& u : updates) {
        SetBlock(std::get<0>(u), std::get<1>(u), std::get<2>(u), std::get<3>(u));
    }
}

float World::GetSunIntensity() const {
    // Mezzogiorno (0.5) -> intensità massima 1.0
    // Mezzanotte (0.0 o 1.0) -> intensità 0.1
    // Usa un'onda sinusoidale
    float sine = sin(m_timeOfDay * 2.0f * 3.14159265f - 3.14159265f / 2.0f);
    // sine va da -1 (notte) a +1 (giorno)
    float intensity = (sine + 1.0f) * 0.5f; // Normalizza tra 0 e 1
    // Non far scendere l'intensità a 0 assoluto per vedere qualcosa
    return std::max(0.15f, intensity);
}

glm::vec3 World::GetSkyColor() const {
    float intensity = GetSunIntensity();
    
    // Devo rendere GetCurrentPlanet() cost-correct o castare
    // Per semplicità uso m_currentPlanetType per cercarlo senza const
    glm::vec3 nightColor = glm::vec3(0,0,0);
    glm::vec3 dayColor = glm::vec3(1,1,1);
    for (const auto& p : m_solarSystem) {
        if (p.type == m_currentPlanetType) {
            nightColor = p.skyNight;
            dayColor = p.skyDay;
            break;
        }
    }
    
    // Albe / Tramonti
    if (intensity > 0.3f && intensity < 0.6f) {
        glm::vec3 sunsetColor = glm::vec3(0.8f, 0.4f, 0.2f);
        // Blend aggiuntivo vicino al tramonto
        float sunsetBlend = 1.0f - abs(intensity - 0.45f) / 0.15f;
        sunsetBlend = std::clamp(sunsetBlend, 0.0f, 1.0f);
        glm::vec3 base = glm::mix(nightColor, dayColor, intensity);
        return glm::mix(base, sunsetColor, sunsetBlend * 0.6f);
    }

    return glm::mix(nightColor, dayColor, intensity);
}

void World::GenerateChunk(Chunk* chunk) {
    PerlinNoise pn(12345); // Seed hardcoded per ora
    
    BlockType surfaceBlock = BlockType::Grass;
    BlockType subBlock = BlockType::Dirt;
    BlockType liquidBlock = BlockType::Water;
    
    if (m_currentPlanetType == PlanetType::MarsDesolation) {
        surfaceBlock = BlockType::Sand;
        subBlock = BlockType::Sand;
        liquidBlock = BlockType::Air; // Niente oceani
    } else if (m_currentPlanetType == PlanetType::Glacies) {
        surfaceBlock = BlockType::Ice;
        subBlock = BlockType::Stone;
        liquidBlock = BlockType::Ice; // Oceani ghiacciati
    }
    
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            double gx = chunk->cx * CHUNK_SIZE + x;
            double gz = chunk->cz * CHUNK_SIZE + z;
            
            // Rumore combinato per colline morbide
            double n = pn.noise(gx * 0.03, 0, gz * 0.03);
            double n2 = pn.noise(gx * 0.1, 0, gz * 0.1) * 0.5;
            int height = 20 + (int)((n + n2) * 12);
            
            // Fiumi: CarveRiver inline (meno frequenti su Marte)
            double riverNoise = abs(pn.noise(gx * 0.015, 0, gz * 0.015));
            if (riverNoise < 0.04) {
                height -= (int)((0.04 - riverNoise) * 100);
            }
            
            for (int y = 0; y < CHUNK_HEIGHT; y++) {
                if (y < height - 4) {
                    // Caverne 3D (Noise)
                    double caveNoise = pn.noise(gx * 0.05, y * 0.05, gz * 0.05);
                    if (caveNoise > 0.65) {
                        chunk->blocks[x][y][z] = (uint8_t)BlockType::Air;
                        // Sul fondo delle caverne funghi luminescenti o lava
                        if (y < 15 && caveNoise > 0.75) {
                            if (y < 6) chunk->blocks[x][y][z] = (uint8_t)BlockType::Lava;
                            else if (pn.noise(gx, y, gz) > 0.8 && m_currentPlanetType == PlanetType::EarthPrime) 
                                chunk->blocks[x][y][z] = (uint8_t)BlockType::Mushroom;
                        }
                    } else {
                        // Minerali rari nella roccia
                        if (pn.noise(gx * 0.2, y * 0.2, gz * 0.2) > 0.85) {
                            chunk->blocks[x][y][z] = (uint8_t)BlockType::Ore;
                        } else {
                            chunk->blocks[x][y][z] = (uint8_t)BlockType::Stone;
                        }
                    }
                } else if (y < height) {
                    chunk->blocks[x][y][z] = (uint8_t)subBlock;
                } else if (y == height) {
                    if (y < 18) {
                        if (m_currentPlanetType == PlanetType::EarthPrime) chunk->blocks[x][y][z] = (uint8_t)BlockType::Sand;
                        else chunk->blocks[x][y][z] = (uint8_t)surfaceBlock;
                    }
                    else chunk->blocks[x][y][z] = (uint8_t)surfaceBlock;
                } else if (y < 18) {
                    chunk->blocks[x][y][z] = (uint8_t)liquidBlock;
                } else {
                    chunk->blocks[x][y][z] = (uint8_t)BlockType::Air;
                }
            }
        }
    }
    
    // Inizializza la capacita termica del nuovo chunk basandosi sulla generazione
    chunk->UpdateHeatCapacity();
}

void World::PlaceTree(int x, int y, int z) {
    // Tronco (4 blocchi di altezza)
    for (int i = 0; i < 4; i++) {
        SetBlock(x, y + i, z, BlockType::Wood);
    }
    // Chioma 3x3x2
    for (int lx = x - 1; lx <= x + 1; lx++) {
        for (int lz = z - 1; lz <= z + 1; lz++) {
            for (int ly = y + 2; ly <= y + 3; ly++) {
                if (GetBlock(lx, ly, lz) == BlockType::Air) {
                    SetBlock(lx, ly, lz, BlockType::Leaves);
                }
            }
        }
    }
    // Cima della chioma
    SetBlock(x, y + 4, z, BlockType::Leaves);
    SetBlock(x + 1, y + 4, z, BlockType::Leaves);
    SetBlock(x - 1, y + 4, z, BlockType::Leaves);
    SetBlock(x, y + 4, z - 1, BlockType::Leaves);
}

float World::GetTemperatureAt(int x, int z) const {
    int cx = (x >= 0) ? (x / CHUNK_SIZE) : ((x - CHUNK_SIZE + 1) / CHUNK_SIZE);
    int cz = (z >= 0) ? (z / CHUNK_SIZE) : ((z - CHUNK_SIZE + 1) / CHUNK_SIZE);
    ChunkCoord coord{cx, cz};
    
    auto it = m_chunks.find(coord);
    if (it != m_chunks.end() && it->second) {
        return (float)it->second->temperature;
    }
    return 293.15f;
}

Chunk* World::GetChunk(int cx, int cz) {
    ChunkCoord coord{cx, cz};
    auto it = m_chunks.find(coord);
    if (it != m_chunks.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool World::IsInBounds(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_HEIGHT) return false;
    int cx = (int)floor((float)x / CHUNK_SIZE);
    int cz = (int)floor((float)z / CHUNK_SIZE);
    ChunkCoord coord{cx, cz};
    return m_chunks.find(coord) != m_chunks.end();
}

BlockType World::GetBlock(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_HEIGHT) return BlockType::Air;
    int cx = (int)floor((float)x / CHUNK_SIZE);
    int cz = (int)floor((float)z / CHUNK_SIZE);
    ChunkCoord coord{cx, cz};
    
    auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) return BlockType::Air;
    
    int lx = x - cx * CHUNK_SIZE;
    int lz = z - cz * CHUNK_SIZE;
    return (BlockType)it->second->blocks[lx][y][lz];
}

void World::SetBlock(int x, int y, int z, BlockType type) {
    if (y < 0 || y >= CHUNK_HEIGHT) return;
    int cx = (int)floor((float)x / CHUNK_SIZE);
    int cz = (int)floor((float)z / CHUNK_SIZE);
    ChunkCoord coord{cx, cz};
    
    auto it = m_chunks.find(coord);
    if (it != m_chunks.end()) {
        int lx = x - cx * CHUNK_SIZE;
        int lz = z - cz * CHUNK_SIZE;
        if (it->second->blocks[lx][y][lz] != (uint8_t)type) {
            uint8_t oldType = it->second->blocks[lx][y][lz];
            it->second->blocks[lx][y][lz] = (uint8_t)type;
            
            // Aggiornamento Incrementale Termodinamico (O(1) anziche O(N))
            if (oldType != (uint8_t)BlockType::Air) {
                const auto& oldMat = GetBlockMaterial((BlockType)oldType);
                double c = oldMat.mass * oldMat.heatCapacitySp;
                it->second->heatCapacity -= c;
                it->second->internalEnergy -= c * it->second->temperature; // Rimuove energia proporzionale
            }
            if (type != BlockType::Air) {
                const auto& newMat = GetBlockMaterial(type);
                double c = newMat.mass * newMat.heatCapacitySp;
                it->second->heatCapacity += c;
                it->second->internalEnergy += c * it->second->temperature; // Aggiunge energia a T ambiente
            }
            
            if (it->second->heatCapacity < 1000.0) it->second->heatCapacity = 1000.0;
            it->second->UpdateTemperature();

            it->second->isDirty = true;
            
            // Mark neighboring chunks as dirty if placed on chunk border
            if (lx == 0) { auto n = GetChunk(cx - 1, cz); if (n) n->isDirty = true; }
            if (lx == CHUNK_SIZE - 1) { auto n = GetChunk(cx + 1, cz); if (n) n->isDirty = true; }
            if (lz == 0) { auto n = GetChunk(cx, cz - 1); if (n) n->isDirty = true; }
            if (lz == CHUNK_SIZE - 1) { auto n = GetChunk(cx, cz + 1); if (n) n->isDirty = true; }
        }
    }
}

constexpr glm::vec3 World::BlockColor(BlockType t) {
    switch (t) {
        case BlockType::Grass:       return {0.10f, 0.85f, 0.30f};
        case BlockType::Dirt:        return {0.55f, 0.35f, 0.15f};
        case BlockType::Stone:       return {0.50f, 0.50f, 0.50f};
        case BlockType::Wood:        return {0.58f, 0.40f, 0.20f};
        case BlockType::Sand:        return {0.85f, 0.80f, 0.55f};
        case BlockType::Water:       return {0.10f, 0.40f, 0.90f};
        case BlockType::Lava:        return {0.95f, 0.35f, 0.05f};
        case BlockType::Leaves:      return {0.15f, 0.45f, 0.10f};
        case BlockType::MobSpawner:  return {0.45f, 0.10f, 0.70f};
        case BlockType::LightSource: return {1.00f, 0.85f, 0.20f};
        case BlockType::Mushroom:    return {0.10f, 0.90f, 0.80f}; // Ciano bioluminescente
        case BlockType::Ore:         return {0.85f, 0.20f, 0.20f}; // Minerale rosso
        default:                     return {1.0f,  0.0f,  1.0f};
    }
}

void World::AddFace(int x, int y, int z, int face, const glm::vec3& color, float texIndex, std::vector<Vertex>& outVerts, std::vector<uint32_t>& outIndices, bool lowerY) {
    uint32_t baseIndex = (uint32_t)outVerts.size();
    glm::vec3 origin((float)x, (float)y, (float)z);
    const glm::vec2 UVS[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    glm::vec3 normal = glm::vec3((float)FACE_DIRS[face].x, (float)FACE_DIRS[face].y, (float)FACE_DIRS[face].z);

    for (int i = 0; i < 4; i++) {
        glm::vec3 pos = origin + FACE_VERTS[face][i];
        if (lowerY && pos.y == origin.y + 1) {
            pos.y -= 0.15f;
        }
        outVerts.push_back({ pos, color, UVS[i], texIndex, normal });
    }

    outIndices.push_back(baseIndex + 0);
    outIndices.push_back(baseIndex + 1);
    outIndices.push_back(baseIndex + 2);
    outIndices.push_back(baseIndex + 0);
    outIndices.push_back(baseIndex + 2);
    outIndices.push_back(baseIndex + 3);
}

std::vector<ChunkCoord> World::BuildDirtyChunks() {
    std::vector<ChunkCoord> updated;
    
    for (auto& pair : m_chunks) {
        Chunk* chunk = pair.second.get();
        if (!chunk->isDirty) continue;
        
        chunk->vertices.clear();
        chunk->indices.clear();
        
        int cx = chunk->cx;
        int cz = chunk->cz;
        
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int y = 0; y < CHUNK_HEIGHT; y++) {
                for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                    BlockType type = (BlockType)chunk->blocks[lx][y][lz];
                    if (type == BlockType::Air) continue;

                    int gx = cx * CHUNK_SIZE + lx;
                    int gz = cz * CHUNK_SIZE + lz;
                    glm::vec3 color = BlockColor(type);

                    for (int face = 0; face < 6; face++) {
                        glm::ivec3 neighbor = glm::ivec3(gx, y, gz) + FACE_DIRS[face];
                        BlockType neighType = GetBlock(neighbor.x, neighbor.y, neighbor.z);
                        
                        bool drawFace = false;
                        if (neighType == BlockType::Air) drawFace = true;
                        else if (type != BlockType::Water && type != BlockType::Lava && (neighType == BlockType::Water || neighType == BlockType::Lava)) drawFace = true;

                        if (drawFace) {
                            glm::vec3 faceColor = color;
                            if (face >= 2) faceColor *= 0.85f;
                            if (face == 1) faceColor *= 0.70f;
                            
                            bool isLiquid = (type == BlockType::Water || type == BlockType::Lava);
                            bool lowerY = false;
                            if (isLiquid && face == 0) { 
                                BlockType above = GetBlock(gx, y + 1, gz);
                                if (above != BlockType::Water && above != BlockType::Lava) {
                                    lowerY = true;
                                }
                            }

                            AddFace(gx, y, gz, face, faceColor, (float)type, chunk->vertices, chunk->indices, lowerY);
                        }
                    }
                }
            }
        }
        
        chunk->isDirty = false;
        chunk->isMeshEmpty = chunk->indices.empty();
        updated.push_back(pair.first);
    }
    
    return updated;
}

void World::BuildGhostMesh(const std::vector<GhostBlock>& ghosts) {
    m_ghostVertices.clear();
    m_ghostIndices.clear();
    glm::vec3 ghostColor(0.2f, 0.8f, 1.0f); 

    for (const auto& ghost : ghosts) {
        int x = ghost.pos.x;
        int y = ghost.pos.y;
        int z = ghost.pos.z;

        for (int face = 0; face < 6; face++) {
            glm::ivec3 neighbor = glm::ivec3(x, y, z) + FACE_DIRS[face];
            if (GetBlock(neighbor.x, neighbor.y, neighbor.z) == BlockType::Air) {
                AddFace(x, y, z, face, ghostColor, (float)ghost.type, m_ghostVertices, m_ghostIndices, false);
            }
        }
    }
}
