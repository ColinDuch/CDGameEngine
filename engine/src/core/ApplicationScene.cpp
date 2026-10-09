#include <engine/core/Application.hpp>
#include <engine/core/Config.hpp>
#include <engine/core/Localization.hpp>
#include <engine/core/UiHelpers.hpp>

#include <engine/renderer/UniformBufferObject.hpp>
#include <engine/renderer/GriGeometry.hpp>
#include <engine/renderer/CheckerTexture.hpp>
#include <engine/renderer/DefaultNormal.hpp>
#include <engine/renderer/Frustum.hpp>

#include <engine/scene/AnimStateMachineSerializer.hpp>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <chrono>
#include <ImGuiFileDialog.h>
#include <ImGuizmo.h>
#include <imgui.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <algorithm>
#include <functional>

int Application::createMaterial(const std::string& name) {
    auto varMaterial = std::make_shared<MaterialAsset>(
        vulkanContext, descriptorPool->getHandle(), materialLayout, name);
    materials.push_back(varMaterial);
    refreshMaterialSets();
    return static_cast<int>(materials.size() - 1);
}

void Application::refreshMaterialSets() {
    std::vector<VkDescriptorSet> sets;
    sets.reserve(materials.size());
    for (const auto& m : materials) sets.push_back(m->getSet());
    commandBuffer->setMaterials(sets, defaultMaterial->getSet());
}

void Application::saveAnimMachines() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(currentProjectDir) / "assets" / "anims";
    for (const auto& sm : animMachines) {
        const fs::path file = dir / AnimStateMachineSerializer::fileNameFor(sm->name);
        try {
            AnimStateMachineSerializer::save(*sm, file.string());
        } catch (const std::exception& e) {
            std::cerr << "[save] " << sm->name << " : " << e.what() << std::endl;
            sceneMessage += " | " + Lang::fmt("State machine not saved: %s", sm->name.c_str());
        }
    }

    // machines supprimees ou renommees : on efface leur ancien fichier,
    // sauf si une machine actuelle porte ce nom (suppression anulee par ctrl+z)
    for (const auto& f : pendingMachineDeletes) {
        bool used = false;
        for (const auto& sm : animMachines) {
            if (AnimStateMachineSerializer::fileNameFor(sm->name) == f) used = true;
        }
        std::error_code ec;
        if (!used) fs::remove(dir / f, ec);
    }
    pendingMachineDeletes.clear();
}

void Application::loadAnimMachines() {
    namespace fs = std::filesystem;
    animMachines.clear();
    pendingMachineDeletes.clear();  // un autre projet : rien a supprimer 

    const fs::path dir = fs::path(currentProjectDir) / "assets" / "anims";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".animsm") continue;
        try {
            std::string warnings;
            animMachines.push_back(AnimStateMachineSerializer::load(entry.path().string(), warnings));
            if (!warnings.empty()) {
                std::cerr << "[load]" << warnings << std::endl;
                sceneMessage += warnings;
            }
        } catch (const std::exception& e) {
            std::cerr << "[load] " << e.what() << std::endl;
            sceneMessage += " | " + Lang::fmt("Unreadable state machine: %s", entry.path().filename().string().c_str());
        }
    }

    // directory_iterator ne garantit aucun ordre : on trie pour un resultat stable
    std::sort(animMachines.begin(), animMachines.end(), [](const auto& a, const auto& b) { return a->name < b->name; }); 
}

std::shared_ptr<AnimStateMachineAsset> Application::findAnimMachine(const std::string& name) const {
    if (name.empty()) return nullptr;
    for (const auto& sm : animMachines) {
        if (sm->name == name) return sm;
    }
    return nullptr;
}

