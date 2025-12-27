//
// Created by 2401Lucas on 2025-11-01.
//

#ifndef GPU_PARTICLE_SIM_TRANSFORM_H
#define GPU_PARTICLE_SIM_TRANSFORM_H

#include <glm/glm.hpp>

class Transform {
public:
    Transform();

    Transform(glm::vec3 pos, glm::vec3 rot, glm::vec3 sca);

    ~Transform() = default;

    const glm::vec3 GetPosition() const { return m_position; }
    const glm::vec3 GetRotation() const { return m_rotation; }
    const glm::vec3 GetScale() const { return m_scale; }

    void SetPosition(glm::vec3 pos) {
        m_position = pos;
        m_transformMatUpdated = true;
    }

    void AddToPosition(glm::vec3 pos) {
        m_position += pos;
        m_transformMatUpdated = true;
    }

    void SetRotation(glm::vec3 rot) {
        m_rotation = rot;
        m_transformMatUpdated = true;
    }

    void AddToRotation(glm::vec3 rot) {
        m_rotation += rot;
        m_transformMatUpdated = true;
    }

    void SetScale(glm::vec3 sca) {
        m_scale = sca;
        m_transformMatUpdated = true;
    }

    glm::vec3 Front() const {
        glm::vec3 cam_front;
        cam_front.x =
                -cos(glm::radians(m_rotation.x)) * sin(glm::radians(m_rotation.y));
        cam_front.y = -sin(glm::radians(m_rotation.x));
        cam_front.z =
                cos(glm::radians(m_rotation.x)) * cos(glm::radians(m_rotation.y));
        cam_front = glm::normalize(cam_front);
        return cam_front;
    }

    glm::mat4 GetTransformMat() {
        if (m_transformMatUpdated) CalculateTransformMat();
        return m_transformMat;
    }

private:
    void CalculateTransformMat();

    // Size; 104 bytes
    glm::mat4 m_transformMat;
    glm::vec3 m_position;
    glm::vec3 m_rotation;
    glm::vec3 m_scale;
    bool m_transformMatUpdated = true;
};


#endif //GPU_PARTICLE_SIM_TRANSFORM_H
