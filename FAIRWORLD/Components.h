#pragma once
#include <string>

// Componente di base per la posizione nello spazio 3D
struct TransformComponent {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Componente per identificare un'entità con un nome leggibile
struct NameComponent {
    std::string name;
};
