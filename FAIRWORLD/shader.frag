#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in float fragTexIndex;

layout(binding = 1) uniform sampler2DArray texSampler;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = vec4(1.0);
    if (fragTexIndex >= 0.0) {
        texColor = texture(texSampler, vec3(fragTexCoord, fragTexIndex));
    }

    // Moltiplica il colore del vertex (ombreggiatura facce) con la texture RGB.
    // L'alpha della texture viene IGNORATO: i blocchi sono sempre opachi.
    // - Texture bianca (default) = mostra il vertex color puro
    // - Texture dipinta = applica il colore sulla faccia del blocco
    outColor = vec4(fragColor * texColor.rgb, 1.0);
}
