#include <engine/physics/PhysicsWorld.hpp>

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
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>

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
    constexpr uint32_t TERRAIN_ID = 0xFFFFFFFF;     // cle du terrain : aucun objet n a cet id

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
    std::unordered_map<uint32_t, glm::vec3> pendingForces;      // force demandees pendant l image

    // personnages : ce ne sont pas des corps Jolt, mais des capsules deplacees a la main
    struct Character {
        JPH::Ref<JPH::CharacterVirtual> character;
        glm::vec2 move{ 0.0f };     // vitesse horizontale voulue, en m/s
        float jump = 0.0f;      // saut demande, applique au prochain pas
    };
    std::unordered_map<uint32_t, Character> characters;

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
    const float frame = std::min(dt, 0.25f);    // une image tres longue ne fait pas exploser le calcul

    // les forces de l image deviennent une impulsion (force x duree de l image) :
    // le resultat ne depend ni des fps ni du nombre de pas simules dans l image
    JPH::BodyInterface& bodiesApi = impl->system.GetBodyInterface();
    for (const auto& [objectId, force] : impl->pendingForces) {
        const auto it = impl->bodies.find(objectId);
        if (it != impl->bodies.end()) bodiesApi.AddImpulse(it->second, toJolt(force * frame));
    }
    impl->pendingForces.clear();
    impl->accumulator += frame;
    while (impl->accumulator >= FIXED_STEP) {
        updateCharacters();
        impl->system.Update(FIXED_STEP, 1, &impl->tempAllocator, &impl->jobs);
        impl->accumulator -= FIXED_STEP;
        impl->steps++;
        impl->simulated += FIXED_STEP;
    }
}

void PhysicsWorld::addBox(uint32_t objectId, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& center, 
    const glm::vec3& halfExtent, bool dynamic, float mass) {
    removeBody(objectId);   // un seul corps par objet : l ancien est remplace
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
    
    // la masse choisie ; Jolt en deduit l inertie a partir de la forme
    if (dynamic) {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = std::max(mass, 0.001f);
    }
    
    JPH::BodyInterface& bodiesApi = impl->system.GetBodyInterface();
    const JPH::BodyID id = bodiesApi.CreateAndAddBody(settings, dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);

    if (id.IsInvalid()) {
        std::cerr << "[physics] trop de corps" << std::endl;
        return;
    }
    impl->bodies[objectId] = id;
}

bool PhysicsWorld::getTransform(uint32_t objectId, glm::vec3& position, glm::quat& rotation) const {
    const auto c = impl->characters.find(objectId);
    if (c != impl->characters.end()) {
        const JPH::RVec3 p = c->second.character->GetPosition();
        position = glm::vec3(static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ()));
        // la rotation n est pas touchee : elle reste celle de l objet, ce sont les scripts qui l orientent
        return true;
    }

    const auto it = impl->bodies.find(objectId);
    if (it == impl->bodies.end()) return false;

    JPH::RVec3 p;
    JPH::Quat q;
    impl->system.GetBodyInterface().GetPositionAndRotation(it->second, p, q);
    position = glm::vec3(static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ()));
    rotation = glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
    return true;
}

void PhysicsWorld::removeBody(uint32_t objectId) {
    impl->pendingForces.erase(objectId);
    impl->characters.erase(objectId);   // un personnage n est pas dans "bodies"
    const auto it = impl->bodies.find(objectId);
    if (it == impl->bodies.end()) return;

    JPH::BodyInterface& bodiesApi = impl->system.GetBodyInterface();
    bodiesApi.RemoveBody(it->second);
    bodiesApi.DestroyBody(it->second);
    impl->bodies.erase(it);
}

void PhysicsWorld::clear() {
    JPH::BodyInterface& bodiesApi = impl->system.GetBodyInterface();
    for (const auto& [objectId, id] : impl->bodies) {
        bodiesApi.RemoveBody(id);       // retire de la simulation
        bodiesApi.DestroyBody(id);      // puis libere
    }
    impl->bodies.clear();
    impl->characters.clear();
}

