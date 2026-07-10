#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragRoughMetal;
layout(location = 3) in float fragEmissive;
layout(location = 4) in float fragAO;
layout(location = 5) in float fragLight;

layout(location = 6) in vec3 fragWorldPos;
layout(location = 7) in float fragTexIndex;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2DArray texSampler;

layout(push_constant) uniform ForgePushConstantData {
    mat4 mvp;
    vec4 colorOverride;
    int useColorOverride;
    float seasonProgress;
    vec4 lightDir;
} push;

// --- BIOLOGICAL SEASONAL MODEL (GPU-SIDE) ---
float getSpatialNoise(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float getSmoothNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(getSpatialNoise(i + vec2(0.0, 0.0)), getSpatialNoise(i + vec2(1.0, 0.0)), u.x),
               mix(getSpatialNoise(i + vec2(0.0, 1.0)), getSpatialNoise(i + vec2(1.0, 1.0)), u.x), u.y);
}

float calcGaussianPigment(float t, float mu, float sigma) {
    return exp(-pow(t - mu, 2.0) / (2.0 * pow(sigma, 2.0)));
}
// ---------------------------------------------

void main() {
    float roughness = fragRoughMetal.x;
    float metallic = fragRoughMetal.y;
    vec3 baseColor = fragColor.rgb;

    // --- Triplanar Mapping ---
    if (fragTexIndex >= 0.0) {
        vec3 blending = abs(fragNormal);
        blending = normalize(max(blending, 0.00001)); // Prevent division by zero
        float b = (blending.x + blending.y + blending.z);
        blending /= vec3(b, b, b);

        vec4 xaxis = texture(texSampler, vec3(fragWorldPos.y, fragWorldPos.z, fragTexIndex));
        vec4 yaxis = texture(texSampler, vec3(fragWorldPos.x, fragWorldPos.z, fragTexIndex));
        vec4 zaxis = texture(texSampler, vec3(fragWorldPos.x, fragWorldPos.y, fragTexIndex));
        
        vec4 texColor = xaxis * blending.x + yaxis * blending.y + zaxis * blending.z;
        baseColor *= texColor.rgb; // Modulate base color with texture
    }

    // Rilevamento euristico: Se il verde è dominante, applica il ciclo stagionale
    if (baseColor.g > baseColor.r + 0.15 && baseColor.g > baseColor.b + 0.15) {
        float noiseValue = getSmoothNoise(fragWorldPos.xz * 0.05) * 0.6 + getSmoothNoise(fragWorldPos.xz * 0.15) * 0.4;
        float localBiologicalTime = clamp(push.seasonProgress + (noiseValue - 0.5) * 0.3, 0.0, 1.0);

        float clorofilla  = calcGaussianPigment(localBiologicalTime, 0.15, 0.20);
        float carotenoidi = calcGaussianPigment(localBiologicalTime, 0.55, 0.12);
        float antocianine = calcGaussianPigment(localBiologicalTime, 0.70, 0.08);
        float tannini     = calcGaussianPigment(localBiologicalTime, 0.90, 0.18);

        float totalPigments = clorofilla + carotenoidi + antocianine + tannini;
        if (totalPigments > 0.0) {
            clorofilla  /= totalPigments;
            carotenoidi /= totalPigments;
            antocianine /= totalPigments;
            tannini     /= totalPigments;
        }

        vec3 colCarotenoidi = vec3(0.95, 0.65, 0.10);
        vec3 colAntocianine = vec3(0.80, 0.15, 0.15);
        vec3 colTannini     = vec3(0.38, 0.26, 0.18);

        baseColor = (baseColor * clorofilla) + 
                    (colCarotenoidi * carotenoidi) + 
                    (colAntocianine * antocianine) + 
                    (colTannini * tannini);

        float frostInfiltration = smoothstep(0.85, 0.98, localBiologicalTime);
        float upFactor = smoothstep(0.7, 1.0, normalize(fragNormal).y);
        baseColor = mix(baseColor, vec3(0.90, 0.95, 0.98), frostInfiltration * upFactor * 0.8);
    }
    
    // Luce direzionale dinamica
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    if (length(push.lightDir.xyz) > 0.1) {
        // Usa la luce custom orientata verso la scena. 
        // Inverto per avere la direzione *verso* la sorgente (convenzione classica)
        lightDir = normalize(-push.lightDir.xyz);
    }
    
    float diff = max(dot(fragNormal, lightDir), 0.0);
    
    // Ambient base influenzata da metallic (meno metallic = più ambient diffusa)
    vec3 ambient = vec3(0.3) * (1.0 - metallic * 0.8);
    
    // Specular highlight per testare la roughness
    vec3 viewDir = normalize(vec3(0.0, 0.0, 1.0)); // Finta view dir
    vec3 reflectDir = reflect(-lightDir, fragNormal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), mix(4.0, 128.0, 1.0 - roughness));
    
    vec3 specularColor = mix(vec3(1.0), baseColor, metallic) * spec * (1.0 - roughness);
    vec3 diffuseColor = baseColor * (1.0 - metallic) * diff;
    
    // Applica AO (Scurisce gli angoli, essenziale per voxel senza texture)
    // E luce calcolata dal volume (fragLight)
    ambient *= fragAO * fragLight;
    diffuseColor *= fragAO * fragLight;
    
    // Un trucco extra per separare i blocchi: enfatizza leggermente i bordi riducendo l'AO in modo non lineare 
    // (L'AO voxel è di solito 1.0 sui lati esposti e 0.75 / 0.5 negli angoli)
    // Rendiamo l'ombra dell'AO più forte e drammatica per staccare i blocchi
    float dramaticAO = pow(fragAO, 2.0); 
    
    vec3 lighting = (ambient + diffuseColor) * dramaticAO + specularColor;
    
    // Aggiungi Emissive (ignora l'illuminazione)
    vec3 finalColor = lighting + (baseColor * fragEmissive);
    
    // Tonemapping basico
    finalColor = finalColor / (finalColor + vec3(1.0));
    finalColor = pow(finalColor, vec3(1.0/2.2)); // Gamma correction
    
    outColor = vec4(finalColor, push.useColorOverride == 1 ? push.colorOverride.a : 1.0);
}
