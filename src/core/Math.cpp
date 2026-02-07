#include "Math.hpp"

glm::mat4 math::composeTransform(const glm::vec3 &translation,
                                 const glm::vec4 &rotationRaw,
                                 const glm::vec3 &scale) {
  glm::quat rotation = toQuat(rotationRaw);
  glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
  glm::mat4 R = glm::toMat4(rotation);
  glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
  return T * R * S;
}

glm::quat math::toQuat(const glm::vec4 &rotationRaw) {
  return glm::quat(rotationRaw.w, rotationRaw.x, rotationRaw.y, rotationRaw.z);
}

glm::vec4 math::fromQuat(const glm::quat &quat) {
  return glm::vec4(quat.x, quat.y, quat.z, quat.w);
}
