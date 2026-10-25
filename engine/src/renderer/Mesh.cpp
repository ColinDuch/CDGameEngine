#include <engine/renderer/Mesh.hpp>
#include <engine/renderer/TextureCache.hpp>
#include <engine/renderer/BufferUtils.hpp>

#include <filesystem>

// ---- Buffers geometrie partages (vertex + index), crees une fois. ----
void Mesh::createGeometry(const VulkanContext& ctx,
                          const std::vector<Vertex>& verts,
                          const std::vector<uint32_t>& inds) {
    verticesCPU = verts;
    indicesCPU = inds;

    // Boite englobante locale, pour le frustum culling.
    if (verticesCPU.empty()) {
        boundsMin = boundsMax = glm::vec3(0.0f);
    } else {
        boundsMin = boundsMax = verticesCPU[0].position;
        for (const auto& v : verticesCPU) {
            boundsMin = glm::min(boundsMin, v.position);
            boundsMax = glm::max(boundsMax, v.position);
        }
    }

    vertexBuffer = std::make_shared<VertexBuffer>(ctx, verticesCPU);
    indexBuffer = std::make_shared<IndexBuffer>(ctx, indicesCPU);
}

// ---- Cree un sous-mesh : textures (ou damier) + descriptor set + renderMode. ----
void Mesh::addSubMesh(const VulkanContext& ctx,
                      uint32_t indexOffset, uint32_t indexCount,
                      const std::string& diffusePath, const std::string& normalPath,
                      const glm::vec3& baseColor, bool hasUVSub, bool referencesTexture,
                      float metallic, float roughness,
                      VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout,
                      VkBuffer uniformBuffer) {
    SubMeshRender sub;
    sub.indexOffset = indexOffset;
    sub.indexCount = indexCount;
    sub.baseColor = baseColor;
    sub.metallic = metallic;
    sub.roughness = roughness;
    sub.diffusePath = diffusePath;
    sub.normalPath = normalPath;
    sub.referencesTexture = referencesTexture;

    // Texture diffuse : la vraie, sinon un damier. Partagees via le cache.
    sub.texture = diffusePath.empty() ? TextureCache::getChecker(ctx)
                                      : TextureCache::get(ctx, diffusePath);

    // Normal map : la vraie, sinon une normale plate par defaut.
    sub.normalTexture = normalPath.empty() ? TextureCache::getFlatNormal(ctx)
                                           : TextureCache::get(ctx, normalPath);
    sub.hasNormalMap = !normalPath.empty();

    // Diagnostic + rendu, decide par sous-mesh :
    if (!hasUVSub) {
        sub.renderMode = 2;              // pas d'UV -> damier magenta/noir (priorite absolue)
    } else if (referencesTexture) {
        // Le .mtl reference une texture :
        if (!diffusePath.empty()) {
            sub.renderMode = 0;          // ... fichier present -> texture
        } else {
            sub.renderMode = 1;          // ... fichier manquant -> damier gris/blanc (alarme)
        }
    } else {
        sub.renderMode = 4;              // aucune texture referencee -> couleur Kd du materiau
    }

    sub.descriptorSet = std::make_unique<DescriptorSet>(
        ctx.device, descriptorPool, descriptorSetLayout,
        uniformBuffer,
        sub.texture->getImageView(), sub.texture->getSampler(),
        sub.normalTexture->getImageView(), sub.normalTexture->getSampler()
    );

    subMeshes.push_back(std::move(sub));
}

