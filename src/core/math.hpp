#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace core
{

glm::mat4 composeTransform(const glm::vec3& translation,
                           const glm::vec4& rotationRaw,
                           const glm::vec3& scale);

glm::quat toQuat(const glm::vec4& rotationRaw);
glm::vec4 fromQuat(const glm::quat& rotationRaw);
glm::vec4 eulerToQuat(const glm::vec3& euler);

struct Ray
{
  glm::vec3 origin;
  glm::vec3 direction;
  float minDist = 1e-5f;
  float maxDist = 1e10f;
};

bool rayAABBIntersection(const Ray& ray, const glm::vec3& boxMin,
                         const glm::vec3& boxMax, float& t);

}  // namespace core
