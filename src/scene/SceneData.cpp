#include "SceneData.hpp"

#include <iomanip>
#include <iostream>

#include <glm/gtc/type_ptr.hpp>

namespace
{
/**********************************************************/
void printVec(const std::string& label, const glm::vec3& v)
/**********************************************************/
{
  std::cout << "    " << std::left << std::setw(15) << label << ": [" << v.x
            << ", " << v.y << ", " << v.z << "]\n";
}
}  // namespace

/**********************************************************/
void SceneData::clear()
/**********************************************************/
{
  meshPaths.clear();
  texturePaths.clear();
  materials.clear();
  instances.clear();
  lights.clear();
}

/**********************************************************/
int SceneData::addMesh(const std::string& name, const std::string& path)
/**********************************************************/
{
  meshPaths.push_back({name, path});
  return (int) meshPaths.size() - 1;
}

/**********************************************************/
int SceneData::addTexture(const std::string& name, const std::string& path)
/**********************************************************/
{
  texturePaths.push_back({name, path});
  return (int) texturePaths.size() - 1;
}

/**********************************************************/
void SceneData::dump() const
/**********************************************************/
{
  // --- Assets ---
  std::cout << "[Assets]\n";
  std::cout << "  Meshes (" << meshPaths.size() << "):\n";
  for (const auto& p : meshPaths)
    std::cout << "    - " << p.name << ":" << p.path << "\n";

  std::cout << "  Textures (" << texturePaths.size() << "):\n";
  for (const auto& p : texturePaths)
    std::cout << "    - " << p.name << ":" << p.path << "\n";

  // --- Materials ---
  std::cout << "\n[Materials]\n";
  for (size_t i = 0; i < materials.size(); ++i)
  {
    const auto& m = materials[i];
    std::cout << "  " << i << ": " << m.name << "\n";
    std::cout << "    BaseColor: [" << m.baseColor.x << ", " << m.baseColor.y
              << ", " << m.baseColor.z << "]\n";
    std::cout << "    Roughness: " << m.roughness
              << " | Metallic: " << m.metallic << "\n";
    std::cout << "    TexId : " << m.textureId << "\n";
  }

  // --- Instances ---
  std::cout << "\n[Instances]\n";
  for (const auto& inst : instances)
  {
    std::cout << "  - Name: " << inst.name << "\n";
    std::cout << "    MeshId: " << inst.meshId
              << " | MatId: " << inst.materialId << "\n";
    printVec("Pos", inst.translation);
    printVec("Scale", inst.scale);
    std::cout << "    HitGroup: " << (int) inst.hitGroup << "\n";
  }

  // --- Lights ---
  std::cout << "\n[Lights]\n";
  for (const auto& l : lights)
  {
    std::cout << "  Type: " << (int) l.type << " | Intensity: " << l.intensity
              << "\n";
    printVec("Color", l.color);
    printVec("Position", l.position);
  }

  // --- Globals ---
  std::cout << "\n[Globals]\n";
  std::cout << "  UseSky: " << (useSky ? "True" : "False") << "\n";
  printVec("Background", backgroundColor);
  printVec("Cam Eye", camera.eye);
  printVec("Cam Center", camera.center);
  printVec("Cam up", camera.up);

  std::cout << "===================================================\n\n";
}
