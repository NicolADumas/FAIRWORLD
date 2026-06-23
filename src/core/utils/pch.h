// pch.h: questo è un file di intestazione precompilata.
// I file elencati di seguito vengono compilati una sola volta, in modo da migliorare le prestazioni per le compilazioni successive.
// Questa impostazione influisce anche sulle prestazioni di IntelliSense, incluso il completamento codice e molte altre funzionalità di esplorazione del codice.
// I file elencati qui vengono però TUTTI ricompilati se uno di essi viene aggiornato da una compilazione all'altra.
// Non aggiungere qui file soggetti a frequenti aggiornamenti; in caso contrario si perderanno i vantaggi offerti in termini di prestazioni.

#ifndef PCH_H
#define PCH_H

#include <vector>
#include <iostream>
#include <optional>
#include <set>

// --- Prevent Windows.h min/max macro conflicts with std::min / std::max ---
#define NOMINMAX

// --- GLM (Matematica 3D) ---
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE // FONDAMENTALE PER VULKAN!
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "framework.h"

#pragma warning(disable: 4996)
#endif //PCH_H
