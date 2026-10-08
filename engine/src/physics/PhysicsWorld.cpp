# include <engine/physics/PhysicsWorld.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <algorithm>
#include <thread>
#include <unordered_map>
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

    // glm <-> Jolt : memes conventions, seul l ordre du quaternion differe
    JPH::Vec3 toJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
    JPH::Quat toJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
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
    std::unordered_map<uint32_t, JPH::BodyID> bodies;   // id d objet -> corps Jolt

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
    clear();
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
    clear();
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

void PhysicsWorld::addBox(uint32_t objectId, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& center, const glm::vec3& halfExtent, bool dynamic) {
    // une boite plate (un plan) n a pas d epaisseur : on lui en donne un minimum
    const glm::vec3 half = glm::max(glm::abs(halfExtent), glm::vec3(0.01f));
    const float radius = std::min(JPH::cDefaultConvexRadius, std::min(half.x, std::min(half.y, half.z)));

    // la boite est centree sur le mesh, pas forcement sur l origine de l objet (ex: pieds du personnage)
    JPH::RotatedTranslatedShapeSettings shape(toJolt(center), JPH::Quat::sIdentity(), new JPH::BoxShapeSettings(toJolt(half), radius));
    JPH::ShapeSettings::ShapeResult result = shape.Create();
    if (result.HasError()) {
        std::cerr << "[physics] forme invalide : " << result.GetError().c_str() << std::endl;
        return;
    }

    JPH::BodyCreationSettings settings(result.Get(), JPH::RVec3(toJolt(position)), toJolt(rotation).Normalized(),
        dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
        dynamic ? Layers::MOVING : Layers::STATIC);
    
    JPH::BodyInterface& bodiesApi = impl->system.GetBodyInterface();
    const JPH::BodyID id = bodiesApi.CreateAndAddBody(settings, dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);

    if (id.IsInvalid()) {
        std::cerr << "[physics] trop de corps" << std::endl;
        return;
    }
    impl->bodies[objectId] = id;
}

bool PhysicsWorld::getTransform(uint32_t objectId, glm::vec3& position, glm::quat& rotation) const {
    const auto it = impl->bodies.find(objectId);
    if (it == impl->bodies.end()) return false;

    JPH::RVec3 p;
    JPH::Quat q;
    impl->system.GetBodyInterface().GetPositionAndRotation(it->second, p, q);
    position = glm::vec3(static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ()));
    rotation = glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
    return true;
}

void PhysicsWorld::clear() {
    JPH::BodyInterface& bodiesApi = impl->system.GetBodyInterface();
    for (const auto& [objectId, id] : impl->bodies) {
        bodiesApi.RemoveBody(id);       // retire de la simulation
        bodiesApi.DestroyBody(id);      // puis libere
    }
    impl->bodies.clear();
}