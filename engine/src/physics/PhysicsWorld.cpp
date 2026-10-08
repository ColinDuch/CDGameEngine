# include <engine/physics/PhysicsWorld.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <thread>
#include <iostream>

/*
---------------------------------------------------------------------------
Couches : Jolt ne teste que les paires de couches qui peuvent se toucher
deux statiques ne se testent jamais entre eux, c est un gros gain
---------------------------------------------------------------------------
*/

namespace Layers {
    constexpr JPH::ObjectLayer STATIC = 0;      // sol, murs: ne bougent jamais
    constexpr JPH::ObjectLayer MOVING = 1;      // tout ce qui bouge
    constexpr JPH::uint COUNT = 2;
}

namespace {
    // une couche "large" par couche d objet : la cas le plus simple
    class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
        public :
            JPH::uint GetNumBroadPhaseLayers() const override { return Layers::COUNT; }
            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
                return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer));
            }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
            return layer.GetValue() == Layers::STATIC ? "STATIC" : "MOVING";
        }
#endif
    };

    // qui peut toucher qui : seulement si l un des deux bouge
    class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
        public:
            bool ShouldCollide(JPH::ObjectLayer a, JPH::BroadPhaseLayer b) const override {
                return a == Layers::MOVING || b.GetValue() == Layers::MOVING;
            }
    };

    class ObjectVsObject final : public JPH::ObjectLayerPairFilter {
        public:
            bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
                return a == Layers::MOVING || b == Layers::MOVING;
            }
    };

    constexpr float FIXED_STEP = 1.0f / 60.0f;  // la physique avance par pas fixes
}

struct PhysicsWorld::Impl {
    JPH::TempAllocatorImpl tempAllocator{ 10 * 1024 * 1024 };   //10 Mo de memoire de travail
    JPH::JobSystemThreadPool jobs{
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
        static_cast<int>(std::max(1u, std::thread::hardware_concurrency()) - 1)
    };

    BroadPhaseLayers broadPhaseLayers;
    ObjectVsBroadPhase objectVsBroadPhase;
    ObjectVsObject objectVsObject;
    JPH::PhysicsSystem system;

    float accumulator = 0.0f;   // temps pas encore simule
    int steps = 0;      // pas simules depuis le play
    float simulated = 0.0f;     // temps simule depuis le play

    Impl() {
        system.Init(10240, 0, 65536, 10240, broadPhaseLayers, objectVsBroadPhase, objectVsObject);
        system.SetGravity(JPH::Vec3(0.0f, 0.0f, -9.81f));       // monde en Z-up
    }
};

PhysicsWorld::PhysicsWorld() {
    // allocator, fabrique et types : une seule fois, avant tout autre objet Jolt
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    impl = std::make_unique<Impl>();

    std::cout << "[physics] Jolt " << JPH_VERSION_MAJOR << "." << JPH_VERSION_MINOR << "." << JPH_VERSION_PATCH << " pret" << std::endl;
}

PhysicsWorld::~PhysicsWorld() {
    impl.reset();       // les objets Jolt avant de desenregistrer les types

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    std::cout << "[physics] arret" << std::endl;
}

void PhysicsWorld::beginPlay() {
    impl->accumulator = 0.0f;
    impl->steps = 0;
    impl->simulated = 0.0f;
}

void PhysicsWorld::endPlay() {
    std::cout << "[physics] " << impl->steps << " pas simules en " << impl->simulated << " s" << std::endl;
}

void PhysicsWorld::step(float dt) {
    // pas fixes : la simulation ne depend pas des fps, et reste stable
    impl->accumulator += std::min(dt, 0.25f);   // une image tres longue ne fait pas exploser le calcul
    while (impl->accumulator >= FIXED_STEP) {
        impl->system.Update(FIXED_STEP, 1, &impl->tempAllocator, &impl->jobs);
        impl->accumulator -= FIXED_STEP;
        impl->steps++;
        impl->simulated += FIXED_STEP;
    }
}