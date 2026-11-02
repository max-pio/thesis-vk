// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#pragma once

#include <glm/glm.hpp>

struct GLFWwindow;

/// Orbit camera: looks at a target point from a given distance. The left mouse button rotates around the target, the
/// mouse wheel zooms, WASD / QE move the target. World space is right-handed with +y up; with yaw = pitch = 0 the
/// camera looks along -z.
class Camera
{
public:
    Camera() = default;

    /// Reads mouse and keyboard (through ImGui and GLFW) and advances the camera by `timeDelta` seconds.
    /// Must be called after ImGui::NewFrame(), because it uses ImGui's mouse wheel and "want capture" flags.
    void update(GLFWwindow* window, float timeDelta);

    /// Position of the camera in world space.
    glm::vec3 getPosition() const;
    /// View matrix: transforms world space to the camera's view space (camera at the origin, looking along -z).
    glm::mat4 getWorldToViewSpace() const;
    /// Perspective projection with depth range [0, 1] and y pointing down in NDC (see camera.cpp).
    /// `aspectRatio` is width / height of the image that is rendered.
    glm::mat4 getViewToProjectionSpace(float aspectRatio) const;
    /// Projection * view.
    glm::mat4 getWorldToProjectionSpace(float aspectRatio) const;

    float mNear = 0.1f;                       ///< distance of the near plane
    float mFar = 1000.0f;                     ///< distance of the far plane
    float mVerticalFov = glm::radians(60.0f); ///< vertical field of view in radians
    glm::vec3 mTarget = glm::vec3(0.0f);      ///< orbit center
    float mDistance = 150.0f;                 ///< distance between camera and target
    float mYaw = 0.0f;                        ///< rotation around the global y axis in radians
    float mPitch = glm::radians(20.0f); ///< elevation in radians; positive = camera above the target, looking down

private:
    /// Unit vector from the camera towards the target.
    glm::vec3 getViewDirection() const;

    bool mDragging = false; ///< true while a rotation drag that started outside the GUI is in progress
};
