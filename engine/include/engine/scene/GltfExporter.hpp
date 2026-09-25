#pragma once

#include <engine/renderer/SceneObject.hpp>
#include <engine/renderer/Mesh.hpp>
#include <engine/renderer/Vertex.hpp>
#include <vector>
#include <string>
#include <memory>

class GltfExporter {
public:
    static void exportScene(const std::string& filepath,
        const std::vector<std::shared_ptr<SceneObject>>& sceneObjects,
        const std::vector<std::string>& meshNames,
        const std::vector<std::vector<Vertex>>& meshVertices,
        const std::vector<std::vector<uint32_t>>& meshIndices,
        const std::vector<std::string>& meshTexturePaths);
};