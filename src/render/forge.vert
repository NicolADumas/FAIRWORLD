#version 450

layout(push_constant) uniform ForgePushConstantData {
    mat4 mvp;
    vec4 colorOverride;
    int useColorOverride;
    float seasonProgress;
    uint grid_width;
    uint debug_lens_active;
    vec4 lightDir;
    vec4 cameraPos;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inRoughMetal;
layout(location = 3) in uint inMaterialID; // L'intero puro!
layout(location = 4) in vec3 inNormal;
layout(location = 5) in float inAO;
layout(location = 6) in float inLight;
layout(location = 7) in float inEmissive;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) flat out uint outMaterialID;
layout(location = 4) out vec4 outColor;
layout(location = 5) out float outEmissive;

void main() {
    // Calcolo della posizione a schermo
    gl_Position = push.mvp * vec4(inPosition, 1.0);
    
    // UV Mapping procedurale (Proiezione planare basata sulla normale della faccia)
    vec3 absNormal = abs(inNormal);
    vec2 uv = vec2(0.0);
    
    if (absNormal.y > 0.5) {
        // Faccia Top o Bottom: spalma su X e Z
        uv = inPosition.xz;
    } else if (absNormal.x > 0.5) {
        // Faccia Left o Right: spalma su Z e Y
        uv = inPosition.zy;
    } else {
        // Faccia Front o Back: spalma su X e Y
        uv = inPosition.xy;
    }
    
    outWorldPos = inPosition;
    outNormal = inNormal;
    outUV = uv;
    outMaterialID = inMaterialID;
    outColor = inColor;
    outEmissive = inEmissive;
}
