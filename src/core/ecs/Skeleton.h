#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>

namespace fw {

    struct JointData {
        std::string name;
        int parentIndex = -1;
        uint32_t voxelEntity = 0xFFFFFFFF;
        std::string meshPath;
        
        glm::mat4 localRestTransform = glm::mat4(1.0f);
        glm::mat4 meshOffset = glm::mat4(1.0f);
        
        // Per l'animazione / limiti
        int GetDofCount() const { return 3; } // xyz
    };

    class Skeleton {
    public:
        std::vector<JointData> m_joints;
        
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
