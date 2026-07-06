#version 450

layout(push_constant) uniform ForgePushConstantData {
    mat4 mvp;
    vec4 colorOverride;
    int useColorOverride;
    float seasonProgress;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in float inTexIndex;
layout(location = 4) in vec3 inNormal;
layout(location = 5) in float inAO;
layout(location = 6) in float inLight;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragRoughMetal;
layout(location = 3) out float fragEmissive;
layout(location = 4) out float fragAO;
layout(location = 5) out float fragLight;
layout(location = 6) out vec3 fragWorldPos;

void main() {
    // Calcolo della posizione a schermo
    gl_Position = push.mvp * vec4(inPosition, 1.0);
    
    // Selezione del colore tramite push constant
    if (push.useColorOverride == 1) {
        fragColor = push.colorOverride.rgb;
    } else {
        fragColor = inColor;
    }
    
    fragNormal = inNormal;
    fragRoughMetal = inTexCoord;
    fragEmissive = inTexIndex;
    fragAO = inAO;
    fragLight = inLight;
    fragWorldPos = inPosition;
}
