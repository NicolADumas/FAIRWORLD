import sys
import re

with open("d:\\FAIRWORLD\\FAIRWORLD\\src\\render\\RenderManager.cpp", "r") as f:
    content = f.read()

# 1. Trova RenderDesktop start
desktop_start = content.find("void RenderManager::RenderDesktop(")

# 2. Extract everything from `// --- SKY PASS ---` up to `// Chiude else di isForge`
sky_pass_start = content.find("    // --- SKY PASS ---")
im_draw_start = content.find("    ImDrawData* draw_data = ImGui::GetDrawData();")

if sky_pass_start == -1 or im_draw_start == -1:
    print("Non ho trovato le ancore per la sostituzione!")
    sys.exit(1)

# Extract fairworld content
fairworld_block = content[sky_pass_start:im_draw_start]

# We must remove the forge branch from fairworld_block
# It looks like:
#     // --- DISEGNO FORGEWORLD (ECS) ---
#     bool isForge = (context && context->isForgeMode);
#     if (isForge) { ... RenderForge ... } else {
#         // --- DISEGNO MESH GHOST ---
forge_router_start = fairworld_block.find("    // --- DISEGNO FORGEWORLD (ECS) ---")
forge_router_else = fairworld_block.find("        // --- DISEGNO MESH GHOST ---")
if forge_router_start != -1 and forge_router_else != -1:
    fairworld_block = fairworld_block[:forge_router_start] + "    // --- DISEGNO MESH GHOST ---" + fairworld_block[forge_router_else + 37:]

# Also remove the `    } // Chiude else di isForge` at the end
chiude_else = fairworld_block.find("    } // Chiude else di isForge")
if chiude_else != -1:
    fairworld_block = fairworld_block[:chiude_else] + fairworld_block[chiude_else+32:]

# Replace m_commandBuffers[m_currentFrame] with cmd
fairworld_block = fairworld_block.replace("m_commandBuffers[m_currentFrame]", "cmd")

# Create the RenderFairworld function string
render_fairworld_fn = "void RenderManager::RenderFairworld(VkCommandBuffer cmd, glm::mat4 viewMatrix, glm::vec3 skyColor, SharedContext* context, AssetManager* assets, MobManager* mobManager, Player* player) {\n"
render_fairworld_fn += fairworld_block
render_fairworld_fn += "}\n\n"

# Create the new routing logic for RenderDesktop
router_logic = """    bool isForge = (context && context->isForgeMode);
    if (isForge) {
        float aspect = (float)m_swapchainExtent.width / (float)m_swapchainExtent.height;
        glm::mat4 projMatrix = glm::perspective(glm::radians(m_fov), aspect, 0.1f, 100.0f);
        projMatrix[1][1] *= -1; // Inverti Y per Vulkan
        glm::mat4 viewProjMatrix = projMatrix * viewMatrix;
        
        RenderForge(m_commandBuffers[m_currentFrame], viewProjMatrix, context);
    } else {
        RenderFairworld(m_commandBuffers[m_currentFrame], viewMatrix, skyColor, context, assets, mobManager, player);
    }

"""

# Stitch it all together
new_content = content[:desktop_start] + render_fairworld_fn + content[desktop_start:sky_pass_start] + router_logic + content[im_draw_start:]

with open("d:\\FAIRWORLD\\FAIRWORLD\\src\\render\\RenderManager.cpp", "w") as f:
    f.write(new_content)
    
print("Success!")
