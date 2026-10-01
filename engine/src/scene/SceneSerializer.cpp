#include <engine/scene/SceneSerializer.hpp>
#include <engine/scene/JsonWriter.hpp>
#include <engine/scene/JsonParser.hpp>

#include <fstream>
#include <stdexcept>
#include <filesystem>

namespace {
    // Helper : ecrit un vec3 comme un tableau JSON [x, y, z].
    void writeVec3(JsonWriter& w, const std::string& key, const glm::vec3& v) {
        w.key(key);
        w.beginArray();
        w.value(static_cast<double>(v.x));
        w.value(static_cast<double>(v.y));
        w.value(static_cast<double>(v.z));
        w.endArray();
    }

    // Helper : lit un tableau JSON [x, y, z] en vec3.
    glm::vec3 readVec3(const JsonValue& arr) {
        return glm::vec3(
            static_cast<float>(arr[0].asDouble()),
            static_cast<float>(arr[1].asDouble()),
            static_cast<float>(arr[2].asDouble())
        );
    }

    // Chemin d'asset relatif au projet : assets/<sub>/<nom de fichier>
    std::string assetRelPath(const std::string& src, const char* sub) {
        if (src.empty()) return "";
        return std::string("assets/") + sub + "/" + std::filesystem::path(src).filename().string();
    }

    void writeVec4(JsonWriter& w, const std::string& key, const glm::vec4& v) {
        w.key(key);
        w.beginArray();
        w.value(static_cast<double>(v.x)); w.value(static_cast<double>(v.y));
        w.value(static_cast<double>(v.z)); w.value(static_cast<double>(v.w));
        w.endArray();
    }

    glm::vec4 readVec4(const JsonValue& a) {
        return glm::vec4(
            static_cast<float>(a[0].asDouble()), static_cast<float>(a[1].asDouble()),
            static_cast<float>(a[2].asDouble()), static_cast<float>(a[3].asDouble()));
    }
}

