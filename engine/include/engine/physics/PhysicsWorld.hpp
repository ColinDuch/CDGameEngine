#pragma once 

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <memory>
#include <vector>

/*
monde physique : enveloppe autour de Jolt Physics
aucun en-tete Jolt ici : seul PhysicsWorld.cpp les inclut,
le reste du moteur n a pas a les connaitre (et compile plus vite)
*/

class PhysicsWorld {
    public:
        PhysicsWorld();     // initialise Jolt
        ~PhysicsWorld();    // et le libere

        // pas de copie : Jolt n est initialise qu une fois
        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;

        void beginPlay();       // au play : simulation remise a zero
        void endPlay();     // au stop
        void step(float dt);    // a chaque image penbant le play

        // boite ratachee a un objet de la scene (par son id stable)
        // center et halfExtent : dans le repere de l objet, echelle deja appliquee
        void addBox(uint32_t objectId, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& center, const glm::vec3& halfExtent,
            bool dynamic, float mass = 1.0f);

        // terrain statique : grille de hauteurs resX x et resY (index = iy*resX + ix), centree, Z-up
        void addTerrain(const std::vector<float>& heights, int resX, int resY, float sizeX, float sizeY,
            const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale);

        // position et rotation simulees ; false si l objet n a pas de corps
        bool getTransform(uint32_t objectId, glm::vec3& position, glm::quat& rotation) const;

        void clear();   // retire tous les corps 
    private:
        struct Impl;    // les objets Jolt, chaches dans le .cpp
        std::unique_ptr<Impl> impl;
};