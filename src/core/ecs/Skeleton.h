#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>

namespace fw {

    enum class RigJointType {
        HINGE,
        UNIVERSAL,
        BALL
    };

    enum class NotifyType {
        START_HITBOX,
        END_HITBOX,
        PLAY_SOUND
    };

    struct AnimNotify {
        int frame = 0;
        NotifyType type = NotifyType::START_HITBOX;
        int targetJoint = -1;
    };

    struct JointData {
        std::string name;
        int parentIndex = -1;
        uint32_t voxelEntity = 0xFFFFFFFF;
        std::string meshPath;
        
        glm::mat4 localRestTransform = glm::mat4(1.0f);
        glm::mat4 meshOffset = glm::mat4(1.0f);
        
        // Texture/Material
        uint8_t blockType = 1; // 1 = Erba di default, 0 = Air (nessuno)
        
        // Physics / Limits
        RigJointType type = RigJointType::HINGE;
        float limitMin[3] = { -45.0f, -45.0f, -45.0f };
        float limitMax[3] = {  45.0f,  45.0f,  45.0f };
        
        // Per l'animazione
        int GetDofCount() const { return 3; }
    };

    class Skeleton {
    public:
        std::vector<JointData> m_joints;
        std::vector<AnimNotify> m_notifies;
        
        const std::vector<glm::mat4>& GetGlobalTransforms() const { return m_globalTransforms; }
        std::vector<float>& GetDofState() { return m_dofState; }
        
        void UpdateForwardKinematics() {
            m_globalTransforms.resize(m_joints.size(), glm::mat4(1.0f));
            for (size_t i = 0; i < m_joints.size(); ++i) {
                if (m_joints[i].parentIndex >= 0) {
                    m_globalTransforms[i] = m_globalTransforms[m_joints[i].parentIndex] * m_joints[i].localRestTransform;
                } else {
                    m_globalTransforms[i] = m_joints[i].localRestTransform;
                }
            }
        }
        
    private:
        std::vector<glm::mat4> m_globalTransforms;
        std::vector<float> m_dofState;
    };

}