void SceneSerializer::save(const std::string& path,
                           const std::vector<std::shared_ptr<SceneObject>>& sceneObjects,
                           const std::vector<std::shared_ptr<Mesh>>& availableMeshes,
                           const Camera& camera,
                           const TerrainData& terrainData,
                           const EnvironmentData& envData,
                           const std::vector<MaterialData>& materialsData, 
                           const std::vector<AnimationRefData>& animationsData,
                           const std::vector<NotifyData>& notifiesData) {
    JsonWriter w;
    w.beginObject();

    // --- Camera ---
    w.key("camera");
    w.beginObject();
    writeVec3(w, "position", camera.getPosition());
    w.key("yaw");   w.value(static_cast<double>(camera.getYaw()));
    w.key("pitch"); w.value(static_cast<double>(camera.getPitch()));
    w.endObject();

    // --- Objets ---
    w.key("objects");
    w.beginArray();
    for (const auto& obj : sceneObjects) {
        std::string clipName;
        if (obj->animClip >= 0 && obj->meshIndex >=0 && obj->meshIndex < static_cast<int>(availableMeshes.size())) {
            const auto& cl = availableMeshes[obj->meshIndex]->getClips();
            if (obj->animClip < static_cast<int>(cl.size())) clipName = cl[obj->animClip].name;
        
        } 
        w.beginObject();
        if (obj->meshIndex >= 0 && obj->meshIndex < static_cast<int>(availableMeshes.size())) {
            const auto& mesh = availableMeshes[obj->meshIndex];
            if (!mesh->getBuiltinName().empty()) {
                w.key("modelPath"); w.value("builtin:" + mesh->getBuiltinName());
                w.key("texturePath"); w.value(std::string(""));
            } else {
                w.key("modelPath"); w.value(assetRelPath(mesh->getModelPath(), "models"));
                w.key("texturePath"); w.value(assetRelPath(mesh->getTexturePath(), "textures"));
            }
        } else {
            // Objet sans mesh (groupe / Empty).
            w.key("modelPath");   w.value(std::string(""));
            w.key("texturePath"); w.value(std::string(""));
        }
        w.key("scriptPath");  w.value(obj->scriptPath);
        writeVec3(w, "position", obj->position);
        writeVec3(w, "rotation", obj->getEulerDegrees());
        writeVec3(w, "scale", obj->scale);
        w.key("metallic"); w.value(static_cast<double>(obj->metallic));
        w.key("roughness"); w.value(static_cast<double>(obj->roughness));
        w.key("worldUvScale"); w.value(static_cast<double>(obj->worldUvScale));
        w.key("materialIndex"); w.value(static_cast<double>(obj->materialIndex));
        w.key("parent"); w.value(static_cast<double>(obj->parent));
        w.key("name"); w.value(obj->name);
        w.key("animClip"); w.value(clipName);
        w.key("animSpeed"); w.value(static_cast<double>(obj->animSpeed));
        w.key("animLoop"); w.value(static_cast<double>(obj->animLoop ?1 : 0));
        w.key("animPlaying"); w.value(static_cast<double>(obj->animPlaying ? 1 : 0));
        w.key("stateMachine"); w.value(obj->stateMachine.asset ? obj->stateMachine.asset->name : std::string(""));
        w.endObject();
    }
    w.endArray();

    // --- Terrain ---
    w.key("terrain");
    w.beginObject();
    w.key("resX"); w.value(static_cast<double>(terrainData.resX));
    w.key("resY"); w.value(static_cast<double>(terrainData.resY));
    w.key("sizeX"); w.value(static_cast<double>(terrainData.sizeX));
    w.key("sizeY"); w.value(static_cast<double>(terrainData.sizeY));
    w.key("uvTiling"); w.value(static_cast<double>(terrainData.uvTiling));
    w.key("renderMode"); w.value(static_cast<double>(terrainData.renderMode));
    writeVec3(w, "position", terrainData.position);
    writeVec3(w, "rotation", terrainData.rotation);
    writeVec3(w, "scale", terrainData.scale);
    w.key("layers");
    w.beginArray();
    w.value(static_cast<double>(terrainData.layers.x));
    w.value(static_cast<double>(terrainData.layers.y));
    w.value(static_cast<double>(terrainData.layers.z));
    w.value(static_cast<double>(terrainData.layers.w));
    w.endArray();
    w.key("texturePath");   w.value(terrainData.texturePath);
    w.key("heightmapFile"); w.value(terrainData.heightmapFile);
    w.key("layerMaterial");
    w.beginArray();
    for (int i = 0; i < 4; ++i) w.value(static_cast<double>(terrainData.layerMaterial[i]));
    w.endArray();
    w.endObject();

    // --- Environnement / eclairage ---
    w.key("environment");
    w.beginObject();
    w.key("envPath");         w.value(envData.envPath);
    w.key("exposure");        w.value(static_cast<double>(envData.exposure));
    w.key("envIntensity");    w.value(static_cast<double>(envData.envIntensity));
    w.key("showSky");         w.value(static_cast<double>(envData.showSky ? 1 : 0));
    writeVec3(w, "lightDirection", envData.lightDirection);
    writeVec3(w, "lightColor",     envData.lightColor);
    w.key("lightIntensity");  w.value(static_cast<double>(envData.lightIntensity));
    w.key("ambientStrength"); w.value(static_cast<double>(envData.ambientStrength));
    w.endObject();

    // --- Materiaux partages ---
    w.key("materials");
    w.beginArray();
    for (const auto& m : materialsData) {
        w.beginObject();
        w.key("name"); w.value(m.name);
        writeVec4(w, "baseColor", m.baseColor);
        writeVec4(w, "emissive",  m.emissive);
        w.key("metallic");       w.value(static_cast<double>(m.metallic));
        w.key("roughness");      w.value(static_cast<double>(m.roughness));
        w.key("aoStrength");     w.value(static_cast<double>(m.aoStrength));
        w.key("normalStrength"); w.value(static_cast<double>(m.normalStrength));
        w.key("heightScale");    w.value(static_cast<double>(m.heightScale));
        w.key("alphaCutoff");    w.value(static_cast<double>(m.alphaCutoff));
        w.key("uvTiling");       w.value(static_cast<double>(m.uvTiling));
        w.key("maps");
        w.beginArray();
        for (int i = 0; i < 8; ++i) w.value(m.mapPaths[i]);
        w.endArray();
        w.endObject();
    }
    w.endArray();

    w.key("animations");
    w.beginArray();
    for (const auto& a : animationsData) {
        w.beginObject();
        w.key("mesh"); w.value(a.meshName);
        w.key("file"); w.value(assetRelPath(a.file, "anims"));
        w.endObject();
    }
    w.endArray();

    w.key("notifies");
    w.beginArray();
    for (const auto& note : notifiesData) {
        w.beginObject();
        w.key("mesh"); w.value(note.meshName);
        w.key("clip"); w.value(note.clipName);
        w.key("time"); w.value(static_cast<double>(note.time));
        w.key("name"); w.value(note.name);
        w.endObject();
    }
    w.endArray();

    w.endObject();   // ferme la RACINE

    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Impossible d'ouvrir le fichier pour ecriture : " + path);
    }
    file << w.toString();
}