// ---- Construction commune depuis un MeshCacheData (projet ou cache d'import). ----
void Mesh::buildFromCache(const VulkanContext& ctx,
                          const MeshCacheData& cached,
                          const std::string& cacheFilePath,
                          VkDescriptorPool descriptorPool,
                          VkDescriptorSetLayout descriptorSetLayout,
                          VkBuffer uniformBuffer) {
    hasUV = cached.hasUV;
    createGeometry(ctx, cached.vertices, cached.indices);

    // Chemins relatifs = relatifs au .emesh lui-meme ; absolus = laisses tels quels.
    std::filesystem::path baseDir = std::filesystem::path(cacheFilePath).parent_path();
    auto resolveTex = [&](const std::string& p) -> std::string {
        if (p.empty()) return p;
        std::filesystem::path fp(p);
        std::filesystem::path full = fp.is_absolute()
                                   ? fp : (baseDir / fp).lexically_normal();
        std::error_code ec;
        if (!std::filesystem::exists(full, ec)) return std::string();  // -> damier d'alarme
        return full.string();
    };

    for (const auto& sm : cached.subMeshes) {
        std::string diffuse = !texturePath.empty() ? texturePath : resolveTex(sm.diffusePath);
        bool refTex = !texturePath.empty() || sm.referencesTexture;

        addSubMesh(ctx, sm.indexOffset, sm.indexCount,
                   diffuse, resolveTex(sm.normalPath), sm.baseColor, hasUV, refTex,
                   sm.metallic, sm.roughness,
                   descriptorPool, descriptorSetLayout, uniformBuffer);
    }
}

// ---- Constructeur public "fichier" : .emesh, cache d'import, ou Assimp. ----
Mesh::Mesh(const VulkanContext& ctx,
           const std::string& modelPath, const std::string& texturePath,
           VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout,
           VkBuffer uniformBuffer, bool yUpToZUp)
    : texturePath(texturePath), modelPath(modelPath), yUpToZUp(yUpToZUp) {

    // 1. Geometrie de projet deja cuite : rien a importer.
    if (MeshCache::isCacheFile(modelPath)) {
        // un  .emesh garde la geometrie d origine (voir toCacheData) : on redresse au chargement 
        MeshCacheData data =MeshCache::load(modelPath);
        if (yUpToZUp) {
            for (auto& v : data.vertices) {
                v.position = glm::vec3(v.position.x, -v.position.z, v.position.y);
                v.normal = glm::vec3(v.normal.x, -v.normal.z, v.position.y);
                v.tangent = glm::vec3(v.tangent.x, -v.tangent.z, v.tangent.y);
            }
        }
        buildFromCache(ctx, data, modelPath, descriptorPool, descriptorSetLayout, uniformBuffer);
        return;

    }

    // 2. Cache d'import a jour : on saute completement Assimp.
    const std::string cachePath = MeshCache::cachePathFor(modelPath);
    if (!yUpToZUp && MeshCache::isFresh(cachePath, modelPath)) {    // le cache ne connait pas le redressement 
        MeshCacheData cached;
        bool ok = false;
        try { cached = MeshCache::load(cachePath); ok = true; }
        catch (const std::exception&) { ok = false; }   // cache corrompu -> Assimp

        if (ok) {
            buildFromCache(ctx, cached, cachePath,
                           descriptorPool, descriptorSetLayout, uniformBuffer);
            return;
        }
    }

    // 3. Import Assimp (lent), puis ecriture du cache pour la prochaine fois.
    Model model(modelPath, yUpToZUp);
    hasUV = model.getHasUV();
    skeleton = model.getSkeleton();
    skins = model.getSkins();
    clips = model.getClips();
    createSkinBuffer(ctx);
    createGeometry(ctx, model.getVertices(), model.getIndices());

    const auto& materials = model.getMaterials();

    for (const auto& sm : model.getSubMeshes()) {
        const Material& mat = materials[sm.materialIndex];

        // Texture explicite (param) prioritaire, sinon celle du .mtl de ce materiau.
        std::string diffuse = !texturePath.empty() ? texturePath : mat.diffuseTexturePath;
        bool refTex = !texturePath.empty() || mat.referencesTexture;

        addSubMesh(ctx, sm.indexOffset, sm.indexCount,
                   diffuse, mat.normalTexturePath, mat.baseColor, hasUV, refTex,
                   mat.metallic, mat.roughness,
                   descriptorPool, descriptorSetLayout, uniformBuffer);
    }

    if (skins.empty() && !yUpToZUp) {
            // Le cache est un confort : s'il echoue, on continue sans bruit.
        try {
            MeshCacheData d = toCacheData();
            std::filesystem::path modelDir = std::filesystem::path(modelPath).parent_path();
            auto makeAbs = [&](std::string& p) {
                if (p.empty()) return;
                std::filesystem::path fp(p);
                if (!fp.is_absolute()) p = (modelDir / fp).lexically_normal().string();
            };
            for (auto& sm : d.subMeshes) { makeAbs(sm.diffusePath); makeAbs(sm.normalPath); }
            MeshCache::save(cachePath, d);
        } catch (const std::exception&) {
        }
    }
}

