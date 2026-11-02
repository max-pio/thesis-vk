// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#include "camera.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

#include <GLFW/glfw3.h>
#include <imgui.h>

namespace {
constexpr float ROTATION_SPEED = 0.005f; ///< radians per pixel of mouse movement
constexpr float ZOOM_FACTOR = 0.9f;      ///< distance is multiplied by this per wheel step (zoom in)
constexpr float MIN_DISTANCE = 0.2f;     ///< the camera never gets closer to the target than this
constexpr float MOVE_SPEED = 0.5f;       ///< target movement in distances per second
// Slightly less than 90 degrees: at exactly +-90 degrees the view direction is parallel to the up vector and the
// view matrix would flip or become undefined.
const float MAX_PITCH = glm::radians(89.0f);
} // namespace

glm::vec3 Camera::getViewDirection() const
{
    // Yaw 0 / pitch 0 gives (0, 0, -1). Positive pitch tilts the direction downwards (camera above the target).
    return glm::vec3(std::sin(mYaw) * std::cos(mPitch), -std::sin(mPitch), -std::cos(mYaw) * std::cos(mPitch));
}

glm::vec3 Camera::getPosition() const
{
    return mTarget - mDistance * getViewDirection();
}

glm::mat4 Camera::getWorldToViewSpace() const
{
    return glm::lookAt(getPosition(), mTarget, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::getViewToProjectionSpace(float aspectRatio) const
{
    glm::mat4 projection = glm::perspective(mVerticalFov, aspectRatio, mNear, mFar);
    // glm::perspective follows OpenGL, where NDC +y points up. Vulkan's NDC +y points down (origin top left, like the
    // image), and this framework does not flip the viewport, so we negate y in the projection instead. The image then
    // appears upright and counter-clockwise triangles, as seen on screen, remain front faces.
    projection[1][1] *= -1.0f;
    return projection;
}

glm::mat4 Camera::getWorldToProjectionSpace(float aspectRatio) const
{
    return getViewToProjectionSpace(aspectRatio) * getWorldToViewSpace();
}

void Camera::update(GLFWwindow* window, float timeDelta)
{
    const ImGuiIO& io = ImGui::GetIO();

    // Start a drag only if the click happened outside the GUI. WantCaptureMouse stays true while a GUI drag is in
    // progress, but we also remember our own drag, so leaving an ImGui window during our drag does not stop it.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.WantCaptureMouse)
        mDragging = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        mDragging = false;

    // Rotate: dragging right moves the camera to the left around the target, so the scene appears to turn with the
    // mouse (grab-the-scene). Dragging down moves the camera up.
    if (mDragging)
    {
        mYaw += io.MouseDelta.x * ROTATION_SPEED;
        mPitch = std::clamp(mPitch + io.MouseDelta.y * ROTATION_SPEED, -MAX_PITCH, MAX_PITCH);
    }

    // Zoom: multiplicative, so each wheel step changes the distance by the same percentage.
    if (!io.WantCaptureMouse && io.MouseWheel != 0.0f)
        mDistance = std::max(MIN_DISTANCE, mDistance * std::pow(ZOOM_FACTOR, io.MouseWheel));

    // Move the target.
    if (io.WantCaptureKeyboard)
        return;
    const auto down = [&](int key) {
        return glfwGetKey(window, key) == GLFW_PRESS;
    };

    // Horizontal axes from the yaw only, so W moves along the view direction projected onto the xz plane.
    const glm::vec3 forward(std::sin(mYaw), 0.0f, -std::cos(mYaw));
    const glm::vec3 right(std::cos(mYaw), 0.0f, std::sin(mYaw));
    glm::vec3 direction(0.0f);
    direction += forward * float(down(GLFW_KEY_W) - down(GLFW_KEY_S));
    direction += right * float(down(GLFW_KEY_D) - down(GLFW_KEY_A));
    direction.y += float(down(GLFW_KEY_E) - down(GLFW_KEY_Q));

    float speed = MOVE_SPEED * mDistance; // scales with distance so it feels the same zoomed in and out
    if (down(GLFW_KEY_LEFT_SHIFT) || down(GLFW_KEY_RIGHT_SHIFT))
        speed *= 4.0f;
    if (down(GLFW_KEY_LEFT_CONTROL) || down(GLFW_KEY_RIGHT_CONTROL))
        speed *= 0.25f;
    mTarget += direction * speed * timeDelta;
}
