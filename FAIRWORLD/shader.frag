#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in float fragTexIndex;
layout(location = 3) in vec3 fragNormal;
layout(location = 4) in vec3 fragWorldPos;

layout(binding = 1) uniform sampler2DArray texSampler;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = vec4(1.0);
    if (fragTexIndex >= 0.0) {
        texColor = texture(texSampler, vec3(fragTexCoord, fragTexIndex));
    }
    
    vec3 baseColor = fragColor * texColor.rgb;

    // --- ANALYTIC BEVELING (Cubi smussati senza poligoni extra) ---
    vec3 normal = normalize(fragNormal);
    vec3 dp1 = dFdx(fragWorldPos);
    vec3 dp2 = dFdy(fragWorldPos);
    vec2 duv1 = dFdx(fragTexCoord);
    vec2 duv2 = dFdy(fragTexCoord);

    // Calcolo TBN matrix on-the-fly
    float r = 1.0 / (duv1.x * duv2.y - duv1.y * duv2.x);
    // Controllo divisione per zero
    if (!isinf(r) && !isnan(r) && abs(duv1.x * duv2.y - duv1.y * duv2.x) > 0.0001) {
        vec3 tangent = normalize((dp1 * duv2.y - dp2 * duv1.y) * r);
        vec3 bitangent = normalize((dp2 * duv1.x - dp1 * duv2.x) * r);

        float bevelWidth = 0.15; // Aumentato da 0.06 a 0.15 per renderlo super evidente!
        vec2 edge = abs(fragTexCoord - 0.5) * 2.0; // 0 to 1
        vec2 bevelAmount = smoothstep(1.0 - bevelWidth, 1.0, edge);

        // Aumentiamo la curvatura a 1.5 per fare un bevel molto rotondo
        vec3 bendX = (fragTexCoord.x < 0.5 ? -tangent : tangent) * (bevelAmount.x * 1.5);
        vec3 bendY = (fragTexCoord.y < 0.5 ? -bitangent : bitangent) * (bevelAmount.y * 1.5);

        normal = normalize(normal + bendX + bendY);
    }

    // --- PROCEDURAL PBR MATERIAL PROPERTIES ---
    float roughness = 0.8;
    float metallic = 0.0;
    
    int type = int(fragTexIndex + 0.5);
    if (type == 6 || type == 13) { // Water, Ice
        roughness = 0.02; // Super liscio
        metallic = 0.3;
    } else if (type == 12 || type == 14) { // Ore, StargateFrame
        roughness = 0.2;
        metallic = 1.0; // Puro metallo
    } else if (type == 3) { // Stone
        roughness = 0.5;
        metallic = 0.1;
    } else if (type == 1 || type == 2 || type == 8) { // Grass, Dirt, Leaves
        roughness = 0.9;
        metallic = 0.0;
    } else if (type == 7) { // Lava
        roughness = 1.0;
        metallic = 0.0;
        baseColor *= 2.5; // Emissive boost estremo
    }

    // --- PBR LIGHTING CALCULATION ---
    vec3 camPos = (inverse(ubo.view) * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    vec3 viewDir = normalize(camPos - fragWorldPos);
    
    // Sole più direzionale e tramonto dorato per massimizzare i riflessi
    vec3 sunDir = normalize(vec3(0.8, 0.5, 0.6));
    vec3 sunColor = vec3(1.2, 1.1, 0.9) * 2.0;
    vec3 ambient = vec3(0.2, 0.25, 0.35) * baseColor;
    
    // Diffuse (Lambert)
    float NdotL = max(dot(normal, sunDir), 0.0);
    vec3 diffuse = baseColor * NdotL * (1.0 - metallic);
    
    // Specular (Blinn-Phong pompato)
    vec3 halfVector = normalize(sunDir + viewDir);
    float NdotH = max(dot(normal, halfVector), 0.0);
    float shininess = exp2((1.0 - roughness) * 12.0 + 1.0);
    float specPower = pow(NdotH, shininess);
    vec3 specularColor = mix(vec3(0.04), baseColor, metallic);
    // Moltiplicatore 3.0 per far brillare molto i bordi!
    vec3 specular = specularColor * specPower * NdotL * 3.0; 

    // Combina illuminazione
    vec3 finalColor = ambient + (diffuse + specular) * sunColor;

    // Se è lava, aggiungi self-illumination (emissive)
    if (type == 7) {
        finalColor += baseColor * 1.5;
    }

    // Aggiungi un finto "Rim Light" sui bordi (angolo di Fresnel)
    float rim = 1.0 - max(dot(viewDir, normal), 0.0);
    rim = smoothstep(0.6, 1.0, rim);
    finalColor += vec3(0.3, 0.4, 0.5) * rim * (1.0 - roughness);

    // Tone mapping semplice
    finalColor = finalColor / (finalColor + vec3(1.0));
    // Gamma correction
    finalColor = pow(finalColor, vec3(1.0/2.2));

    outColor = vec4(finalColor, 1.0);
}
