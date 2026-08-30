#version 450

// Input dal Vertex Shader
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) flat in uint inMaterialID; // Intero puro!
layout(location = 4) in vec4 inVertexColor;
layout(location = 5) in float inEmissive;

// Binding PBR (Grattacieli)
layout(set = 0, binding = 0) uniform sampler2DArray albedoArray;
layout(set = 0, binding = 1) uniform sampler2DArray normalArray;
layout(set = 0, binding = 2) uniform sampler2DArray ormArray;

struct CellData {
    float heat;
    float pressure;
};

// Push Constants
layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 colorOverride;
    int useColorOverride;
    float seasonProgress;
    // --- THERMAL / LIGHTING PUSH CONSTANTS ---
    uint grid_width;
    uint debug_lens_active;
    vec4 lightDir;
    vec4 cameraPos;
} push;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

// --- FUNZIONI PBR (Cook-Torrance BRDF) ---
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / max(denom, 0.0000001); // Previene divisioni per 0
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// --- GENERATORE DI MATRICE TBN DA DERIVATE ---
vec3 calculateNormalFromMap(vec3 geomNormal, vec3 tangentNormal, vec3 pos, vec2 uv) {
    // Normal map: decodifica da [0,1] a [-1,1]
    vec3 tnorm = tangentNormal * 2.0 - 1.0;
    
    // Le derivate parziali estraggono i vettori tangenti al volo sulla faccia del blocco!
    vec3 dp1 = dFdx(pos);
    vec3 dp2 = dFdy(pos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    
    vec3 dp2perp = cross(dp2, geomNormal);
    vec3 dp1perp = cross(geomNormal, dp1);
    
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    
    // Gram-Schmidt / Normalizzazione
    T = normalize(T);
    B = normalize(B);
    
    mat3 TBN = mat3(T, B, geomNormal);
    return normalize(TBN * tnorm);
}

void main() {
    // 1. Campiona le 3 texture
    vec3 uvLayer = vec3(inUV, float(inMaterialID));
    vec4 albedo = texture(albedoArray, uvLayer);
    
    if(albedo.a < 0.1) discard; 
    
    vec3 normalMap = texture(normalArray, uvLayer).rgb;
    vec3 orm = texture(ormArray, uvLayer).rgb;
    
    float ao = orm.r;
    float roughness = max(orm.g, 0.05); // Tappo minimo per lo specchio perfetto
    float metallic = orm.b;

    // 2. TBN Normal Decoding
    vec3 N = normalize(inNormal);
    N = calculateNormalFromMap(N, normalMap, inWorldPos, inUV);

    // Vettori base per la Luce Direzionale
    vec3 V = normalize(push.cameraPos.xyz - inWorldPos);
    vec3 L = normalize(length(push.lightDir.xyz) > 0.1 ? -push.lightDir.xyz : vec3(0.5, 1.0, 0.3));
    vec3 H = normalize(V + L);

    // 3. Esegui il loop di illuminazione PBR
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo.rgb, metallic);

    float NDF = DistributionGGX(N, H, roughness);   
    float G   = GeometrySmith(N, V, L, roughness);      
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);
    
    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular     = numerator / denominator;
    
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;	  
    
    float NdotL = max(dot(N, L), 0.0);
    vec3 radiance = vec3(4.0); // Potenza del "Sole"
    
    // Radianza totale (Lo)
    vec3 Lo = (kD * albedo.rgb / PI + specular) * radiance * NdotL;

    // 4. Ambient & finalColor (Modulato dall'AO)
    vec3 ambient = vec3(0.15) * albedo.rgb * ao;
    vec3 finalColor = ambient + Lo;
    
    // Add glowing or colored overlays based on inEmissive
    if (inEmissive > 0.0) {
        finalColor = mix(finalColor, inVertexColor.rgb, inEmissive);
        finalColor += inVertexColor.rgb * inEmissive * 1.5; // Additive glow
    }

    // --- ATMOSPHERIC FOG ---
    float dist = distance(push.cameraPos.xyz, inWorldPos);
    vec3 viewDir = normalize(inWorldPos - push.cameraPos.xyz);
    float sunScattering = max(dot(viewDir, normalize(push.lightDir.xyz)), 0.0);
    
    // Colore base atmosfera (azzurro orizzonte) sfumato con il sole (arancio) se guardiamo verso di esso
    vec3 fogColor = mix(vec3(0.4, 0.6, 0.9), vec3(1.0, 0.8, 0.5), pow(sunScattering, 8.0));
    
    // Nebbia esponenziale
    float fogDensity = 0.001; 
    float fogFactor = 1.0 - exp(-pow(dist * fogDensity, 2.5));
    
    // Per i pianeti piccoli, limitiamo l'effetto per non coprire tutto, oppure attiviamo nebbia solo lontano
    if (dist > 100.0) {
        finalColor = mix(finalColor, fogColor, clamp(fogFactor, 0.0, 1.0));
    }

    // Tonemapping HDR e Gamma Correction
    finalColor = finalColor / (finalColor + vec3(1.0));
    finalColor = pow(finalColor, vec3(1.0 / 2.2));

    float finalAlpha = 1.0;
    if (push.useColorOverride == 1) {
        finalAlpha = push.colorOverride.a;
    } else if (inMaterialID == 6u) { // Trasparenza dell'acqua per vedere il fondale marino
        finalAlpha = 0.80;
    } else if (inMaterialID == 13u) { // Ghiaccio traslucido
        finalAlpha = 0.85;
    } else if (inVertexColor.a < 0.99 && inVertexColor.a > 0.05) {
        finalAlpha = inVertexColor.a; // Supporto alpha dai vertici (Planet Mapper, ecc.)
    }

    outColor = vec4(finalColor, finalAlpha);
}