void PhysicsWorld::addTerrain(const std::vector<float>& heights, int resX, int resY, float sizeX, float sizeY,
    const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale) {
    if (resX < 2 || resY < 2 || heights.size() < static_cast<size_t>(resX) * resY) return;
    removeBody(TERRAIN_ID);     // un seul terrain : l ancien est remplace

    // Jolt veut une grille carree de n x n echantillons, n pair : on reechantillonne 
    const int n = (std::max(resX, resY) + 1) & ~1;

    // hauteur du terrain a une position fractionnaire de sa grille (bilineaire)
    auto sample = [&](float fx, float fy) {
        const int x0 = std::min(static_cast<int>(fx), resX - 2);
        const int y0 = std::min(static_cast<int>(fy), resY - 2);
        const float tx = fx - x0, ty = fy - y0;
        const float h00 = heights[y0 * resX + x0], h10 = heights[y0 * resX + x0 + 1];
        const float h01 = heights[(y0 + 1) * resX + x0], h11 = heights[(y0 + 1) * resX + x0 + 1];
        return (h00 * (1 - tx) + h10 * tx) * (1 - ty) + (h01 * (1 - tx) + h11 * tx) * ty;
    };

    /*
    le heightfield de Jolt est en Y-up : hauteur sur Y, grille sur X et Z
    on le tourne de +90 degres autour de X pour le poser dasn notre monde Z-up.
    cette rotation envoie Z de Jolt vers -Y du terrain : les rangees sont donc lues a l envers
    */
    std::vector<float> samples(static_cast<size_t>(n) * n);
    for (int y = 0; y < n; ++y) {
        const  float fy = (resY - 1) * (1.0f - static_cast<float>(y) / (n - 1));    // rangee inversee
        for (int x = 0; x <n; ++x) {
            const float fx = (resX - 1) * static_cast<float>(x) / (n - 1);
            samples[static_cast<size_t>(y) * n + x] = sample(fx, fy);
        }
    }

    // l echelle de l objet terrain est integree a la forme (x, hauteur, y)
    const glm::vec3 s = glm::abs(scale);
    const JPH::Vec3 joltScale(sizeX / (n - 1) * s.x, s.z, sizeY / (n - 1) * s.y);
    const JPH::Vec3 joltOffset(-0.5f * sizeX * s.x, 0.0f, -0.5f * sizeY * s.y);

    JPH::HeightFieldShapeSettings shape(samples.data(), joltOffset, joltScale, static_cast<JPH::uint32>(n));
    JPH::ShapeSettings::ShapeResult result = shape.Create();
    if (result.HasError()) {
        std::cerr << "[physics] terrainInvalide : " << result.GetError().c_str() << std::endl;
        return;
    }

    const glm::quat yUpToZUp = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    JPH::BodyCreationSettings settings(result.Get(), JPH::RVec3(toJolt(position)), 
        toJolt(rotation * yUpToZUp).Normalized(), JPH::EMotionType::Static, Layers::STATIC);

    JPH::BodyInterface& bodiesApi = impl->system.GetBodyInterface();
    const JPH::BodyID id = bodiesApi.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    if (id.IsInvalid()) return;
    impl->bodies[TERRAIN_ID] = id;
}

void PhysicsWorld::addCharacter(uint32_t objectId, const glm::vec3& position, const glm::quat& rotation,
    float radius, float height, const glm::vec3& footOffset) {
    removeBody(objectId);

    // capsule = un cylindre et deux demi-spheres : sa hauteur totale vaut cylindre + 2 rayons
    radius = std::max(radius, 0.05f);
    const float halfCylinder = std::max(0.5f * height - radius, 0.01f);

    // la capsule de Jolt est orientee selon Y : on la redresse selon Z,
    // et on la remote pour que son point le plus bas touche les pieds
    const glm::quat yToZ = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 offset = footOffset + glm::vec3(0.0f, 0.0f, halfCylinder + radius);
    JPH::RotatedTranslatedShapeSettings shape(toJolt(offset), toJolt(yToZ), 
        new JPH::CapsuleShapeSettings(halfCylinder, radius));
    JPH::ShapeSettings::ShapeResult result = shape.Create();
    if (result.HasError()) {
        std::cerr << "[physics] capsule invalide : " << result.GetError().c_str() << std::endl;
        return;
    }

    JPH::CharacterVirtualSettings settings;
    settings.mShape = result.Get();
    settings.mUp = JPH::Vec3::sAxisZ();     // monde en Z-up
    settings.mMaxSlopeAngle = JPH::DegreesToRadians(45.0f); // au dela, il glisse
    // seul le bas de la capsule peut porter : un plafond touche par al tete n est pas un sol
    settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisZ(), -(footOffset.z + radius));
    settings.mMass = 70.0f;     // kg, pour pousser les caisses
    settings.mMaxStrength = 400.0f;     // force de pousse max, en newtons

    // marge de depart : des pieds jusqu a 50cm sous le sol (scult, placement a l oeil)
    // remontent a la surface. Un sol plus haut que ca (un pont, un etage) n est pas touche.
    constexpr float SPAWN_MARGIN = 0.5f;
    glm::vec3 start = position;
    const float feetZ = position.z + footOffset.z;
    const glm::vec3 rayFrom(position.x + footOffset.x, position.y + footOffset.y, feetZ + SPAWN_MARGIN);
    JPH::RRayCast ray(JPH::RVec3(toJolt(rayFrom)), JPH::Vec3(0.0f, 0.0f, -2.0f * SPAWN_MARGIN));    // longeur incluse
    JPH::RayCastResult hit;
    if (impl->system.GetNarrowPhaseQuery().CastRay(ray, hit, impl->system.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
        impl->system.GetDefaultLayerFilter(Layers::MOVING))) {
        const float groundZ = rayFrom.z - 2.0f * SPAWN_MARGIN * hit.mFraction;
        if (groundZ > feetZ) start.z += groundZ - feetZ;   // seulement si les pieds sont sous le sol
    }

    Impl::Character c;
    c.character = new JPH::CharacterVirtual(&settings, JPH::RVec3(toJolt(position)), JPH::Quat::sIdentity(), &impl->system);
    (void)rotation;     // un  personnage reste debout : sa rotation ne regarde que l objet
    impl->characters[objectId] = c;
}

