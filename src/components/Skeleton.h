#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
        glm::mat4 globalRestTransform = glm::mat4(1.0f);
        glm::mat4 meshOffset = glm::mat4(1.0f);
        
        // Texture/Material
        uint8_t blockType = 1; // 1 = Erba di default, 0 = Air (nessuno)
        
        // Physics / Limits
        RigJointType type = RigJointType::HINGE;
        float limitMin[3] = { -45.0f, -45.0f, -45.0f };
        float limitMax[3] = {  45.0f,  45.0f,  45.0f };
        
        // Per l'animazione
        int GetDofCount() const { return 3; }
        
        uint32_t joltBodyID = 0xFFFFFFFF;
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
        
        static void GenerateBiped(Skeleton& skeleton) {
            skeleton.m_joints.clear();
            skeleton.m_dofState.clear();
            
            auto add = [&](const std::string& name, int parent, glm::vec3 off, fw::RigJointType type = fw::RigJointType::HINGE) {
                fw::JointData j;
                j.name = name; j.parentIndex = parent; j.type = type;
                j.localRestTransform = glm::translate(glm::mat4(1.0f), off);
                skeleton.m_joints.push_back(j);
                skeleton.m_dofState.resize(skeleton.m_joints.size() * 3, 0.0f);
            };

            add("Root",      -1, {0,  0,    0},    fw::RigJointType::BALL);
            add("Spine",      0, {0,  1.0f, 0},    fw::RigJointType::UNIVERSAL);
            add("Chest",      1, {0,  0.8f, 0},    fw::RigJointType::UNIVERSAL);
            add("Neck",       2, {0,  0.5f, 0},    fw::RigJointType::UNIVERSAL);
            add("Head",       3, {0,  0.4f, 0},    fw::RigJointType::BALL);
            add("ShoulderL",  2, {-0.6f, 0.4f, 0}, fw::RigJointType::BALL);
            add("ElbowL",     5, {-0.6f, 0, 0},    fw::RigJointType::HINGE);
            add("WristL",     6, {-0.5f, 0, 0},    fw::RigJointType::UNIVERSAL);
            add("ShoulderR",  2, { 0.6f, 0.4f, 0}, fw::RigJointType::BALL);
            add("ElbowR",     8, { 0.6f, 0, 0},    fw::RigJointType::HINGE);
            add("WristR",     9, { 0.5f, 0, 0},    fw::RigJointType::UNIVERSAL);
            // Mano destra - qui agganceremo l'arma
            add("Hand_R",    10, { 0.2f, 0, 0},    fw::RigJointType::BALL); 
            
            add("HipL",       0, {-0.3f,-0.2f, 0}, fw::RigJointType::BALL);
            add("KneeL",     12, {0, -0.8f, 0},    fw::RigJointType::HINGE);
            add("AnkleL",    13, {0, -0.7f, 0},    fw::RigJointType::UNIVERSAL);
            add("HipR",       0, { 0.3f,-0.2f, 0}, fw::RigJointType::BALL);
            add("KneeR",     15, {0, -0.8f, 0},    fw::RigJointType::HINGE);
            add("AnkleR",    16, {0, -0.7f, 0},    fw::RigJointType::UNIVERSAL);

            skeleton.UpdateForwardKinematics();
        }
        
    private:
        std::vector<glm::mat4> m_globalTransforms;
        std::vector<float> m_dofState;
    };

}
