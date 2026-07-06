#version 450

layout(location = 0) in vec2 fragUV;

layout(push_constant) uniform PushConstants {
    mat4 invView;
    mat4 invProj;
    float timeOfDay;
    float moonPhase;
    vec2 dummy;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    // 1. Ricostruzione del raggio della telecamera (Ray Direction)
    // Trasformiamo la coordinata UV in Clip Space [-1, 1]
    vec4 target = pc.invProj * vec4(fragUV.x * 2.0 - 1.0, fragUV.y * 2.0 - 1.0, 1.0, 1.0);
    vec3 rayDir = (pc.invView * vec4(normalize(target.xyz / target.w), 0.0)).xyz;
    rayDir = normalize(rayDir);

    // 2. Colore Base del Cielo (Gradiente Zenith -> Orizzonte)
    float heightStr = max(0.0, rayDir.y);
    
    // Calcolo posizione del Sole
    float sunAngle = (pc.timeOfDay - 0.25) * 2.0 * 3.14159265;
    vec3 sunDir = normalize(vec3(cos(sunAngle), sin(sunAngle), 0.3)); // 0.3 sposta il sole un po' a sud/nord
    
    // Intensità della luce solare per il cielo (alba/tramonto/notte/giorno)
    float sunHeight = sunDir.y;
    float dayFactor = clamp((sunHeight + 0.2) / 0.4, 0.0, 1.0); // 0 = notte, 1 = giorno
    
    vec3 skyZenithDay   = vec3(0.2, 0.4, 0.8);
    vec3 skyHorizonDay  = vec3(0.6, 0.8, 1.0);
    
    vec3 skyZenithNight = vec3(0.01, 0.01, 0.05);
    vec3 skyHorizonNight= vec3(0.05, 0.05, 0.1);
    
    vec3 skySunset      = vec3(0.8, 0.4, 0.1);
    
    vec3 zenithColor  = mix(skyZenithNight, skyZenithDay, dayFactor);
    vec3 horizonColor = mix(skyHorizonNight, skyHorizonDay, dayFactor);
    
    // Aggiungi l'arancione del tramonto all'orizzonte quando il sole è basso
    float sunsetFactor = clamp(1.0 - abs(sunHeight) / 0.3, 0.0, 1.0);
    // Il tramonto si vede di più verso il sole
    float sunDirBlend = max(0.0, dot(rayDir, sunDir));
    horizonColor = mix(horizonColor, skySunset, sunsetFactor * pow(sunDirBlend, 2.0));
    
    // Gradiente finale in base all'altezza (Y) del raggio
    vec3 skyColor = mix(horizonColor, zenithColor, pow(heightStr, 0.5));
    
    // 3. Disegno del Sole
    float sunCos = dot(rayDir, sunDir);
    float sunRadiusCos = cos(0.05); // Dimensione Sole
    if (sunCos > sunRadiusCos) {
        // Glow solare
        float intensity = smoothstep(sunRadiusCos, 1.0, sunCos);
        skyColor += vec3(1.0, 0.9, 0.6) * pow(intensity, 4.0) * 5.0;
        
        // Disco solare netto
        if (sunCos > cos(0.015)) {
            skyColor = vec3(2.0, 1.9, 1.5); // Molto luminoso
        }
    }
    
    // 4. Disegno della Luna (con Fasi Lunari simulate fisicamente!)
    // La luna orbita spostata dalla fase lunare
    // moonPhase=0.0 -> Luna Nuova (luna vicina al sole, opposta di 0 radianti, quindi stesso angolo!)
    // moonPhase=0.5 -> Luna Piena (luna opposta al sole, + PI radianti)
    float moonAngle = sunAngle + pc.moonPhase * 2.0 * 3.14159265; 
    
    vec3 moonDir = normalize(vec3(cos(moonAngle), sin(moonAngle), 0.3));
    
    float moonCos = dot(rayDir, moonDir);
    float moonRadiusCos = cos(0.02); // Dimensione Luna (leggermente più grande del disco solare)
    
    if (moonCos > moonRadiusCos) {
        // Calcolo della Normale 3D della sfera lunare
        // Costruiamo un sistema di riferimento per la Luna
        vec3 up = vec3(0.0, 1.0, 0.0);
        if (abs(moonDir.y) > 0.99) up = vec3(1.0, 0.0, 0.0);
        vec3 right = normalize(cross(up, moonDir));
        up = normalize(cross(moonDir, right));
        
        // Proiezione del raggio sul piano della luna per trovare UV locale
        vec3 localDir = rayDir - moonCos * moonDir;
        float d = length(localDir);
        float r = sin(0.02); // raggio in coordinate mondiali (approssimato)
        
        vec2 uv = vec2(dot(localDir, right), dot(localDir, up)) / r;
        if (length(uv) <= 1.0) {
            float z = sqrt(1.0 - dot(uv, uv));
            // Normale sulla superficie della sfera lunare (punta verso la telecamera)
            vec3 moonNormal = normalize(uv.x * right + uv.y * up - z * moonDir);
            
            // Illuminazione del sole sulla luna (Fase Lunare procedurale)
            // sunDir  la direzione verso il sole. Se moonNormal punta verso il sole,  illuminata.
            float moonIllumination = dot(moonNormal, sunDir);
            
            vec3 moonLitColor = vec3(0.9, 0.9, 0.9);
            // Earthshine: aggiungiamo un leggero bagliore azzurrino/grigio al lato oscuro per renderlo visibile di notte!
            vec3 moonDarkColor = skyColor * 0.5 + vec3(0.01, 0.015, 0.02); 
            
            // Createri finti (Noise semplicissimo basato su UV)
            float craters = fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453);
            moonLitColor *= mix(0.7, 1.0, craters);
            moonDarkColor *= mix(0.8, 1.0, craters);
            
            // Sfumatura morbida sul terminatore (Smoothstep) invece di un taglio netto
            float litFactor = smoothstep(-0.1, 0.1, moonIllumination);
            skyColor = mix(skyColor, mix(moonDarkColor, moonLitColor, litFactor), 1.0);
        }
    }
    
    outColor = vec4(skyColor, 1.0);
}
