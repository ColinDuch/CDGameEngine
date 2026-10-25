#include <engine/renderer/Model.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <stdexcept>
#include <filesystem>
#include <iostream>
#include <functional>
#include <algorithm>
#include <assimp/config.h>
#include <filesystem>

namespace {

    // Assimp est row-major, glm column-major : il faut transposer.
    glm::mat4 toGlm(const aiMatrix4x4& m) {
        return glm::mat4(
            m.a1, m.b1, m.c1, m.d1,
            m.a2, m.b2, m.c2, m.d2,
            m.a3, m.b3, m.c3, m.d3,
            m.a4, m.b4, m.c4, m.d4);
    }

    glm::vec3 toVec(const aiVector3D& v) { return glm::vec3(v.x, v.y, v.z); }
    glm::quat toQuat(const aiQuaternion& q) { return glm::quat(q.w, q.x, q.y, q.z); }

    //convertit les AI anims d'une scene en clip pour uen squelette donne 
    std::vector<AnimationClip> extractClips(const aiScene* scene, const Skeleton& skeleton) {
        std::vector<AnimationClip> out;

        // "a" est un compteur
        for (unsigned int a = 0; a < scene->mNumAnimations; ++a) {
            const aiAnimation* anim = scene->mAnimations[a];
            
            AnimationClip clip;
            clip.name = (anim->mName.length > 0) ? anim->mName.C_Str() : ("clip_" + std::to_string(a));

            double tps = (anim->mTicksPerSecond != 0.0) ? anim->mTicksPerSecond : 25.0; 
            clip.duration = static_cast<float>(anim->mDuration / tps);
            std::cout << "[clip] '" <<clip.name <<"' duree=" << anim->mDuration
                      << " ticks, tps=" << anim->mTicksPerSecond
                      <<" -> " <<clip.duration <<" S, "
                      << anim->mNumChannels << " canaux" << std::endl;
            clip.fps = static_cast<float>(tps);

            // "c" est un compteur
            for (unsigned int c = 0 ; c < anim->mNumChannels; ++c) {
                const aiNodeAnim* ch = anim->mChannels[c];

                /*
                l association se fait par noms, un fichier d'aniamtion separer 
                n a pas les meme index mais bien les noms d os
                */
                int bi = skeleton.find(ch->mNodeName.C_Str());
                if (bi < 0) {
                    std::cout <<"[clip]     canal ignore : '" <<ch->mNodeName.C_Str() << "'" <<std::endl;
                    continue;
                }

                AnimChannel out_ch;
                out_ch.bone = bi;
                for (unsigned int k = 0; k < ch->mNumPositionKeys; ++k) out_ch.positions.push_back({static_cast<float>(ch->mPositionKeys[k].mTime / tps),
                                  toVec(ch->mPositionKeys[k].mValue) });
                for (unsigned int k = 0; k < ch->mNumRotationKeys; ++k) out_ch.rotations.push_back({static_cast<float>(ch->mRotationKeys[k].mTime / tps),
                                  toQuat(ch->mRotationKeys[k].mValue) });
                for (unsigned int k = 0; k < ch->mNumScalingKeys; ++k) out_ch.scales.push_back({static_cast<float>(ch->mScalingKeys[k].mTime / tps),
                                  toVec(ch->mScalingKeys[k].mValue) });
                
                clip.channels.push_back(std::move(out_ch));
            }
            /*
            certains export annoncent uen duree fausse donc on la reprend
            sur la dernière cle reelle 
            */
            float maxKey =0.0f;
            for (const auto& c : clip.channels) {
                if (!c.positions.empty()) maxKey = std::max(maxKey, c.positions.back().time);
                if (!c.rotations.empty()) maxKey = std::max(maxKey, c.rotations.back().time);
                if (!c.scales.empty()) maxKey = std::max(maxKey, c.scales.back().time);
            }
            if (maxKey > clip.duration) clip.duration = maxKey;
            if (!clip.channels.empty()) out.push_back(std::move(clip));
        }
        return out;
    }

    // Garde les 4 plus gros poids : on remplace le plus faible s'il est depasse.
    void addWeight(VertexSkin& s, int bone, float w) {
        int minSlot = 0;
        for (int k = 1; k < 4; ++k) {
            if (s.weights[k] < s.weights[minSlot]) minSlot = k;
        }
        if (w > s.weights[minSlot]) {
            s.boneIds[minSlot] = bone;
            s.weights[minSlot] = w;
        }
    }

} // namespace

Model::Model(const std::string& filepath, bool yUpToZUp) {
    loadModel(filepath, yUpToZUp);
}

