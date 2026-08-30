#version 450

layout(push_constant) uniform GLBPushConstantData {
    mat4 mvp;
    mat4 model;
    vec4 baseColorFactor;
    float roughnessFactor;
    float metallicFactor;
    vec2 padding;
} push;

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec4 inColor;

layout(location = 0) out vec4 outColor;

const vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
const vec3 cameraPos = vec3(0.0, 100.0, 0.0); // Simple fallback

void main() {
    vec3 N = normalize(inNormal);
    vec3 L = lightDir;
    
    float NdotL = max(dot(N, L), 0.1);
    
    vec3 finalColor = push.baseColorFactor.rgb * inColor.rgb * NdotL;
    
    // Tonemapping & Gamma
    finalColor = finalColor / (finalColor + vec3(1.0));
    finalColor = pow(finalColor, vec3(1.0 / 2.2));
    
    outColor = vec4(finalColor, push.baseColorFactor.a * inColor.a);
}
