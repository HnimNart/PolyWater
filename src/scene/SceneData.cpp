#include "SceneData.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <iomanip>
#include <iostream>

namespace {
/**********************************************************/
void printVec(const std::string &label, const glm::vec3 &v)
/**********************************************************/
{
  std::cout << "    " << std::left << std::setw(15) << label << ": [" << v.x
            << ", " << v.y << ", " << v.z << "]\n";
}
} // namespace

/**********************************************************/
void dumpSceneData(const SceneData &scene)
/**********************************************************/
{
  std::cout << "\n================= SCENE DATA DUMP =================\n";

  // --- Assets ---
  std::cout << "[Assets]\n";
  std::cout << "  Meshes (" << scene.meshPaths.size() << "):\n";
  for (const auto &p : scene.meshPaths)
    std::cout << "    - " << p.name << ":" << p.path << "\n";

  std::cout << "  Textures (" << scene.texturePaths.size() << "):\n";
  for (const auto &p : scene.texturePaths)
    std::cout << "    - " << p.name << ":" << p.path << "\n";

  // --- Materials ---
  std::cout << "\n[Materials]\n";
  for (size_t i = 0; i < scene.materials.size(); ++i) {
    const auto &m = scene.materials[i];
    std::cout << "  " << i << ": " << m.name << "\n";
    std::cout << "    BaseColor: [" << m.baseColor.x << ", " << m.baseColor.y
              << ", " << m.baseColor.z << "]\n";
    std::cout << "    Roughness: " << m.roughness
              << " | Metallic: " << m.metallic << "\n";
    std::cout << "    TexIndex : " << m.textureIndex << "\n";
  }

  // --- Instances ---
  std::cout << "\n[Instances]\n";
  for (const auto &inst : scene.instances) {
    std::cout << "  - Name: " << inst.name << "\n";
    std::cout << "    MeshIdx: " << inst.meshIndex
              << " | MatIdx: " << inst.materialIndex << "\n";
    printVec("Pos", inst.translation);
    printVec("Scale", inst.scale);
    std::cout << "    HitGroup: " << (int)inst.hitGroup << "\n";
  }

  // --- Lights ---
  std::cout << "\n[Lights]\n";
  for (const auto &l : scene.lights) {
    std::cout << "  Type: " << (int)l.type << " | Intensity: " << l.intensity
              << "\n";
    printVec("Color", l.color);
    printVec("Position", l.position);
  }

  // --- Globals ---
  std::cout << "\n[Globals]\n";
  std::cout << "  UseSky: " << (scene.useSky ? "True" : "False") << "\n";
  printVec("Background", scene.backgroundColor);
  printVec("Cam Eye", scene.camera.eye);
  printVec("Cam Center", scene.camera.center);
  printVec("Cam up", scene.camera.up);

  std::cout << "===================================================\n\n";
}
