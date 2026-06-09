#include "pch.h"

// Questo è il file fondamentale per VMA.
// Definendo VMA_IMPLEMENTATION prima di includere vk_mem_alloc.h in UN SOLO FILE cpp,
// istruiamo il compilatore a generare il codice effettivo della libreria solo qui.
// In tutti gli altri file (es. RenderManager.cpp), includeremo vk_mem_alloc.h SENZA questa macro,
// usandolo solo come file di dichiarazione.
#define VMA_IMPLEMENTATION

// Indichiamo a VMA che stiamo usando Vulkan 1.3
#define VMA_VULKAN_VERSION 1001000

// Includiamo l'header gigante scaricato
#include "vk_mem_alloc.h"