void PhysicsWorld::setCharacterMove(uint32_t objectId, const glm::vec2& velocity) {
    const auto it = impl->characters.find(objectId);
    if (it != impl->characters.end()) it->second.move = velocity;
}

void PhysicsWorld::characterJump(uint32_t objectId, float speed) {
    const auto it = impl->characters.find(objectId);
    if (it != impl->characters.end()) it->second.jump = speed;
}

bool PhysicsWorld::isCharacter(uint32_t objectId) const {
    return impl->characters.count(objectId) > 0;
}

bool PhysicsWorld::isCharacterGrounded(uint32_t objectId) const {
    const auto it = impl->characters.find(objectId);
    return it != impl->characters.end() && it->second.character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}

bool PhysicsWorld::getVelocity(uint32_t objectId, glm::vec3& velocity) const {
    JPH::Vec3 v;
    const auto c = impl->characters.find(objectId);
    if (c != impl->characters.end()) {
        v = c->second.character->GetLinearVelocity();
    } else {
        const auto b = impl->bodies.find(objectId);
        if (b == impl->bodies.end()) return false;
        v = impl->system.GetBodyInterface().GetLinearVelocity(b->second);
    }
    velocity = glm::vec3(v.GetX(), v.GetY(), v.GetZ());
    return true;
}

bool PhysicsWorld::isDynamic(uint32_t objectId) const {
    const auto it = impl->bodies.find(objectId);
    return it != impl->bodies.end() && impl->system.GetBodyInterface().GetMotionType(it->second) == JPH::EMotionType::Dynamic;
}

void PhysicsWorld::addForce(uint32_t objectId, const glm::vec3& force) {
    if (isDynamic(objectId)) impl->pendingForces[objectId] += force;    // plusieurs appels s additionnent 
}

void PhysicsWorld::addImpulse(uint32_t objectId, const glm::vec3& impulse) {
    if (!isDynamic(objectId)) return;
    impl->system.GetBodyInterface().AddImpulse(impl->bodies.at(objectId), toJolt(impulse));     // le reveille aussi
}

void PhysicsWorld::setVelocity(uint32_t objectId, const glm::vec3& velocity) {
    if (!isDynamic(objectId)) return;
    impl->system.GetBodyInterface().SetLinearVelocity(impl->bodies.at(objectId), toJolt(velocity));
}

void PhysicsWorld::updateCharacters() {
    const JPH::Vec3 gravity = impl->system.GetGravity();
    for (auto& [objectId, c] : impl->characters) {
        JPH::CharacterVirtual& ch = *c.character;
        const JPH::CharacterVirtual::EGroundState ground = ch.GetGroundState();
        JPH::Vec3 move(c.move.x, c.move.y, 0.0f);

        // pente trop raide : on retire la part du movement qui monte dedans, il ne peut que glisser
        if (ground == JPH::CharacterVirtual::EGroundState::OnSteepGround) {
            JPH::Vec3 n = ch.GetGroundNormal();
            n.SetZ(0.0f);       // direction de la descente, a plat 
            if (n.LengthSq() > 1e-6f) {
                n = n.Normalized();
                const float into = move.Dot(n);     // negatif = vers le haut de la pente
                if (into < 0.0f) move -= into * n;
            }
        }

        // verticale : au sol, on s y pose (vitesse nulle) sauf si on vient de sauter et qu on monte encore ;
        // en l air ou sur une pente trop raide, la gravite s accumule
        float vz = ch.GetLinearVelocity().GetZ();
        const bool onGround = ground == JPH::CharacterVirtual::EGroundState::OnGround;
        if (onGround && vz <= 0.1f) vz = 0.0f;
        else vz += gravity.GetZ() * FIXED_STEP;

        // saut : seulement depuis un sol praticable, et une seule fois par demande
        if (c.jump > 0.0f && onGround) vz = c.jump;
        c.jump = 0.0f;

        ch.SetLinearVelocity(JPH::Vec3(move.GetX(), move.GetY(), vz));

        // ExtendedUpdate = update + monter les marches + rester colle au sol dans les descentes,
        // Ses reglages par defaut sont en Y-up : on les remet en Z-up.
        JPH::CharacterVirtual::ExtendedUpdateSettings ext;
        ext.mStickToFloorStepDown = JPH::Vec3(0.0f, 0.0f, -0.5f);   // suit le sol jusqu a 50 cm plus bas 
        ext.mWalkStairsStepUp = JPH::Vec3(0.0f, 0.0f, 0.4f);    // monte des marches jusqu a 40cm
        ch.ExtendedUpdate(FIXED_STEP, gravity, ext, 
            impl->system.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
            impl->system.GetDefaultLayerFilter(Layers::MOVING),
            {}, {}, impl->tempAllocator);
    }
}