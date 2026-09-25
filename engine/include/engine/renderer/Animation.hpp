#pragma once

#include <engine/renderer/Skeleton.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>

struct VecKey {float time = 0.0f; glm::vec3 value{0.0f}; };
struct QuatKey {float time = 0.0f; glm::quat value{1.0f, 0.0f, 0.0f, 0.0f}; }; 

struct AnimChannel {
    int bone = -1;                      // index des bones dans le squelette 
    std::vector<VecKey> positions;
    std::vector<QuatKey> rotations;
    std::vector<VecKey> scales;
};

/*
un evenement pose sur la timeline 
ce n'est pas l import d un fbx 
c est pour mettre des evenements particulier sur 
une animation par exemple : de 8 a 16 le perso est invincible
*/
struct AnimNotify {
    float time = 0.0f;  //secondes depuis le debut du clip
    std::string name;   // son nom
};

struct AnimationClip {
    std::string name;
    float duration = 0.0f;              // en secondes conveti lors de l'import
    float fps = 30.0f;
    std::vector<AnimChannel> channels;
    std::vector<AnimNotify> notifies;

    /*
    transformation locale a l'instant t, et les os non animes reste ou ils sont
    donc en cas de problème a l'import ou a l'export si une zone du rig n'est pas anime 
    le moteur ne crash pas en tout cas pas pour ça les os inanimes ne bougeront pas 
    */
    std::vector<glm::mat4> sampleLocals(const Skeleton& sk, float t) const;
    Pose samplePose(const Skeleton& sk, float t) const;
};

// melange de deux poses 
Pose blendPoses(const Pose& a, const Pose& b, float w);