SceneData SceneSerializer::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Impossible d'ouvrir le fichier : " + path);
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    JsonValue root = JsonParser::parse(content);

    SceneData scene;

    if (root.has("camera")) {
        const JsonValue& cam = root["camera"];
        scene.cameraPosition = readVec3(cam["position"]);
        scene.cameraYaw   = static_cast<float>(cam["yaw"].asDouble());
        scene.cameraPitch = static_cast<float>(cam["pitch"].asDouble());
    }

    if (root.has("objects")) {
        const JsonValue& objects = root["objects"];
        for (size_t i = 0; i < objects.size(); i++) {
            const JsonValue& o = objects[i];
            SceneObjectData data;
            data.modelPath   = o["modelPath"].asString();
            data.texturePath = o["texturePath"].asString();
            data.position = readVec3(o["position"]);
            data.rotation = readVec3(o["rotation"]);
            data.scale    = readVec3(o["scale"]);
            data.scriptPath = o.has("scriptPath") ? o["scriptPath"].asString() : std::string("");
            data.metallic  = o.has("metallic")  ? static_cast<float>(o["metallic"].asDouble())  : 0.0f;
            data.roughness = o.has("roughness") ? static_cast<float>(o["roughness"].asDouble()) : 0.5f;
            data.worldUvScale = o.has("worldUvScale") ? static_cast<float>(o["worldUvScale"].asDouble()) : 0.0f;
            data.materialIndex = o.has("materialIndex")
                ? static_cast<int>(o["materialIndex"].asDouble()) : -1;
            data.parent = o.has("parent") ? static_cast<int>(o["parent"].asDouble()) : -1;
            data.name = o.has("name") ? o["name"].asString() : std::string("");
            data.animClipName = o.has("animClip") ? o["animClip"].asString() : std::string("");
            data.animSpeed = o.has("animSpeed") ? static_cast<float>(o["animSpeed"].asDouble()) : 1.0f;
            data.animLoop = o.has("animLoop") ? (o["animLoop"].asDouble() != 0.0) : true;
            data.animPlaying = o.has("animPlaying") ? (o["animPlaying"].asDouble() != 0.0) : true;
            data.stateMachine = o.has("stateMachine") ? o["stateMachine"].asString() : std::string("");
            scene.objects.push_back(data);
        }
    }

    if (root.has("terrain")) {
        const JsonValue& t = root["terrain"];
        scene.terrain.hasTerrain    = true;
        scene.terrain.resX          = static_cast<int>(t["resX"].asDouble());
        scene.terrain.resY          = static_cast<int>(t["resY"].asDouble());
        scene.terrain.sizeX         = static_cast<float>(t["sizeX"].asDouble());
        scene.terrain.sizeY         = static_cast<float>(t["sizeY"].asDouble());
        scene.terrain.uvTiling      = static_cast<float>(t["uvTiling"].asDouble());
        scene.terrain.renderMode    = static_cast<int>(t["renderMode"].asDouble());
        scene.terrain.position      = readVec3(t["position"]);
        scene.terrain.rotation      = readVec3(t["rotation"]);
        scene.terrain.scale         = readVec3(t["scale"]);
        const JsonValue& L = t["layers"];
        scene.terrain.layers = glm::vec4(
            static_cast<float>(L[0].asDouble()), static_cast<float>(L[1].asDouble()),
            static_cast<float>(L[2].asDouble()), static_cast<float>(L[3].asDouble()));
        scene.terrain.texturePath   = t["texturePath"].asString();
        scene.terrain.heightmapFile = t["heightmapFile"].asString();
        if (t.has("layerMaterial")) {
            const JsonValue& lm = t["layerMaterial"];
            for (int i = 0; i < 4 && i < static_cast<int>(lm.size()); ++i)
                scene.terrain.layerMaterial[i] = static_cast<int>(lm[i].asDouble());
        }
    }

    if (root.has("environment")) {
        const JsonValue& e = root["environment"];
        scene.environment.envPath        = e["envPath"].asString();
        scene.environment.exposure       = static_cast<float>(e["exposure"].asDouble());
        scene.environment.envIntensity   = static_cast<float>(e["envIntensity"].asDouble());
        scene.environment.showSky        = e["showSky"].asDouble() != 0.0;
        scene.environment.lightDirection = readVec3(e["lightDirection"]);
        scene.environment.lightColor     = readVec3(e["lightColor"]);
        scene.environment.lightIntensity = static_cast<float>(e["lightIntensity"].asDouble());
        scene.environment.ambientStrength= static_cast<float>(e["ambientStrength"].asDouble());
    }

    if (root.has("materials")) {
        const JsonValue& arr = root["materials"];
        for (size_t i = 0; i < arr.size(); ++i) {
            const JsonValue& e = arr[i];
            MaterialData m;
            m.name           = e["name"].asString();
            m.baseColor      = readVec4(e["baseColor"]);
            m.emissive       = readVec4(e["emissive"]);
            m.metallic       = static_cast<float>(e["metallic"].asDouble());
            m.roughness      = static_cast<float>(e["roughness"].asDouble());
            m.aoStrength     = static_cast<float>(e["aoStrength"].asDouble());
            m.normalStrength = static_cast<float>(e["normalStrength"].asDouble());
            m.heightScale    = static_cast<float>(e["heightScale"].asDouble());
            m.alphaCutoff    = static_cast<float>(e["alphaCutoff"].asDouble());
            m.uvTiling       = static_cast<float>(e["uvTiling"].asDouble());
            const JsonValue& maps = e["maps"];
            for (int k = 0; k < 8 && k < static_cast<int>(maps.size()); ++k)
                m.mapPaths[k] = maps[k].asString();
            scene.materials.push_back(m);
        }
    }

    if (root.has("animations")) {
        const JsonValue& arr = root["animations"];
        for (size_t i = 0; i< arr.size(); ++i) {
            const JsonValue& a= arr[i];
            AnimationRefData ref;
            ref.meshName = a.has("mesh") ? a["mesh"].asString() : std::string("");
            ref.file = a.has("file") ? a["file"].asString() : std::string("");
            if (ref.meshName.empty() || ref.file.empty()) continue;
            scene.animations.push_back(ref);
        }
    }

    if (root.has("notifies")) {
        const JsonValue& arr = root["notifies"];
        for (size_t i = 0; i < arr.size(); ++i) {
            const JsonValue& n = arr[i];
            NotifyData nd;
            nd.meshName = n.has("mesh") ? n["mesh"].asString() : std::string("");
            nd.clipName = n.has("clip") ? n["clip"].asString() : std::string("");
            nd.time = n.has("time") ? static_cast<float>(n["time"].asDouble()) : 0.0f;
            nd.name = n.has("name") ? n["name"].asString() : std::string("");
            if (nd.meshName.empty() || nd.clipName.empty() || nd.name.empty()) continue;
            scene.notifies.push_back(nd);
        }
    }

    return scene;
}