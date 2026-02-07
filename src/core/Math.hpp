#pragma once

#ifndef MATH
#define MATH 1

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace math {

glm::mat4 composeTransform(const glm::vec3 &translation,
                           const glm::vec4 &rotationRaw,
                           const glm::vec3 &scale);

glm::quat toQuat(const glm::vec4 &rotationRaw);
glm::vec4 fromQuat(const glm::quat &rotationRaw);
} // namespace math
#endif
