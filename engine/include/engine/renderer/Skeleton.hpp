#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstddef>

// Donnees de skinning d'un sommet. Buffer SEPARE (binding 1) : on ne
// veut pas alourdir Vertex, qui sert aussi au terrain.
struct VertexSkin {
    glm::ivec4 boneIds{0, 0, 0, 0};
    glm::vec4  weights{0.0f};
    // Binding 1 : buffer separe, present uniquement sur les meshes skinnes.
    static VkVertexInputBindingDescription bindingDescription() {
        VkVertexInputBindingDescription b{};
        b.binding = 1;
        b.stride = sizeof(VertexSkin);
        b.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return b;
    }

    static std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 2> a{};
        a[0].binding = 1;
        a[0].location = 5;
        a[0].format = VK_FORMAT_R32G32B32A32_SINT;
        a[0].offset = offsetof(VertexSkin, boneIds);
        a[1].binding = 1;
        a[1].location = 6;
        a[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        a[1].offset = offsetof(VertexSkin, weights);
        return a;
    }
};

/*
les fbx portent des espaces blancs dans leurs nom qui varie a l'export 
aka ça peut faire buge une anim en faisant en sorte que seul l'os
de la racine soit présent donc on compare que ce qui suit les ":"
souvent utilise comme norme "mixamorig1: hips"
*/
inline std::string normalizeBoneName(const std::string& n) {
    size_t colonPos = n.rfind(':');
    return (colonPos == std::string::npos) ? n : n.substr(colonPos + 1);
}

struct BoneInfo {
    std::string name;
    int  parent = -1;   // index dans bones, -1 = racine
    glm::mat4 localBind{1.0f};  // transformation locale au repos (aiNode)
    glm::mat4 offsetMatrix{1.0f};   // monde -> espace de l'os (inverse bind pose)
    bool skinned = false;   // true si un aiBone porte ce nom
};

/*
Transformation d un os, decomposee : c est la seule forme
qui se melange
Interpoler des matrices donne du cisaillement 
*/
struct BoneTransform{
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

// une pose qui est egale a la transformation locale de chaque os a un instant donne
using Pose = std::vector<BoneTransform>;

struct Skeleton {
    std::vector<BoneInfo> bones;
    std::unordered_map<std::string, int> nameToIndex;
    glm::mat4 globalInverse{1.0f};

    bool empty() const { return bones.empty(); }

    int find(const std::string& n) const {
        auto it = nameToIndex.find(normalizeBoneName(n));
        return (it == nameToIndex.end()) ? -1 : it->second;
    }

    // Nombre d'os reellement lies a des sommets (les autres sont
    // des noeuds intermediaires de la hierarchie).
    int skinnedCount() const {
        int nbSkinned = 0;
        for (const auto& b : bones) if (b.skinned) ++nbSkinned;
        return nbSkinned;
    }

    // Matrices envoyees au GPU. locals == nullptr -> pose de repos, et alors
    // chaque matrice vaut l'identite : le maillage doit rester IDENTIQUE.
    // On n'applique PAS globalInverse : le loader ignore les transformations
    // de noeuds, les sommets sont deja dans l'espace du mesh.
    std::vector<glm::mat4> skinMatrices(const std::vector<glm::mat4>* locals = nullptr) const {
        std::vector<glm::mat4> globals(bones.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < bones.size(); ++i) {
            const glm::mat4& local = (locals && i < locals->size()) ? (*locals)[i] : bones[i].localBind;
            // Le parent est toujours ajoute avant ses enfants : deja calcule.
            globals[i] = (bones[i].parent >= 0) ? globals[bones[i].parent] * local : local;
        }

        std::vector<glm::mat4> out(bones.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < bones.size(); ++i) {
            if (bones[i].skinned) out[i] = globals[i] * bones[i].offsetMatrix;
        }
        return out;
    }

    // pose de repos point de départ de tout melnage
    Pose bindPose() const {
        Pose p(bones.size());
        for (size_t i = 0; i < bones.size(); ++i) {
            const glm::mat4& varLocal = bones[i].localBind;
            glm::vec3 nbcount0(varLocal[0]), nbcount1(varLocal[1]), nbcount2(varLocal[2]);
            p[i].scale = glm::vec3(glm::length(nbcount0), glm::length(nbcount1), glm::length(nbcount2));
            glm::mat3 rot(nbcount0 / p[i].scale.x, nbcount1 / p[i].scale.y, nbcount2 / p[i].scale.z);
            p[i].rotation = glm::normalize(glm::quat_cast(rot));
            p[i].position = glm::vec3(varLocal[3]);
        }
        return p;
    }

    //meme calcul que SkinMatrices mais a partir d une pose de melange
    std::vector<glm::mat4> skinMatricesFromPose(const Pose& pose) const {
        std::vector<glm::mat4> globals(bones.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < bones.size(); ++i) {
            glm::mat4 local = bones[i].localBind;
            if (i < pose.size()) {
                local = glm::translate(glm::mat4(1.0f), pose[i].position)
                    * glm::mat4_cast(pose[i].rotation)
                    * glm::scale(glm::mat4(1.0f), pose[i].scale);
            }
            globals[i] = (bones[i].parent >= 0) ? globals[bones[i].parent] * local : local;
        }

        std::vector<glm::mat4> out(bones.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < bones.size(); ++i) {
            if (bones[i].skinned) out[i] = globals[i] * bones[i].offsetMatrix;
        }
        return out;
    }
};
