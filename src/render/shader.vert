#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    float seasonProgress;
} ubo;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 colorOffset; // .a = 1.0 means override color, 0.0 means don't
} push;

// Dati che arrivano dal Vertex Buffer sulla CPU
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in float inTexIndex;
layout(location = 4) in vec3 inNormal;
layout(location = 5) in float inAO;
layout(location = 6) in float inLight;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out float fragTexIndex;
layout(location = 3) out vec3 fragNormal;
layout(location = 4) out vec3 fragWorldPos;
layout(location = 5) out float fragAO;
layout(location = 6) out float fragLight;

void main() {
    mat4 finalModel = push.model;
    
    vec4 worldPos = finalModel * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;
    
    if (push.colorOffset.a > 0.5) {
        fragColor = push.colorOffset.rgb; // Override color (for basic shapes if needed)
    } else {
        fragColor = inColor;
    }
    
    fragTexCoord = inTexCoord;
    fragTexIndex = inTexIndex;
    
    // Assumiamo che non ci siano scale non uniformi per ora
    fragNormal = mat3(finalModel) * inNormal;
    fragWorldPos = worldPos.xyz;
    
    fragAO = inAO;
    fragLight = inLight;
}