void Application::updateAnimations(float dt) {
    std::vector<VkDescriptorSet> objBoneSets(sceneObjects.size(), VK_NULL_HANDLE);
    size_t slot = 0;

    for (size_t i = 0; i < sceneObjects.size(); ++i) {
        auto& obj = sceneObjects[i];
        int meshIdx = obj->meshIndex;
        if (meshIdx < 0 || meshIdx >= static_cast<int>(availableMeshes.size())) continue;

        const auto& mesh =availableMeshes[meshIdx];
        if (!mesh || !mesh->isSkinned()) continue;
        if (mesh->getSkeleton().bones.size() > BoneBuffer::MaxBones) continue;

        const auto& clips = mesh->getClips();
        if (isPlaying) stepStateMachine(*obj, clips);
        std::vector<glm::mat4> mats;

        const auto& sk = mesh->getSkeleton();

        if (obj->animClip >= 0 && obj->animClip < static_cast<int>(clips.size())) {
            const AnimationClip& c = clips[obj->animClip];

            if (obj->animPlaying) obj->animTime += dt * obj->animSpeed;

            if (c.duration > 0.0f) {
                if (obj->animLoop) {
                    obj->animTime = std::fmod(obj->animTime, c.duration);
                    if (obj->animTime < 0.0f) obj->animTime += c.duration;
                } else {
                    obj->animTime = std::clamp(obj->animTime, 0.0f, c.duration);
                }
            }
            obj->animJustEnded = obj->animLoop ? (obj->animTime < obj->animLastTime) : (c.duration > 0.0f && obj->animTime >= c.duration - 1e-4f);

            // --- notifies on declanche ceux franchis depuis l image precedente 
            if (isPlaying && obj->animPlaying && !c.notifies.empty()) {
                float from = obj->animLastTime;
                float to = obj->animTime;

                for (const auto& note : c.notifies) {
                    bool crossed;
                    if (to >= from) {
                        crossed = (note.time > from && note.time <= to);
                    } else {
                        crossed = (note.time > from) || (note.time <= to);
                    }
                    if (crossed) {
                        std::string err;
                        scripts->callNotify(static_cast<int>(i), note.name, err);
                        if (!err.empty()) std::cout << err;
                    }
                }
            }
            obj->animLastTime = obj->animTime;

            Pose pose = c.samplePose(sk, obj->animTime);

            if (obj->animBlend < 1.0f && obj->animPrevClip >= 0 && obj->animPrevClip < static_cast<int>(clips.size())) {
                
                const AnimationClip& p = clips[obj->animPrevClip];
                obj->animPrevTime += dt;
                if (p.duration > 0.0f) obj->animPrevTime = std::fmod(obj->animPrevTime, p.duration);

                Pose prev = p.samplePose(sk, obj->animPrevTime);
                pose = blendPoses(prev, pose, obj->animBlend);

                obj->animBlend += (obj->animBlendDuration > 0.0f) ? dt / obj->animBlendDuration : 1.0f;
                if (obj->animBlend >= 1.0f) {
                    obj->animBlend = 1.0f;
                    obj->animPrevClip = -1;
                }
            }
            mats = sk.skinMatricesFromPose(pose);
        } else {
            mats= sk.skinMatrices();
        }

        if (slot >= bonePool.size()) {
            bonePool.push_back(std::make_unique<BoneBuffer>(vulkanContext, descriptorPool->getHandle(), boneLayout));
        }
        bonePool[slot]->upload(mats);
        objBoneSets[i] = bonePool[slot]->getSet();
        ++slot;
        
    }
    commandBuffer->setBoneSets(objBoneSets);
}

void Application::clearScene() {
    sceneObjects.clear();
    nextObjectId = 1;

    if (availableMeshes.empty()) {
        loadBuiltinMeshes();
    } else {
        availableMeshes.resize(builtinMeshCount);
        meshNames.resize(builtinMeshCount);
    }

    selectedObjectIndex = -1;
    selectedObjects.clear();
    selectedMeshIndex = 0;

    commandBuffer->setSceneObjects(sceneObjects);
}

