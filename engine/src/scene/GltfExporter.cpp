#define GLM_ENABLE_EXPERIMENTAL
#include <engine/scene/GltfExporter.hpp>
#include <engine/scene/JsonWriter.hpp>
#include <engine/scene/Base64.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <fstream>
#include <stdexcept>
#include <cstring>

namespace {

    std::vector<uint8_t> readFileBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            throw std::runtime_error("failed to open texture file: " + path);
        }

        size_t size = static_cast<size_t>(file.tellg());
        std::vector<uint8_t> buffer(size);

        file.seekg(0);
        file.read(reinterpret_cast<char*>(buffer.data()), size);

        return buffer;
    }

    std::vector<uint8_t> toBytes(const std::vector<uint32_t>& indices) {
        std::vector<uint8_t> bytes(indices.size() * sizeof(uint32_t));
        memcpy(bytes.data(), indices.data(), bytes.size());
        return bytes;
    }

}

void GltfExporter::exportScene(const std::string& filepath,
        const std::vector<std::shared_ptr<SceneObject>>& sceneObjects,
        const std::vector<std::string>& meshNames,
        const std::vector<std::vector<Vertex>>& meshVertices,
        const std::vector<std::vector<uint32_t>>& meshIndices,
        const std::vector<std::string>& meshTexturePaths) {
    
    JsonWriter w;
    w.beginObject();

    w.key("asset");
    w.beginObject();
    w.key("version"); w.value("2.0");
    w.key("generator"); w.value("GameEngine");
    w.endObject();

    // --- Scene / Nodes ---
    w.key("scene"); w.value(0);

    w.key("scenes");
    w.beginArray();
    w.beginObject();
    w.key("nodes");
    w.beginArray();
    for (size_t i = 0; i < sceneObjects.size(); i++) {
        w.value(static_cast<int>(i));
    }
    w.endArray();
    w.endObject();
    w.endArray();

    w.key("nodes");
    w.beginArray();
    for (const auto& object : sceneObjects) {
        w.beginObject();
        w.key("mesh"); w.value(object->meshIndex);

        w.key("translation");
        w.beginArray();
        w.value(static_cast<double>(object->position.x));
        w.value(static_cast<double>(object->position.y));
        w.value(static_cast<double>(object->position.z));
        w.endArray();

        glm::quat q = object->orientation;
        w.key("rotation");
        w.beginArray();
        w.value(static_cast<double>(q.x));
        w.value(static_cast<double>(q.y));
        w.value(static_cast<double>(q.z));
        w.value(static_cast<double>(q.w));
        w.endArray();

        w.key("scale");
        w.beginArray();
        w.value(static_cast<double>(object->scale.x));
        w.value(static_cast<double>(object->scale.y));
        w.value(static_cast<double>(object->scale.z));
        w.endArray();

        w.endObject();
    }
    w.endArray();

    // --- Meshes / Accessors / BufferViews / Buffers ---
    w.key("meshes");
    w.beginArray();
    for (size_t m = 0; m < meshVertices.size(); m++) {
        w.beginObject();
        w.key("name"); w.value(meshNames[m]);
        w.key("primitives");
        w.beginArray();
        w.beginObject();
        w.key("attributes");
        w.beginObject();
        w.key("POSITION"); w.value(static_cast<int>(m * 3 + 0));
        w.key("TEXCOORD_0"); w.value(static_cast<int>(m * 3 + 1));
        w.endObject();
        w.key("indices"); w.value(static_cast<int>(m * 3 + 2));
        w.key("material"); w.value(static_cast<int>(m)); 
        w.endObject();
        w.endArray();
        w.endObject();
    }
    w.endArray();

    w.key("images");
    w.beginArray();
    std::vector<std::vector<uint8_t>> imageBytesList;
    for (const auto& texPath : meshTexturePaths) {
        imageBytesList.push_back(readFileBytes(texPath));
        std::string imgB64 = Base64::encode(imageBytesList.back());
        w.beginObject();
        w.key("uri"); w.value("data:image/png;base64," + imgB64);
        w.endObject();
    }
    w.endArray();

    w.key("textures");
    w.beginArray();
    for (size_t m = 0; m < meshTexturePaths.size(); m++) {
        w.beginObject();
        w.key("source"); w.value(static_cast<int>(m));
        w.endObject();
    }
    w.endArray();

    w.key("materials");
    w.beginArray();
    for (size_t m = 0; m < meshTexturePaths.size(); m++) {
        w.beginObject();
        w.key("pbrMetallicRoughness");
        w.beginObject();
        w.key("baseColorTexture");
        w.beginObject();
        w.key("index"); w.value(static_cast<int>(m));
        w.endObject();
        w.key("metallicFactor"); w.value(0.0);
        w.key("roughnessFactor"); w.value(1.0);
        w.endObject();
        w.endObject();
    }
    w.endArray();

    // Un buffer unique concaténant toutes les données de tous les meshes
    std::vector<uint8_t> allBytes;
    std::vector<size_t> bufferOffsets;
    std::vector<size_t> bufferLengths;

    for (size_t m = 0; m < meshVertices.size(); m++) {
        // Positions
        std::vector<glm::vec3> positions;
        std::vector<glm::vec2> texcoords;
        for (const auto& v : meshVertices[m]) {
            positions.push_back(v.position);
            texcoords.push_back(v.texCoord);
        }

        std::vector<uint8_t> posBytes(positions.size() * sizeof(glm::vec3));
        memcpy(posBytes.data(), positions.data(), posBytes.size());
        bufferOffsets.push_back(allBytes.size());
        bufferLengths.push_back(posBytes.size());
        allBytes.insert(allBytes.end(), posBytes.begin(), posBytes.end());

        std::vector<uint8_t> uvBytes(texcoords.size() * sizeof(glm::vec2));
        memcpy(uvBytes.data(), texcoords.data(), uvBytes.size());
        bufferOffsets.push_back(allBytes.size());
        bufferLengths.push_back(uvBytes.size());
        allBytes.insert(allBytes.end(), uvBytes.begin(), uvBytes.end());

        std::vector<uint8_t> idxBytes = toBytes(meshIndices[m]);
        bufferOffsets.push_back(allBytes.size());
        bufferLengths.push_back(idxBytes.size());
        allBytes.insert(allBytes.end(), idxBytes.begin(), idxBytes.end());
    }

    w.key("bufferViews");
    w.beginArray();
    for (size_t i = 0; i < bufferOffsets.size(); i++) {
        w.beginObject();
        w.key("buffer"); w.value(0);
        w.key("byteOffset"); w.value(static_cast<int>(bufferOffsets[i]));
        w.key("byteLength"); w.value(static_cast<int>(bufferLengths[i]));
        w.endObject();
    }
    w.endArray();

    w.key("accessors");
    w.beginArray();
    for (size_t m = 0; m < meshVertices.size(); m++) {
        size_t base = m * 3;

        w.beginObject();
        w.key("bufferView"); w.value(static_cast<int>(base + 0));
        w.key("componentType"); w.value(5126);
        w.key("count"); w.value(static_cast<int>(meshVertices[m].size()));
        w.key("type"); w.value("VEC3");
        w.endObject();

        w.beginObject();
        w.key("bufferView"); w.value(static_cast<int>(base + 1));
        w.key("componentType"); w.value(5126);
        w.key("count"); w.value(static_cast<int>(meshVertices[m].size()));
        w.key("type"); w.value("VEC2");
        w.endObject();

        w.beginObject();
        w.key("bufferView"); w.value(static_cast<int>(base + 2));
        w.key("componentType"); w.value(5125);
        w.key("count"); w.value(static_cast<int>(meshIndices[m].size()));
        w.key("type"); w.value("SCALAR");
        w.endObject();
    }
    w.endArray();

    std::string base64Data = Base64::encode(allBytes);

    w.key("buffers");
    w.beginArray();
    w.beginObject();
    w.key("byteLength"); w.value(static_cast<int>(allBytes.size()));
    w.key("uri"); w.value("data:application/octet-stream;base64," + base64Data);
    w.endObject();
    w.endArray();

    w.endObject();

    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file for writing: " + filepath);
    }
    file << w.toString();
}