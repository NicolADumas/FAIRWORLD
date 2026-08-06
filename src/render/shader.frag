#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    float seasonProgress;
    int debugColorMode;
} ubo;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in flat uint fragTexIndex;
layout(location = 3) in vec3 fragNormal;
layout(location = 4) in vec3 fragWorldPos;
layout(location = 5) in float fragAO;
layout(location = 6) in float fragLight;

layout(binding = 1) uniform sampler2DArray texSampler;

layout(location = 0) out vec4 outColor;

// --- BIOLOGICAL SEASONAL MODEL (GPU-SIDE) ---
// Funzione di rumore analitica pseudo-casuale veloce
float getSpatialNoise(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

// Generatore di rumore sfumato (Value Noise) per creare transizioni morbide sul terreno
float getSmoothNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f); // Interpolazione cubica (Hermite)

    return mix(mix(getSpatialNoise(i + vec2(0.0, 0.0)), 
                   getSpatialNoise(i + vec2(1.0, 0.0)), u.x),
               mix(getSpatialNoise(i + vec2(0.0, 1.0)), 
                   getSpatialNoise(i + vec2(1.0, 1.0)), u.x), u.y);
}

// Equazione della Distribuzione Gaussiana
float calcGaussianPigment(float t, float mu, float sigma) {
    return exp(-pow(t - mu, 2.0) / (2.0 * pow(sigma, 2.0)));
}
// ---------------------------------------------

void main() {
    vec4 texColor = vec4(1.0);
    if (fragTexIndex > 0u) {
        texColor = texture(texSampler, vec3(fragTexCoord, float(fragTexIndex)));
    }
    
    vec3 baseColor = fragColor.rgb * texColor.rgb;
    
    int type = int(fragTexIndex);

    // --- BIOLOGICAL SEASONAL VEGETATION COLORING ---
    if (type == 1 || type == 8) { // Grass, Leaves
        // 1. Calcolo del Microclima Spaziale (Scala 0.05 per chiazze ampie ~20 blocchi)
        float noiseValue = getSmoothNoise(fragWorldPos.xz * 0.05) * 0.6 + getSmoothNoise(fragWorldPos.xz * 0.15) * 0.4;
        
        // Trasliamo il tempo globale del frame di un offset locale compreso tra -0.15 e +0.15
        float localBiologicalTime = clamp(ubo.seasonProgress + (noiseValue - 0.5) * 0.3, 0.0, 1.0);

        // 2. Valutazione delle curve dei Pigmenti Chimici
        float clorofilla  = calcGaussianPigment(localBiologicalTime, 0.15, 0.20); // Primavera/Estate
        float carotenoidi = calcGaussianPigment(localBiologicalTime, 0.55, 0.12); // Autunno (Giallo)
        float antocianine = calcGaussianPigment(localBiologicalTime, 0.70, 0.08); // Tardo Autunno (Rosso)
        float tannini     = calcGaussianPigment(localBiologicalTime, 0.90, 0.18); // Inverno (Marrone)

        // 3. Normalizzazione dei contributi cromatici
        float totalPigments = clorofilla + carotenoidi + antocianine + tannini;
        if (totalPigments > 0.0) {
            clorofilla  /= totalPigments;
            carotenoidi /= totalPigments;
            antocianine /= totalPigments;
            tannini     /= totalPigments;
        }

        // Vettori cromatici dei pigmenti puri
        vec3 colCarotenoidi = vec3(0.95, 0.65, 0.10); // Arancione/Giallo dorato
        vec3 colAntocianine = vec3(0.80, 0.15, 0.15); // Rosso vivo
        vec3 colTannini     = vec3(0.38, 0.26, 0.18); // Marrone corteccia/foglia secca

        // Miscelazione lineare
        baseColor = (baseColor * clorofilla) + 
                    (colCarotenoidi * carotenoidi) + 
                    (colAntocianine * antocianine) + 
                    (colTannini * tannini);

        // Effetto neve profondo inverno sui blocchi rivolti verso l'alto (normale Y > 0.8)
        float frostInfiltration = smoothstep(0.85, 0.98, localBiologicalTime);
        float upFactor = smoothstep(0.7, 1.0, normalize(fragNormal).y);
        // Aggiungiamo neve solo se c'è "frost" e la faccia guarda verso l'alto
        baseColor = mix(baseColor, vec3(0.90, 0.95, 0.98), frostInfiltration * upFactor * 0.8);
    }
    // ------------------------------------------------

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

    // Ambient Occlusion e Sunlight dal Voxel (passati come vertex attributes)
    float ao = fragAO;
    float blockLight = fragLight;

    // Combina illuminazione moltiplicata per l'Ambient Occlusion Voxel
    vec3 finalColor = (ambient * ao) + (diffuse + specular) * sunColor * ao * blockLight;

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

    // ==========================================
    // DEBUG COLOR MODE (Per risolvere inversione)
    // ==========================================
    if (ubo.debugColorMode == 1) {
        // Inversione totale
        finalColor = vec3(1.0) - finalColor;
    } else if (ubo.debugColorMode == 2) {
        // Swap Red/Blue
        finalColor = finalColor.bgr;
    }

    float finalAlpha = 1.0;
    if (type == 6) { // Water transparency
        finalAlpha = 0.80;
    } else if (type == 13) { // Ice transparency
        finalAlpha = 0.85;
    } else if (fragColor.a < 0.99 && fragColor.a > 0.05) {
        finalAlpha = fragColor.a;
    }

    outColor = vec4(finalColor, finalAlpha);
}