// ---- Constructeur public "memoire" (import glTF) : un seul sous-mesh. ----
Mesh::Mesh(const VulkanContext& ctx,
           const std::vector<Vertex>& verts, const std::vector<uint32_t>& inds,
           const std::string& texturePath,
           std::string modelPath,
           VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout,
           VkBuffer uniformBuffer)
    : texturePath(texturePath), modelPath(std::move(modelPath)) {

    hasUV = true;   // les meshes importes glTF portent des UV (0,0 par defaut au pire)
    createGeometry(ctx, verts, inds);

    addSubMesh(ctx, 0, static_cast<uint32_t>(inds.size()),
               texturePath, "", glm::vec3(1.0f), true, !texturePath.empty(),
               0.0f, 0.5f,
               descriptorPool, descriptorSetLayout, uniformBuffer);
}

// ---- Seuls skinBuffer/skinMemory sont des handles bruts : le reste est en shared_ptr. ----
Mesh::~Mesh() {
    if (device == VK_NULL_HANDLE) return;   // aucun buffer de skinning cree
    if (skinBuffer) { vkDestroyBuffer(device, skinBuffer, nullptr); skinBuffer = VK_NULL_HANDLE; }
    if (skinMemory) { vkFreeMemory(device, skinMemory, nullptr);    skinMemory = VK_NULL_HANDLE; }
}

// ---- Extraction de la geometrie vers le cache binaire. ----
MeshCacheData Mesh::toCacheData() const {
    MeshCacheData d;
    d.vertices = verticesCPU;
    // un modele redresse est enregistre dans son sens d origine : le redressement est 
    // refait au chargement, sinon il serait applique deux fois
    if (yUpToZUp) {
        for (auto& v : d.vertices) {
            v.position = glm::vec3(v.position.x, v.position.z, -v.position.y);
            v.normal = glm::vec3(v.normal.x, v.normal.z, -v.normal.y);
            v.tangent = glm::vec3(v.tangent.x, v.tangent.z, -v.tangent.y);
        }
    }
    d.indices  = indicesCPU;
    d.hasUV    = hasUV;

    for (const auto& s : subMeshes) {
        MeshCacheSubMesh c;
        c.indexOffset = s.indexOffset;
        c.indexCount  = s.indexCount;
        c.baseColor   = s.baseColor;
        c.metallic    = s.metallic;
        c.roughness   = s.roughness;
        c.referencesTexture = s.referencesTexture;
        c.diffusePath = s.diffusePath;
        c.normalPath  = s.normalPath;
        d.subMeshes.push_back(std::move(c));
    }
    return d;
}

void Mesh::createSkinBuffer(const VulkanContext& ctx) {
    if (skins.empty()) return;
    device = ctx.device;
    BufferUtils::createDeviceLocalBuffer(
        ctx.physicalDevice, ctx.device, ctx.commandPool, ctx.graphicsQueue,
        skins.data(), skins.size() * sizeof(VertexSkin),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        skinBuffer, skinMemory);
}