void Application::saveScene() {
    if (currentProjectDir.empty()) {
        sceneMessage = Lang::tr("Pick a folder first with 'Save as...'");
        return;
    }
    try {
        namespace fs = std::filesystem;
        createProjectHierarchy(currentProjectDir);

        auto copyAsset = [&](const std::string& src, const char* sub) {
            if (src.empty()) return;
            fs::path s(src);
            std::error_code ec;
            if (!fs::exists(s, ec)) return;

            fs::path d = fs::path(currentProjectDir) / "assets" / sub / s.filename();

            if (fs::exists(d, ec) && fs::equivalent(s, d, ec)) return;

            fs::copy_file(s, d, fs::copy_options::overwrite_existing, ec);
            if (ec) sceneMessage = Lang::fmt("Copy failed: %s (%s)",
                                             s.filename().string().c_str(), ec.message().c_str());

            if (s.extension() == ".obj") {
                fs::path mtl = s; mtl.replace_extension(".mtl");
                if (fs::exists(mtl, ec)) {
                    fs::path dm = fs::path(currentProjectDir) / "assets" / sub / mtl.filename();
                    if (!(fs::exists(dm, ec) && fs::equivalent(mtl, dm, ec)))
                        fs::copy_file(mtl, dm, fs::copy_options::overwrite_existing, ec);
                }
            }
        };

        TerrainData td;
        td.hasTerrain  = true;
        td.resX = terrain->getResX();   td.resY = terrain->getResY();
        td.sizeX = terrain->getSizeX(); td.sizeY = terrain->getSizeY();
        td.uvTiling   = terrain->getUvTiling();
        td.renderMode = commandBuffer->getTerrainRenderMode();
        td.layers     = terrainLayers;
        td.position   = terrain->position;
        td.rotation   = terrain->getEulerDegrees();
        td.scale      = terrain->scale;
        td.texturePath = terrain->getTexturePath().empty() ? std::string("")
            : "assets/textures/" + fs::path(terrain->getTexturePath()).filename().string();
        td.heightmapFile = "terrain.bin";

        EnvironmentData ed;
        ed.exposure        = exposure;
        ed.envIntensity    = shininess;
        ed.showSky         = showSky;
        ed.lightDirection  = lightDirection;
        ed.lightColor      = lightColor;
        ed.lightIntensity  = lightIntensity;
        ed.ambientStrength = ambientStrength;

        std::vector<MaterialData> mats;
        for (const auto& mp : materials) {
            MaterialData md;
            const MaterialParams& p = mp->params();
            md.name = mp->getName();
            md.baseColor = p.baseColor;   md.emissive = p.emissive;
            md.metallic = p.metallic;     md.roughness = p.roughness;
            md.aoStrength = p.aoStrength; md.normalStrength = p.normalStrength;
            md.heightScale = p.heightScale;
            md.alphaCutoff = p.alphaCutoff;
            md.uvTiling = p.uvTiling;

            for (int i = 0; i < 8; ++i) {
                const std::string& src = mp->getMapPath(i);
                copyAsset(src, "materiaux");
                md.mapPaths[i] = src.empty() ? std::string("")
                    : "assets/materiaux/" + fs::path(src).filename().string();
            }
            mats.push_back(md);
        }

        const std::string& envSrc = environment->getPath();
        copyAsset(envSrc, "materiaux");
        ed.envPath = envSrc.empty() ? std::string("")
            : "assets/materiaux/" + fs::path(envSrc).filename().string();

        auto bakeTexture = [&](const std::string& src) -> std::string {
            if (src.empty()) return std::string();
            fs::path s(src);
            std::error_code ec;
            if (!fs::exists(s, ec)) return std::string();
            copyAsset(src, "textures");
            return "../textures/" + s.filename().string();
        };

        for (size_t i = 0; i < availableMeshes.size(); ++i) {
            if (!availableMeshes[i]->getBuiltinName().empty()) continue ;
            const std::string src = availableMeshes[i]->getModelPath();
            if (MeshCache::isCacheFile(src)) continue;   // deja cuit
            if (availableMeshes[i]->isSkinned()) continue; // idem pas de cuisson

            bool used = false;
            for (const auto& o : sceneObjects) {
                if (o->meshIndex == static_cast<int>(i)) { used = true; break; }
            }
            if (!used) continue;

            std::string base = !src.empty()
                             ? fs::path(src).stem().string()
                             : ((i < meshNames.size() && !meshNames[i].empty())
                                ? fs::path(meshNames[i]).stem().string() : std::string("mesh"));
            for (char& c : base) {
                if (c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
            }
            if (base.empty()) base = "mesh";

            fs::path dst = fs::path(currentProjectDir) / "assets" / "models"
                         / (base + "_" + std::to_string(i) + ".emesh");

            MeshCacheData data = availableMeshes[i]->toCacheData();
            for (auto& sm : data.subMeshes) {
                sm.diffusePath = bakeTexture(sm.diffusePath);
                sm.normalPath  = bakeTexture(sm.normalPath);
            }

            // Le cache ne doit JAMAIS faire echouer l'enregistrement de la scene.
            try {
                MeshCache::save(dst.string(), data);
                availableMeshes[i]->setModelPath(dst.string());
            } catch (const std::exception& e) {
                sceneMessage += " | " + Lang::fmt("Mesh cache skipped: %s", e.what());
                continue;
            }
            availableMeshes[i]->setModelPath(dst.string());
        }

        // Copie des assets des objets
        for (const auto& mesh : availableMeshes) {
            if (!mesh->getBuiltinName().empty()) continue;
            copyAsset(mesh->getModelPath(),   "models");
            copyAsset(mesh->getTexturePath(), "textures");
        }
        copyAsset(terrain->getTexturePath(), "textures");

        // Copie des 4 materiaux du terrain
        for (int i = 0; i < 4; ++i) {
            td.layerMaterial[i] = terrain->layerMaterial[i];
        }

        fs::path hmPath = fs::path(currentProjectDir) / "terrain.bin";
        terrain->saveHeightmap(hmPath.string());

        fs::path scenePath = fs::path(currentProjectDir) / "scene.json";
        for (auto& a : loadAnimations) {
            copyAsset(a.file, "anims");
        }

        std::vector<NotifyData> notifiesData;
        for (size_t m = 0; m <availableMeshes.size(); ++m) {
            if (!availableMeshes[m]->isSkinned()) continue;
            for (const auto& cl : availableMeshes[m]->getClips()) {
                for (const auto& note : cl.notifies) {
                    notifiesData.push_back({meshNames[m], cl.name, note.time, note.name });
                }
            }
        }

        saveAnimMachines();

        try {
            gameInput.save((fs::path(currentProjectDir) / "input.json").string());
        } catch (const std::exception& e) {
            std::cerr << "[save] " << e.what() << std::endl;
        }

        SceneSerializer::save(scenePath.string(), sceneObjects, availableMeshes, *camera, td, ed, mats, loadAnimations, notifiesData);
        std::cout << "[save] " << sceneObjects.size() << " objets -> "
                  << scenePath.string() << std::endl;
        sceneMessage = Lang::fmt("Project saved: %s", currentProjectDir.c_str());
    } catch (const std::exception& e) {
        sceneMessage = Lang::fmt("Save error: %s", e.what());
    }
}

void Application::loadPrefs() {
    std::ifstream f("preferences.txt");
    if (f) {
        std::string line;
        while (std::getline(f, line)) {
            if (input.readLine(line)) continue;
            if (line.rfind("language=", 0) == 0) {
                try { languageIndex = std::stoi(line.substr(9)); } catch (...) { languageIndex = 0; }
            }
        }
    }
    if (languageIndex != 0 && languageIndex != 1) languageIndex = 0;
    Lang::load(languageIndex == 1 ? "lang/fr.txt" : "");
}

void Application::savePrefs() {
    std::ofstream f("preferences.txt");
    if (!f) return;
    f << "language=" << languageIndex << "\n";
    input.writeTo(f);
}

void Application::loadScene() {
    try {
        namespace fs = std::filesystem;
        fs::path scenePath = fs::path(currentProjectDir) / "scene.json";
        std::cout << "[load] fichier : " << scenePath.string() << std::endl;
        SceneData scene = SceneSerializer::load(scenePath.string());

        auto resolve = [&](const std::string& p) -> std::string {
            if (p.empty()) return p;
            fs::path candidate = fs::path(currentProjectDir) / p;
            if (fs::exists(candidate)) return candidate.string();
            return p;   // repli : ancien chemin absolu/relatif au moteur
        };

        // 1. Vider la scene actuelle.
        clearScene();

        // 2. Restaurer la camera.
        camera->setState(scene.cameraPosition, scene.cameraYaw, scene.cameraPitch);

        // --- Materiaux partages (avant les objets, qui les referencent) ---
        materials.clear();
        for (const auto& md : scene.materials) {
            int idx = createMaterial(md.name);
            MaterialParams& p = materials[idx]->params();
            p.baseColor = md.baseColor;   p.emissive = md.emissive;
            p.metallic = md.metallic;     p.roughness = md.roughness;
            p.aoStrength = md.aoStrength; p.normalStrength = md.normalStrength;
            p.heightScale = md.heightScale;
            p.alphaCutoff = md.alphaCutoff;
            p.uvTiling = md.uvTiling;
            materials[idx]->uploadParams();

            for (int i = 0; i < 8; ++i) {
                if (md.mapPaths[i].empty()) continue;
                try {
                    materials[idx]->setMap(i, resolve(md.mapPaths[i]));
                } catch (const std::exception&) {
                    sceneMessage += " | " + Lang::fmt("Map not found: %s", md.mapPaths[i].c_str());
                }
            }
        }
        refreshMaterialSets();
        selectedMaterial = materials.empty() ? -1 : 0;

        // --- Machines a etats (avant les objets, qui les referencent) ---
        loadAnimMachines();

        // --- Actions de jeu : input.json, sinon celles par defaut ---
        gameInput.resetDefaults();
        const fs::path inputFile = fs::path(currentProjectDir) / "input.json";
        std::error_code inputEc;
        if (fs::exists(inputFile, inputEc)) {
            try {
                gameInput.load(inputFile.string());
            } catch (const std::exception& e) {
                std::cerr << "[load] " << e.what() << std::endl;
                sceneMessage += std::string(" | ") + Lang::tr("Game input unreadable, defaults used");
            }
        }

        // Un mesh cassé fait sauter un objet : les index se decalent, il faut une table.
        std::vector<int> indexMap(scene.objects.size(), -1);

        // 3. Recreer chaque objet : un Mesh depuis les chemins, puis un SceneObject.
        for (size_t oi = 0; oi < scene.objects.size(); ++oi) {
            const auto& objData = scene.objects[oi];
            std::string modelPath   = resolve(objData.modelPath);
            std::string texturePath = resolve(objData.texturePath);

            int meshIndex = -1;

            if (objData.modelPath.rfind("builtin:", 0) == 0) {
                const std::string bn = objData.modelPath.substr(8);
                for (size_t i = 0; i < availableMeshes.size(); ++i) {
                    if (availableMeshes[i]->getBuiltinName() == bn) {
                        meshIndex = static_cast<int>(i);
                        break;
                    }
                }
                if (meshIndex < 0) {
                    sceneMessage += " | " + Lang::fmt("Unknown primitive: %s", bn.c_str());
                    continue;
                }
            }

            for (size_t i = 0; i < availableMeshes.size(); i++) {
                if (availableMeshes[i]->getModelPath() == modelPath &&
                    availableMeshes[i]->getTexturePath() == texturePath) {
                    meshIndex = static_cast<int>(i);
                    break;
                }
            }

            if (meshIndex == -1) {
                try {
                    auto newMesh = std::make_shared<Mesh>(
                        vulkanContext,
                        modelPath, texturePath,
                        descriptorPool->getHandle(), descriptorSetLayout->getHandle(),
                        uniformBuffer->getHandle()
                    );
                    availableMeshes.push_back(newMesh);
                    meshNames.push_back(std::filesystem::path(modelPath).stem().string());
                    meshIndex = static_cast<int>(availableMeshes.size() - 1);
                } catch (const std::exception& e) {
                    sceneMessage += " | " + Lang::fmt("Mesh skipped: %s (%s)",
                                                      objData.modelPath.c_str(), e.what());
                    continue;   // on saute cet objet, le reste de la scene se charge
                }
            }

            // Cree l'objet et restaure sa transformation.
            auto newObject = std::make_shared<SceneObject>(availableMeshes[meshIndex]);
            newObject->position = objData.position;
            newObject->setEulerDegrees(objData.rotation);
            newObject->scale    = objData.scale;
            newObject->meshIndex = meshIndex;
            newObject->id = claimObjectId(objData.id);
            newObject->name = objData.name.empty()
                ? ("Object " + std::to_string(sceneObjects.size())) : objData.name;
            newObject->metallic  = objData.metallic;
            newObject->roughness = objData.roughness;
            newObject->worldUvScale = objData.worldUvScale;
            newObject->scriptPath = objData.scriptPath;
            newObject->materialIndex = objData.materialIndex;
            std::cout << "[load] objet " << sceneObjects.size()
                  << " materialIndex lu = " << objData.materialIndex << std::endl;
            sceneObjects.push_back(newObject);
            indexMap[oi] = static_cast<int>(sceneObjects.size()) - 1;
        }

        // animations du projet d abord les clips ensuite l etat de lecture
        loadAnimations.clear();
        for (const auto& ref : scene.animations) {
            std::string full = resolve(ref.file);
            for (size_t m = 0; m < availableMeshes.size(); ++m) {
                if (m >= meshNames.size() || meshNames[m] != ref.meshName) continue;
                if (!availableMeshes[m]->isSkinned()) continue;
                try {
                    auto varNewClips = Model::loadAnimations(full, availableMeshes[m]->getSkeleton());
                    availableMeshes[m]->addClips(varNewClips);
                    loadAnimations.push_back({ ref.meshName, full });
                } catch (const std::exception& e) {
                    sceneMessage += " | " + Lang::fmt("Animation skipped: %s", e.what());
                }
                break;
            }
        }

        for (const auto& nd : scene.notifies) {
            for (size_t m = 0; m < availableMeshes.size(); ++m) {
                if (m >= meshNames.size() || meshNames[m] != nd.meshName) continue;
                for (auto& cl :availableMeshes[m]->getClipsMutable()) {
                    if (cl.name == nd.clipName) cl.notifies.push_back({ nd.time, nd.name });
                }
                break;
            }
        }

        // les clips existent maintenant on peut les toruver par noms
        for (size_t oi = 0; oi < scene.objects.size(); ++oi) {
            int k = indexMap[oi];
            if (k < 0) continue;
            auto& varObj = sceneObjects[k];
            const auto& varData = scene.objects[oi];
            varObj->animSpeed = varData.animSpeed;
            varObj->animLoop = varData.animLoop;
            varObj->animPlaying = varData.animPlaying;
            varObj->stateMachine.asset = findAnimMachine(varData.stateMachine);
            varObj->animClip = -1;
            if (!varData.animClipName.empty() && varObj->meshIndex >=0 && varObj->meshIndex < static_cast<int>(availableMeshes.size())) {
                const auto& varClip =availableMeshes[varObj->meshIndex]->getClips();
                for (size_t c = 0; c < varClip.size(); ++c) {
                    if (varClip[c].name == varData.animClipName) { varObj->animClip = static_cast<int>(c); break; }
                }
            }
        }

        commandBuffer->setSceneObjects(sceneObjects);
        for (size_t k = 0; k < sceneObjects.size(); ++k) {
            std::cout << "[load] apres boucle, objet " << k
                      << " materialIndex = " << sceneObjects[k]->materialIndex << std::endl;
        }

        // Seconde passe : les parents, traduits via la table d'index.
        for (size_t oi = 0; oi < scene.objects.size(); ++oi) {
            int newIdx = indexMap[oi];
            if (newIdx < 0) continue;
            int oldParent = scene.objects[oi].parent;
            sceneObjects[newIdx]->parent =
                (oldParent >= 0 && oldParent < static_cast<int>(indexMap.size()))
                ? indexMap[oldParent] : -1;
        }
        updateWorldMatrices();

        if (scene.terrain.hasTerrain) {
            const TerrainData& td = scene.terrain;

            terrain->rebuild(td.resX, td.resY, td.sizeX, td.sizeY);

            fs::path hmPath = fs::path(currentProjectDir) / td.heightmapFile;
            terrain->loadHeightmap(hmPath.string());

            terrain->position = td.position;
            terrain->setEulerDegrees(td.rotation);
            terrain->scale = td.scale;
            terrain->setUvTiling(td.uvTiling);
            if (!td.texturePath.empty()) terrain->setTexture(resolve(td.texturePath));

            terrainLayers = td.layers;
            commandBuffer->setTerrainLayers(td.layers);
            commandBuffer->setTerrainRenderMode(td.renderMode);

            commandBuffer->setTerrain(
                terrain->getVertexBufferHandle(), terrain->getIndexBufferHandle(),
                terrain->getIndexCount(), terrain->getLayerSet(),terrain->getShadowIndexBufferHandle(), terrain->getShadowIndexCount());

            // Reflete dans l'UI
            terrainResX = td.resX;   terrainResY = td.resY;
            terrainSizeX = td.sizeX; terrainSizeY = td.sizeY;
            terrainUvTiling = td.uvTiling;

            for (int i = 0; i < 4; ++i) {
                terrain->layerMaterial[i] = td.layerMaterial[i];
            }
        }   // <-- fin du if (scene.terrain.hasTerrain)

        // --- Environnement / eclairage ---
        {
            const EnvironmentData& ed = scene.environment;
            exposure        = ed.exposure;
            shininess       = ed.envIntensity;
            showSky         = ed.showSky;
            lightDirection  = ed.lightDirection;
            lightColor      = ed.lightColor;
            lightIntensity  = ed.lightIntensity;
            ambientStrength = ed.ambientStrength;

            commandBuffer->setSky(skyRenderer.get(), showSky);

            if (!ed.envPath.empty()) {
                try {
                    environment->setTexture(resolve(ed.envPath));
                } catch (const std::exception&) {
                    sceneMessage += " | " + std::string(Lang::tr("Environment not found"));
                }
            }
        }

        sceneMessage = Lang::fmt("Scene loaded: %d object(s)", static_cast<int>(scene.objects.size()));
    } catch (const std::exception& e) {
        sceneMessage = Lang::fmt("Load error: %s", e.what());
    }

    for(size_t k = 0; k < sceneObjects.size(); ++k) {
        int meshIdx = sceneObjects[k]->meshIndex;
        std::cout << "[obj] " << k << sceneObjects[k]->name << "' meshIndex=" <<meshIdx;
        if (meshIdx >= 0 && meshIdx < static_cast<int>(availableMeshes.size())) {
            const auto& sk = availableMeshes[meshIdx]->getSkeleton();
            std::cout << " -> '" << meshNames[meshIdx] << "'"
                      << (availableMeshes[meshIdx]->isSkinned() ? " [skinne]" : "")
                      << " noeuds=" << sk.bones.size()
                      << " pesés=" << sk.skinnedCount();
        } else {
            std::cout << "   HORS BORNES";
        }
        std::cout << std::endl;
    }

    undoStack.clear();
    redoStack.clear();
}

void Application::drawFileMenu() {
    if (ImGui::BeginMenu(Lang::tr("File"))) {
        if (ImGui::MenuItem(Lang::tr("Save"))) {
            if (currentProjectDir.empty()) {
                IGFD::FileDialogConfig config; config.path = ".";
                ImGuiFileDialog::Instance()->OpenDialog("SaveProjectDlg",
                Lang::tr("Project name and location"), ".*", config);
            } else {
                saveScene();
            }
        }
        if (ImGui::MenuItem(Lang::tr("Save as..."))) {
            IGFD::FileDialogConfig config; config.path = ".";
            ImGuiFileDialog::Instance()->OpenDialog("SaveProjectDlg",
                Lang::tr("Choose the project location"), nullptr, config);
        }
        if (ImGui::MenuItem(Lang::tr("Open a project..."))) {
            IGFD::FileDialogConfig config; config.path = ".";
            ImGuiFileDialog::Instance()->OpenDialog("OpenProjectDlg",
                Lang::tr("Choose the project folder"), nullptr, config);
        }

        ImGui::Separator();

        if (ImGui::MenuItem(Lang::tr("Import a model (.obj, .fbx...)..."))) {
            IGFD::FileDialogConfig config;
            config.path = ".";
            ImGuiFileDialog::Instance()->OpenDialog(
                "ImportObjDlg", Lang::tr("Choose a 3D model"), ".obj,.fbx,.gltf,.glb,.dae,.stl", config
            );
        }

        ImGui::Separator();

        if (ImGui::MenuItem(Lang::tr("Export scene (glTF)"))) {
            IGFD::FileDialogConfig config;
            config.path = currentProjectDir.empty() ? "." : currentProjectDir;
            config.fileName = "scene_export.gltf";
            config.flags = ImGuiFileDialogFlags_ConfirmOverwrite;
            ImGuiFileDialog::Instance()->OpenDialog(
                "ExportGltfDlg", Lang::tr("Export scene (glTF)"), ".gltf", config
            );
        }

        ImGui::Separator();
        if (ImGui::MenuItem(Lang::tr("Save and quit"), nullptr, false, !currentProjectDir.empty())) {
            saveScene();
            quitting = true;
        }
        if (ImGui::MenuItem(Lang::tr("Quit"))) {
            askQuit = true;
        }

        ImGui::EndMenu();
    }
}

void Application::exportScene(const std::string& path) {
    std::vector<std::vector<Vertex>> meshVertices;
    std::vector<std::vector<uint32_t>> meshIndices;
    std::vector<std::string> meshTexturePaths;

    for (const auto& mesh : availableMeshes) {
        meshVertices.push_back(mesh->getVerticesCPU());
        meshIndices.push_back(mesh->getIndicesCPU());
        meshTexturePaths.push_back(mesh->getTexturePath());
    }

    try {
        GltfExporter::exportScene(path, sceneObjects, meshNames, meshVertices, meshIndices, meshTexturePaths);
        exportMessage = Lang::tr("Export successful!");
    } catch (const std::exception& e) {
        exportMessage = Lang::fmt("Error: %s", e.what());
    }
}

void Application::importObjModel(const std::string& path) {
    try {
        // Cree le mesh depuis le fichier .obj, sans texture (-> damier/magenta automatique).
        auto newMesh = std::make_shared<Mesh>(
            vulkanContext,
            path, "",
            descriptorPool->getHandle(), descriptorSetLayout->getHandle(),
            uniformBuffer->getHandle()
        );

        availableMeshes.push_back(newMesh);

        // Nom lisible = nom du fichier sans le chemin.
        size_t slash = path.find_last_of("/\\");
        std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
        meshNames.push_back(name);

        int newMeshIndex = static_cast<int>(availableMeshes.size() - 1);

        // Instancie un objet dans la scene avec ce mesh.
        auto newObject = std::make_shared<SceneObject>(availableMeshes[newMeshIndex]);
        newObject->position = glm::vec3(0.0f);
        newObject->meshIndex = newMeshIndex;
        newObject->id = newObjectId();
        newObject->name = name;
        sceneObjects.push_back(newObject);
        commandBuffer->setSceneObjects(sceneObjects);

        sceneMessage = Lang::fmt("Model imported: %s", name.c_str());
    } catch (const std::exception& e) {
        sceneMessage = Lang::fmt("Import error: %s", e.what());
    }
}

void Application::drawFileDialogs() {
    if (ImGuiFileDialog::Instance()->Display("ImportObjDlg")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string filePath = ImGuiFileDialog::Instance()->GetFilePathName();
            importObjModel(filePath);
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("ExportGltfDlg")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            exportScene(ImGuiFileDialog::Instance()->GetFilePathName());
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("LoadTerrainTexDlg")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string filePath = ImGuiFileDialog::Instance()->GetFilePathName();
            terrain->setTexture(filePath);
            commandBuffer->setTerrain(
                terrain->getVertexBufferHandle(), terrain->getIndexBufferHandle(),
                terrain->getIndexCount(), terrain->getLayerSet(),terrain->getShadowIndexBufferHandle(), terrain->getShadowIndexCount());
            commandBuffer->setTerrainRenderMode(0);   // texture chargee -> mode texture
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("LoadHeightmapDlg")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            pushUndo();
            std::string path = ImGuiFileDialog::Instance()->GetFilePathName();
            std::string err;
            if (terrain->loadHeightmapImage(path, heightmapMin, heightmapMax, err))
                sceneMessage = Lang::fmt("Heightmap imported: %s", path.c_str());
            else
                sceneMessage = Lang::fmt("Heightmap error: %s", err.c_str());
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("SaveProjectDlg")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            pendingProjectParent = ImGuiFileDialog::Instance()->GetCurrentPath();
            askProjectName = true;            // le flux continue avec la saisie du nom
        } else {
            projectFlowBusy = false;          // annule -> la modale revient
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("OpenProjectDlg")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            currentProjectDir = ImGuiFileDialog::Instance()->GetCurrentPath();
            loadScene();
        }
        projectFlowBusy = false;
        ImGuiFileDialog::Instance()->Close();
    }

    if (askProjectName) {
        ImGui::OpenPopup(trWin("Project name", "projectname").c_str());
        askProjectName = false;
    }

    if (ImGui::BeginPopupModal(trWin("Project name", "projectname").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(Lang::tr("Location: %s"), pendingProjectParent.c_str());
        ImGui::InputText(Lang::tr("Folder name"), projectNameBuf, sizeof(projectNameBuf));
        ImGui::Spacing();
        if (ImGui::Button(Lang::tr("Create"))) {
            std::string name = projectNameBuf;
            if (!name.empty()) {
                currentProjectDir = (std::filesystem::path(pendingProjectParent) / name).string();
                saveScene();
                projectFlowBusy = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(Lang::tr("Cancel"))) {
            projectFlowBusy = false; 
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGuiFileDialog::Instance()->Display("LoadEnvDlg")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string varPath = ImGuiFileDialog::Instance()->GetFilePathName();
            try {
                environment->setTexture(varPath);
                sceneMessage = Lang::fmt("Environment loaded: %s", varPath.c_str());
            } catch (const std::exception& e) {
                sceneMessage = Lang::fmt("Environment error: %s", e.what());
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("LoadMapDlg")) {
        if (ImGuiFileDialog::Instance()->IsOk() &&
            selectedMaterial >= 0 && selectedMaterial < static_cast<int>(materials.size())) {
            try {
                materials[selectedMaterial]->setMap(
                    mapToLoad, ImGuiFileDialog::Instance()->GetFilePathName());
            } catch (const std::exception& e) {
                sceneMessage = Lang::fmt("Texture error: %s", e.what());
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("LoadAnimDlg")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string varPath = ImGuiFileDialog::Instance()->GetFilePathName();
            int meshIdx =(selectedObjectIndex >= 0 && selectedObjectIndex < static_cast<int>(sceneObjects.size()))
                    ? sceneObjects[selectedObjectIndex]->meshIndex : -1;
            if (meshIdx >= 0 && meshIdx < static_cast<int>(availableMeshes.size())) {
                try {
                    auto newClips = Model::loadAnimations(varPath, availableMeshes[meshIdx]->getSkeleton());
                    availableMeshes[meshIdx]->addClips(newClips);
                    loadAnimations.push_back({ meshNames[meshIdx], varPath });
                    sceneMessage = Lang::fmt("Clips added: %d", static_cast<int>(newClips.size()));
                } catch (const std::exception& e) {
                    sceneMessage = Lang::fmt("Animation: %s", e.what());
                }
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("LinkScriptDlg")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            namespace fs = std::filesystem;
            fs::path chosen = ImGuiFileDialog::Instance()->GetFilePathName();
            std::error_code ec;
            fs::path rel = fs::relative(chosen, fs::path(currentProjectDir), ec);

            if (selectedObjectIndex >= 0 && selectedObjectIndex <   static_cast<int>(sceneObjects.size())) {
                if (ec || rel.empty() || rel.string().rfind("..", 0) == 0) {
                    sceneMessage = Lang::fmt("The script must be inside the project: %s",
                                             chosen.string().c_str());
                } else {
                    sceneObjects[selectedObjectIndex]->scriptPath = rel.generic_string();
                    sceneMessage = Lang::fmt("Script assigned: %s", rel.generic_string().c_str());
                }
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }

}

void Application::createProjectHierarchy(const std::string& dir) {
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(dir) / "assets" / "models");
    fs::create_directories(fs::path(dir) / "assets" / "textures");
    fs::create_directories(fs::path(dir) / "assets" / "materiaux");
    fs::create_directories(fs::path(dir) / "assets" / "anims");
    fs::create_directories(fs::path(dir) / "code");
    fs::path mainLua = fs::path(dir) / "code" / "main.lua";
    if (!fs::exists(mainLua)) {
        std::ofstream f(mainLua);
        f << "-- " << Lang::tr("Scene script.") << "\n"
          << "-- " << Lang::tr("start() runs on Play, update(dt) runs every frame.") << "\n\n"
          << "function start()\n"
          << "    Engine.log(\"" << Lang::tr("Script started:") << " \" .. Engine.count() .. \" "
          << Lang::tr("object(s)") << "\")\n"
             "end\n\n"
             "function update(dt)\n"
             "    if Engine.count() > 0 then\n"
             "        local rx, ry, rz = Engine.getRotation(0)\n"
             "        Engine.setRotation(0, rx, ry, rz + 90 * dt)\n"
             "    end\n"
             "end\n";
    }
}

void Application::startPlay() {
    // Instantane de l'etat d'edition, pour pouvoir tout restaurer au Stop.
    playSnapshot.clear();
    for (const auto& obj : sceneObjects) {
        playSnapshot.push_back({ obj->position, obj->orientation, obj->scale });
    }
    terrainSnapshot = { terrain->position, terrain->orientation, terrain->scale };

    playCameraPos   = camera->getPosition();
    playCameraYaw   = camera->getYaw();
    playCameraPitch = camera->getPitch();

    sculptEnabled = false;      // pas d'outils d'edition pendant le jeu
    terrainSelected = false;
    selectedObjectIndex = -1;

    isPlaying = true;
    physics.beginPlay();

    // les objets dotes d un corps physique (Inspecteur), pour l instant en boite ajustee au mesh
    for (const auto& o : sceneObjects) {
        if (o->bodyType == SceneObject::BodyType::None) continue;
        if (o->parent >= 0) continue;   // un enfant suit son parent : pas encore gere
        if (o->meshIndex < 0 || o->meshIndex >= static_cast<int>(availableMeshes.size())) continue;

        const Mesh& mesh = *availableMeshes[o->meshIndex];
        const glm::vec3 center = (mesh.getBoundsMin() + mesh.getBoundsMax()) * 0.5f * o->scale;
        const glm::vec3 half = (mesh.getBoundsMax() - mesh.getBoundsMin()) * 0.5f * o->scale;
        physics.addBox(o->id, o->position, o->orientation, center, half,
            o->bodyType == SceneObject::BodyType::Dynamic, o->mass);
    }

    for (auto& o : sceneObjects) o->stateMachine.reset();

    scripts->clearLog();
    scriptStatus.clear();
    if (currentProjectDir.empty()) {
        scriptStatus = Lang::tr("No project open.");
    } else {
        scripts->loadProject(currentProjectDir, scriptStatus);
        std::string err;
        scripts->callStart(err);
        if (!err.empty()) scriptStatus += "\n" + err;
    }

}

void Application::stopPlay() {
    isPlaying = false;
    physics.endPlay();

    // Restauration : l'etat d'edition doit etre intact.
    for (size_t i = 0; i < sceneObjects.size() && i < playSnapshot.size(); ++i) {
        sceneObjects[i]->position    = playSnapshot[i].position;
        sceneObjects[i]->orientation = playSnapshot[i].orientation;
        sceneObjects[i]->scale       = playSnapshot[i].scale;
    }
    terrain->position    = terrainSnapshot.position;
    terrain->orientation = terrainSnapshot.orientation;
    terrain->scale       = terrainSnapshot.scale;

    camera->setState(playCameraPos, playCameraYaw, playCameraPitch);

    scripts->unload();

}

void Application::updateGameplay(float deltaTime) {
    if (scripts && scripts->hasScript()) {
        std::string err;
        scripts->callUpdate(deltaTime, err);
        if (!err.empty()) scriptStatus = err;
    }
}

void Application::drawStartupPanel() {
    if (!currentProjectDir.empty() || projectFlowBusy) return;

    ImGui::OpenPopup(trWin("Start", "startup").c_str());

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(trWin("Start", "startup").c_str(), nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {

        ImGui::Text("%s", Lang::tr("No project open."));
        ImGui::TextDisabled("%s", Lang::tr("Scripts and saving require a project."));
        ImGui::Separator();

        if (ImGui::Button(Lang::tr("New project..."), ImVec2(190, 0))) {
            IGFD::FileDialogConfig config; config.path = ".";
            ImGuiFileDialog::Instance()->OpenDialog("SaveProjectDlg",
                Lang::tr("Choose the project location"), nullptr, config);
            projectFlowBusy = true;
            ImGui::CloseCurrentPopup();       // libere le selecteur
        }
        ImGui::SameLine();
        if (ImGui::Button(Lang::tr("Open a project..."), ImVec2(190, 0))) {
            IGFD::FileDialogConfig config; config.path = ".";
            ImGuiFileDialog::Instance()->OpenDialog("OpenProjectDlg",
                Lang::tr("Choose the project folder"), nullptr, config);
            projectFlowBusy = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}