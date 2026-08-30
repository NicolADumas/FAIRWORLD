#version 450

layout(push_constant) uniform GLBPushConstantData {
    mat4 mvp;
    mat4 model;
    vec4 baseColorFactor;
    float roughnessFactor;
    float metallicFactor;
    vec2 padding;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec4 inColor;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec4 outTangent;
layout(location = 4) out vec4 outColor;

void main() {
    gl_Position = push.mvp * vec4(inPosition, 1.0);
    outWorldPos = vec3(push.model * vec4(inPosition, 1.0));
    outNormal = mat3(push.model) * inNormal;
    outUV = inUV;
    outTangent = vec4(mat3(push.model) * inTangent.xyz, inTangent.w);
    outColor = inColor;
}
