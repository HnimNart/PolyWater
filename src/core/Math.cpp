#include "Math.hpp"

/**********************************************************/
glm::mat4 math::composeTransform(const glm::vec3 &translation,
                                 const glm::vec4 &rotationRaw,
                                 const glm::vec3 &scale)
/**********************************************************/
{
  glm::quat rotation = toQuat(rotationRaw);
  glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
  glm::mat4 R = glm::toMat4(rotation);
  glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
  return T * R * S;
}

/**********************************************************/
glm::quat math::toQuat(const glm::vec4 &rotationRaw)
/**********************************************************/
{
  return glm::quat(rotationRaw.w, rotationRaw.x, rotationRaw.y, rotationRaw.z);
}

/**********************************************************/
glm::vec4 math::fromQuat(const glm::quat &quat)
/**********************************************************/
{
  return glm::vec4(quat.x, quat.y, quat.z, quat.w);
}

/**********************************************************/
glm::vec4 math::eulerToQuat(const glm::vec3 &euler)
/**********************************************************/
{
  glm::quat quat = glm::quat(glm::radians(euler));
  return fromQuat(quat);
}

/**********************************************************/
bool math::rayAABBIntersection(const Ray &ray, const glm::vec3 &boxMin,
                               const glm::vec3 &boxMax, float &t)
/**********************************************************/
{
  glm::vec3 dirInv = 1.0f / ray.direction;

  float t1 = (boxMin.x - ray.origin.x) * dirInv.x;
  float t2 = (boxMax.x - ray.origin.x) * dirInv.x;
  float t3 = (boxMin.y - ray.origin.y) * dirInv.y;
  float t4 = (boxMax.y - ray.origin.y) * dirInv.y;
  float t5 = (boxMin.z - ray.origin.z) * dirInv.z;
  float t6 = (boxMax.z - ray.origin.z) * dirInv.z;

  float tmin =
      glm::max(glm::max(glm::min(t1, t2), glm::min(t3, t4)), glm::min(t5, t6));
  float tmax =
      glm::min(glm::min(glm::max(t1, t2), glm::max(t3, t4)), glm::max(t5, t6));

  if (tmax < 0 || tmin > tmax) {
    t = tmax;
    return false;
  }

  t = tmin;
  return true;
}
