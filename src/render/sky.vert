#version 450

layout(location = 0) out vec2 fragUV;

void main() {
    // Generazione del Fullscreen Triangle: 
    // Indice 0: (-1, -1) -> UV (0, 0)
    // Indice 1: ( 3, -1) -> UV (2, 0)
    // Indice 2: (-1,  3) -> UV (0, 2)
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    
    // Convertiamo da UV [0,1] a Clip Space [-1, 1]
    // In Vulkan Y va verso il basso, ma il nostro viewport lo inverte se serve.
    // Usiamo depth=1.0 per posizionare il cielo al fondo del depth buffer
    gl_Position = vec4(uv * 2.0f - 1.0f, 1.0f, 1.0f);
    
    fragUV = uv;
}
