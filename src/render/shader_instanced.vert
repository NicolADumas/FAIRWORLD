#version 450

// Binding 0: Uniform Buffer per proiezioni e vista globale
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    float seasonProgress;
    int debugColorMode;
} ubo;

// Binding 1: SSBO con i dati delle istanze (oppure li passiamo come vertex attributes in binding = 1)
// In questo caso, usiamo Vertex Attributes con rate INSTANCE (binding 1) per massima compatibilità

// --- Attributi del mesh base (cubo 1x1x1 centrato) ---
// Binding 0, Vertex Rate
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inRoughMetal;
layout(location = 3) in float inTexIndex;
layout(location = 4) in vec3 inNormal;
layout(location = 5) in float inAo;
layout(location = 6) in float inLight;
layout(location = 7) in float inEmissive;

// --- Attributi dell'Istanza (MicroVoxel) ---
// Binding 1, Instance Rate
layout(location = 8) in vec3 instPositionOffset;
layout(location = 9) in vec4 instColorOverride;
layout(location = 10) in float instTexIndex;
// Scala del microvoxel (potremmo passarcela, ma per ora fisso a 1/16 o simile, 
// o lo passiamo come instScale se serve variarlo, ma per ora usiamo la mesh pre-scalata)

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragRoughMetal;
layout(location = 2) out float fragTexIndex;
layout(location = 3) out vec2 fragTexCoord;
layout(location = 4) out vec3 fragNormal;
layout(location = 5) out vec3 fragPos;
layout(location = 6) out float fragAo;
layout(location = 7) out float fragLight;
layout(location = 8) out float fragEmissive;

void main() {
    // Il mesh base ha vertex data. L'istanza aggiunge un offset posizionale e sovrascrive il colore/texture
    vec3 worldPos = inPosition + instPositionOffset;

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
    
    // Passa i dati al fragment shader
    fragColor = instColorOverride.rgb;
    fragRoughMetal = inRoughMetal;
    fragTexIndex = instTexIndex;
    
    // UV map di base
    fragTexCoord = inPosition.xy; 
    
    fragNormal = inNormal;
    fragPos = worldPos;
    fragAo = inAo;
    fragLight = inLight;
    fragEmissive = inEmissive;
}
