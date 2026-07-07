#version 450

layout(location = 0) in vec3 inViewDir;     // Direzione della telecamera
layout(location = 1) in vec3 inWorldPos;    // Posizione attuale del frammento

layout(location = 0) out vec4 outColor;

// Parametri passati dalla CPU (Uniform Buffer)
layout(binding = 0) uniform UBO {
    vec3 sunDir;
    float planetRadius;
    float atmosphereRadius;
    float atmosphereDensity; // Il valore calibrato nel Map Builder
} ubo;

// Costanti fisiche di dispersione
const vec3 betaR = vec3(5.5e-6, 13.0e-6, 22.4e-6); // Rayleigh (luce blu)
const float betaM = 21e-6;                         // Mie (luce bianca/foschia)
const float scaleHeightR = 8000.0;                 // Decadimento densità aria (m)
const float scaleHeightM = 1200.0;                 // Decadimento polveri (m)

// Funzione di intersezione Raggio-Sfera
vec2 RaySphereIntersect(vec3 r0, vec3 rd, float radius) {
    float b = dot(r0, rd);
    float c = dot(r0, r0) - radius * radius;
    float h = b * b - c;
    if (h < 0.0) return vec2(-1.0);
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}

void main() {
    vec3 rayOrigin = inWorldPos;
    vec3 rayDir = normalize(inViewDir);
    
    // 1. Dove il raggio visivo colpisce l'atmosfera
    vec2 hit = RaySphereIntersect(rayOrigin, rayDir, ubo.atmosphereRadius);
    if (hit.y < 0.0) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0); // Spazio profondo
        return;
    }
    
    // Limita il segmento di raymarching
    float tMin = max(0.0, hit.x);
    float tMax = hit.y;
    
    // Se colpiamo il pianeta solido, fermiamo il raggio lì
    vec2 planetHit = RaySphereIntersect(rayOrigin, rayDir, ubo.planetRadius);
    if (planetHit.x > 0.0) tMax = min(tMax, planetHit.x);
    
    // 2. Raymarching Setup
    const int NUM_SAMPLES = 16;
    float segmentLength = (tMax - tMin) / float(NUM_SAMPLES);
    float tCurrent = tMin + segmentLength * 0.5;
    
    vec3 totalRayleigh = vec3(0.0);
    vec3 totalMie = vec3(0.0);
    float opticalDepthR = 0.0;
    
    // 3. Raymarching Loop
    for (int i = 0; i < NUM_SAMPLES; i++) {
        vec3 samplePos = rayOrigin + rayDir * tCurrent;
        float height = length(samplePos) - ubo.planetRadius;
        
        // Densità basata sull'altitudine e sul moltiplicatore del Map Builder
        float density = exp(-height / scaleHeightR) * segmentLength * ubo.atmosphereDensity;
        opticalDepthR += density;
        
        // Calcolo della luce solare diretta su questo punto (Out-scattering)
        // [Semplificato per performance: assumiamo luce costante per questo step]
        vec3 attenuation = exp(-betaR * opticalDepthR);
        
        totalRayleigh += density * attenuation;
        tCurrent += segmentLength;
    }
    
    // Colore finale disperso verso l'occhio
    vec3 finalColor = totalRayleigh * betaR * 10.0; // Moltiplicatore intensità
    
    outColor = vec4(finalColor, 1.0);
}
