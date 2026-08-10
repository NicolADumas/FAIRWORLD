import sys

with open("d:\\FAIRWORLD\\FAIRWORLD\\src\\render\\RenderManager.cpp", "r") as f:
    lines = f.readlines()

new_lines = []
skip = False

# 1. Replace RenderDesktop ECS block
for i, line in enumerate(lines):
    if "// --- DISEGNO FORGEWORLD (ECS) ---" in line:
        new_lines.append(line)
        new_lines.append("    bool isForge = (context && context->isForgeMode);\n")
        new_lines.append("    if (isForge) {\n")
        new_lines.append("        float aspect = (float)m_swapchainExtent.width / (float)m_swapchainExtent.height;\n")
        new_lines.append("        glm::mat4 projMatrix = glm::perspective(glm::radians(m_fov), aspect, 0.1f, 100.0f);\n")
        new_lines.append("        projMatrix[1][1] *= -1; // Inverti Y per Vulkan\n")
        new_lines.append("        glm::mat4 viewProjMatrix = projMatrix * viewMatrix;\n")
        new_lines.append("        RenderForge(m_commandBuffers[m_currentFrame], viewProjMatrix, context);\n")
        new_lines.append("    } else {\n")
        skip = True
    elif skip and "// --- DISEGNO MESH GHOST ---" in line:
        new_lines.append(line)
    elif skip and "// --- DISEGNO MOB TRAMITE PUSH CONSTANTS E INSTANCING MANUALE ---" in line:
        skip = False
        new_lines.append(line)
    elif not skip:
        # Also fix the mob logic closing brace since we wrapped ghost mesh in "else {"
        if "        for (const auto& mob : mobManager->instances) {" in line:
            new_lines.append(line)
            # Find where mobManager block ends.
            # Actually, mobManager block ends at line 1515. It's safer to just let the script do it via line count or manually.
        else:
            new_lines.append(line)

with open("d:\\FAIRWORLD\\FAIRWORLD\\src\\render\\RenderManager.cpp", "w") as f:
    f.writelines(new_lines)
