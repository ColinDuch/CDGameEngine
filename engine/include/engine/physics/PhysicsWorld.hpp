# pragma once 

#include <memory>

/*
monde physique : enveloppe autour de Jolt Physics
aucun en-tet Jolt ici : seul PhysicsWorld.cpp les inclut,
le reste du moteur n a pas a les connaitre (et compile plus vite)
*/

class PhysicsWorld {
    public:
        PhysicsWorld();     // initialise Jolt
        ~ PhysicsWorld();   // et le libere

        // pas de copie : Jolt n est initialise qu une fois
        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;

        void beginPlay();       // au play : simulation remise a zero
        void endPlay();     // au stop
        void step(float dt);    // a chaque image pendant le play
    
    private:
        struct Impl;    // les objets Jolt, chaches dans le .cpp
        std::unique_ptr<Impl> impl;
};