void Model::loadModel(const std::string& filepath, bool yUpToZUp) {
    Assimp::Importer importer;

    /*
    sans ca assimp divise chauqe os en noeuds 
    la hierarchie triple donc de taille et les
    canaux d'animation ne visent plus les os 
    */
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

    const aiScene* scene = importer.ReadFile(filepath,
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices |
        aiProcess_FlipUVs);   // Vulkan : origine UV en haut a gauche

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        throw std::runtime_error(std::string("Assimp: ") + importer.GetErrorString());
    }

    // Dossier du fichier, pour prefixer les chemins de textures.
    size_t slash = filepath.find_last_of("/\\");
    std::string baseDir = (slash == std::string::npos) ? "" : filepath.substr(0, slash + 1);

    // --- 1) Lit TOUS les materiaux (couleur Kd + textures) ---
        for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
        const aiMaterial* mat = scene->mMaterials[i];
        Material material{};

        // Couleur diffuse (Kd) -> couleur de base.
        aiColor3D kd(1.0f, 1.0f, 1.0f);
        if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, kd) == AI_SUCCESS) {
            material.baseColor = { kd.r, kd.g, kd.b };
        }

        // Facteurs PBR (fournis par le glTF ; l'.obj gardera les valeurs par defaut).
        float mv = 0.0f, rv = 0.5f;
        if (mat->Get(AI_MATKEY_METALLIC_FACTOR, mv)  == AI_SUCCESS) material.metallic  = mv;
        if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, rv) == AI_SUCCESS) material.roughness = rv;

        aiString texPath;

        // Texture diffuse (map_Kd).
        if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
            material.referencesTexture = true;   // le .mtl VEUT une texture (fichier present ou non)
            std::string candidate = baseDir + texPath.C_Str();
            if (std::filesystem::exists(candidate)) {
                material.diffuseTexturePath = candidate;
            } else {
                std::cout << "[Model] texture diffuse introuvable : " << candidate << std::endl;
            }
        }

        // Normal map (NORMALS, ou HEIGHT pour certains .obj/.mtl).
        aiReturn r = mat->GetTexture(aiTextureType_NORMALS, 0, &texPath);
        if (r != AI_SUCCESS) {
            r = mat->GetTexture(aiTextureType_HEIGHT, 0, &texPath);
        }
        if (r == AI_SUCCESS) {
            std::string candidate = baseDir + texPath.C_Str();
            if (std::filesystem::exists(candidate)) {
                material.normalTexturePath = candidate;
            } else {
                std::cout << "[Model] normal map introuvable : " << candidate << std::endl;
            }
        }

        materials.push_back(material);
    }

    // Garde-fou : au moins un materiau par defaut (blanc) si le fichier n'en a aucun.
    if (materials.empty()) {
        materials.push_back(Material{});
    }

    // --- 1bis) Squelette : la hierarchie de noeuds, AVANT la boucle des meshes ---
    bool anyBones = false;
    for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
        if (scene->mMeshes[m]->mNumBones > 0) { anyBones = true; break; }
    }

    if (anyBones) {
        // On enregistre TOUS les noeuds, pas seulement ceux qui portent des poids :
        // la chaine de parents doit etre complete pour composer les transformations.
        std::function<void(const aiNode*, int)> addNode = [&](const aiNode* node, int parent) {
            BoneInfo b;
            b.name      = node->mName.C_Str();
            b.parent    = parent;
            b.localBind = toGlm(node->mTransformation);

            int idx = static_cast<int>(skeleton.bones.size());
            skeleton.bones.push_back(b);
            std::string key = normalizeBoneName(b.name);
            if (skeleton.nameToIndex.find(key) == skeleton.nameToIndex.end()) {
                skeleton.nameToIndex[key] = idx;   // 1er gagne si doublon de nom
            }
            for (unsigned int c = 0; c < node->mNumChildren; ++c) {
                addNode(node->mChildren[c], idx);
            }
        };
        addNode(scene->mRootNode, -1);
        skeleton.globalInverse = glm::inverse(toGlm(scene->mRootNode->mTransformation));
    }

    // --- 2) Parcourt les meshes : fusionne la geometrie ET enregistre un sous-mesh par mesh ---
    for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
        const aiMesh* mesh = scene->mMeshes[m];

        SubMesh sub{};
        sub.indexOffset = static_cast<uint32_t>(indices.size());
        sub.materialIndex = static_cast<int>(mesh->mMaterialIndex);
        if (sub.materialIndex < 0 || sub.materialIndex >= static_cast<int>(materials.size())) {
            sub.materialIndex = 0;   // clamp de securite
        }

        uint32_t vertexOffset = static_cast<uint32_t>(vertices.size());

        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            Vertex vertex{};

            vertex.position = {
                mesh->mVertices[i].x,
                mesh->mVertices[i].y,
                mesh->mVertices[i].z
            };

            if (mesh->HasNormals()) {
                vertex.normal = {
                    mesh->mNormals[i].x,
                    mesh->mNormals[i].y,
                    mesh->mNormals[i].z
                };
            } else {
                vertex.normal = {0.0f, 0.0f, 1.0f};
            }

            if (mesh->HasTangentsAndBitangents()) {
                vertex.tangent = {
                    mesh->mTangents[i].x,
                    mesh->mTangents[i].y,
                    mesh->mTangents[i].z
                };
            } else {
                vertex.tangent = {1.0f, 0.0f, 0.0f};
            }

            if (mesh->mTextureCoords[0]) {
                vertex.texCoord = {
                    mesh->mTextureCoords[0][i].x,
                    mesh->mTextureCoords[0][i].y
                };
                hasUV = true;
            } else {
                vertex.texCoord = {0.0f, 0.0f};
            }

            vertices.push_back(vertex);
        }

        // Indices (faces triangulees par aiProcess_Triangulate).
        for (unsigned int f = 0; f < mesh->mNumFaces; f++) {
            const aiFace& face = mesh->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                indices.push_back(vertexOffset + face.mIndices[j]);
            }
        }

        // Poids de skinning de CE mesh. mVertexId est local au mesh :
        // il faut y ajouter vertexOffset pour viser le tableau global.
        if (anyBones) {
            skins.resize(vertices.size());

            for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
                const aiBone* bone = mesh->mBones[b];
                int bi = skeleton.find(bone->mName.C_Str());
                if (bi < 0) continue;

                skeleton.bones[bi].offsetMatrix = toGlm(bone->mOffsetMatrix);
                skeleton.bones[bi].skinned = true;

                for (unsigned int w = 0; w < bone->mNumWeights; ++w) {
                    uint32_t vid = vertexOffset + bone->mWeights[w].mVertexId;
                    float weight = bone->mWeights[w].mWeight;
                    if (weight <= 0.0f || vid >= skins.size()) continue;
                    addWeight(skins[vid], bi, weight);
                }
            }
        }

        sub.indexCount = static_cast<uint32_t>(indices.size()) - sub.indexOffset;
        submeshes.push_back(sub);
    }

    // --- 3) Compat : les anciens accesseurs renvoient le 1er materiau ---
    diffuseTexturePath = materials[0].diffuseTexturePath;
    normalTexturePath  = materials[0].normalTexturePath;

    if (anyBones) {
        clips = extractClips(scene, skeleton);
        std::cout << "[Model]" << clips.size() << " clips d'aniamtion" << std::endl;
        std::string base = std::filesystem::path(filepath).stem().string();
        for (size_t i = 0; i < clips.size(); ++i) {
            clips[i].name = (clips.size() == 1) ? base : (base + "_" + std::to_string(i));
        }
    }

    // Redressement Y-up -> Z-up : un quart de tour autour de X, (x, y, z) -> (x, -z, y).
    // Le haut du modele (+Y) devient notre haut (+Z). On tourne positions, normales et tangentes ;
    // le squelette garde ses donnees et recoit la meme rotation via axisFix.
    if (yUpToZUp) {
        auto rotate = [](glm::vec3& v) { v = glm::vec3(v.x, -v.z, v.y); };
        for (auto& v : vertices) {
            rotate(v.position);
            rotate(v.normal);
            rotate(v.tangent);
        }
        if (anyBones) {
            skeleton.axisFix = glm::mat4(glm::mat3(1, 0, 0,     // colonnes : ou vont X, Y, Z
                                                   0, 0, 1,
                                                   0, -1, 0));
            skeleton.axisFixInv = glm::transpose(skeleton.axisFix);   // inverse d une rotation
        }
    }

    // Normalisation : la somme des 4 poids doit valoir 1.
    // Les sommets sans aucun poids restent a zero : le shader retombera
    // sur la position brute, ce qui est le comportement voulu.
    if (!skins.empty()) {
        skins.resize(vertices.size());
        for (auto& s : skins) {
            float sum = s.weights.x + s.weights.y + s.weights.z + s.weights.w;
            if (sum > 1e-6f) s.weights /= sum;
        }
        std::cout << "[Model] squelette : " << skeleton.bones.size() << " noeuds, "
                  << skeleton.skinnedCount() << " os lies aux sommets" << std::endl;
    }
}

std::vector<AnimationClip> Model::loadAnimations(const std::string& path, const Skeleton& skeleton) {
    Assimp::Importer importer;
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    // pas de traitement de geometrie on veut que les courbes
    const aiScene* scene = importer.ReadFile(path, 0);
    if (!scene || !scene->mRootNode) {
        throw std::runtime_error("Animation : lecture impossible : " + path);
    }
    auto loaded = extractClips(scene, skeleton);
    std:: string base = std::filesystem::path(path).stem().string();
    for (size_t i = 0; i < loaded.size(); ++i) {
        loaded[i].name = (loaded.size() == 1) ? base : (base + "_" + std::to_string(i));
    }
    return loaded;
}