#include <engine/renderer/Terrain.hpp>
#include <engine/renderer/BufferUtils.hpp>
#include <engine/renderer/CommandUtils.hpp>
#include <engine/renderer/CheckerTexture.hpp>
#include <engine/renderer/DefaultNormal.hpp>

#include <stb_image.h>
#include <cstdint>
#include <glm/glm.hpp>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <stdexcept>

// ---- Bruit de Perlin maison (gradients haches, FBM) ----
namespace {

float noiseHash(int x, int y, int seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)seed * 362437u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xffffffu) / float(0x1000000);   // [0,1)
}

glm::vec2 noiseGradient(int x, int y, int seed) {
    float a = noiseHash(x, y, seed) * 6.2831853f;
    return glm::vec2(std::cos(a), std::sin(a));
}

float perlin(float x, float y, int seed) {
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    int x1 = x0 + 1, y1 = y0 + 1;
    float sx = x - x0, sy = y - y0;

    auto fade = [](float t){ return t * t * t * (t * (t * 6 - 15) + 10); };
    float u = fade(sx), v = fade(sy);

    auto dotGrad = [&](int gx, int gy) {
        glm::vec2 g = noiseGradient(gx, gy, seed);
        return g.x * (x - gx) + g.y * (y - gy);
    };

    float n00 = dotGrad(x0, y0), n10 = dotGrad(x1, y0);
    float n01 = dotGrad(x0, y1), n11 = dotGrad(x1, y1);
    float nx0 = n00 + u * (n10 - n00);
    float nx1 = n01 + u * (n11 - n01);
    return nx0 + v * (nx1 - nx0);   // ~ [-0.7, 0.7]
}

float fbm(float x, float y, const NoiseParams& p) {
    float total = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
    for (int o = 0; o < p.octaves; ++o) {
        total += perlin(x * freq, y * freq, p.seed + o) * amp;
        norm  += amp;
        amp   *= p.persistence;
        freq  *= p.lacunarity;
    }
    return (norm > 0.0f) ? total / norm : 0.0f;
}

} // namespace

void Terrain::generateNoise(const NoiseParams& p) {
    float scale = (p.scale > 0.001f) ? p.scale : 0.001f;

    // Limite les octaves a ce que la grille peut representer (Nyquist) :
    // on n'ajoute pas de detail plus fin que ~2 cellules -> pas d'aliasing,
    // resultat coherent quelle que soit la resolution.
    float cell = std::min(sizeX / (resX - 1), sizeY / (resY - 1));
    NoiseParams q = p;
    int eff = 1;
    for (int o = 0; o < p.octaves; ++o) {
        float wavelength = scale / std::pow(p.lacunarity, (float)o);
        if (wavelength < 2.0f * cell) break;   // trop fin pour cette grille
        eff = o + 1;
    }
    q.octaves = eff;

    for (int iy = 0; iy < resY; ++iy) {
        for (int ix = 0; ix < resX; ++ix) {
            size_t i = static_cast<size_t>(iy) * resX + ix;
            float wx = vertices[i].position.x;
            float wy = vertices[i].position.y;

            float h = fbm(wx / scale, wy / scale, q) * p.amplitude;   // q, pas p

            if (p.add) heightData[i] += h;
            else       heightData[i]  = h;

            vertices[i].position.z = heightData[i];
        }
    }

    recomputeNormals();
    uploadVertices();
}

Terrain::Terrain(const VulkanContext& ctx,
                 VkDescriptorPool pool, VkDescriptorSetLayout layout, VkBuffer uniformBuffer,
                 int resX, int resY, float sizeX, float sizeY)
    : ctx(ctx), pool(pool), layout(layout), uniformBuffer(uniformBuffer) {

    // Damier + normale plate (comme la grille). Le descriptor set reste stable.
    auto checker = generateCheckerboard(256, 8);
    diffuseTex = std::make_unique<Texture>(ctx, checker.data(), 256, 256);
    auto flat = generateFlatNormal();
    normalTex = std::make_unique<Texture>(ctx, flat.data(), 2, 2);

    descriptorSet = std::make_unique<DescriptorSet>(
        ctx.device, pool, layout, uniformBuffer,
        diffuseTex->getImageView(), diffuseTex->getSampler(),
        normalTex->getImageView(), normalTex->getSampler()
    );

    rebuild(resX, resY, sizeX, sizeY);
    createLayerResources();
}

Terrain::~Terrain() {
    destroyGpuBuffers();
    if (layerLayout) vkDestroyDescriptorSetLayout(ctx.device, layerLayout, nullptr);
}

void Terrain::rebuild(int newResX, int newResY, float newSizeX, float newSizeY) {
    resX  = std::max(2, newResX);
    resY  = std::max(2, newResY);
    sizeX = newSizeX;
    sizeY = newSizeY;

    heightData.assign(static_cast<size_t>(resX) * resY, 0.0f);   // terrain plat
    generateGeometry();

    vkDeviceWaitIdle(ctx.device);   // les anciens buffers ne doivent plus servir
    destroyGpuBuffers();
    createGpuBuffers();
}

void Terrain::generateGeometry() {
    vertices.resize(static_cast<size_t>(resX) * resY);

    const float dx = sizeX / static_cast<float>(resX - 1);
    const float dy = sizeY / static_cast<float>(resY - 1);
    const float x0 = -sizeX * 0.5f;
    const float y0 = -sizeY * 0.5f;

    for (int iy = 0; iy < resY; ++iy) {
        for (int ix = 0; ix < resX; ++ix) {
            size_t i = static_cast<size_t>(iy) * resX + ix;
            Vertex v{};
            v.position = glm::vec3(x0 + ix * dx, y0 + iy * dy, heightData[i]);
            v.normal   = glm::vec3(0.0f, 0.0f, 1.0f);
            v.tangent  = glm::vec3(1.0f, 0.0f, 0.0f);
            // UV en unites monde -> carreaux de taille fixe, alignes sur l'origine.
            float tile = (uvTiling > 0.001f) ? uvTiling : 0.001f;
            v.texCoord = glm::vec2(v.position.x / tile, v.position.y / tile);
            vertices[i] = v;
        }
    }

    // 2 triangles par cellule, sens CCW vu de +Z (cull BACK / frontFace CCW).
    indices.clear();
    indices.reserve(static_cast<size_t>(resX - 1) * (resY - 1) * 6);
    for (int iy = 0; iy < resY - 1; ++iy) {
        for (int ix = 0; ix < resX - 1; ++ix) {
            uint32_t v00 = iy * resX + ix;
            uint32_t v10 = iy * resX + (ix + 1);
            uint32_t v01 = (iy + 1) * resX + ix;
            uint32_t v11 = (iy + 1) * resX + (ix + 1);
            indices.push_back(v00); indices.push_back(v10); indices.push_back(v11);
            indices.push_back(v00); indices.push_back(v11); indices.push_back(v01);
        }
    }
    indexCount = static_cast<uint32_t>(indices.size());

    recomputeNormals();   // plat -> +Z partout
}

void Terrain::recomputeNormals() {
    const float dx = sizeX / static_cast<float>(resX - 1);
    const float dy = sizeY / static_cast<float>(resY - 1);

    auto h = [&](int ix, int iy) -> float {
        ix = std::clamp(ix, 0, resX - 1);
        iy = std::clamp(iy, 0, resY - 1);
        return heightData[static_cast<size_t>(iy) * resX + ix];
    };

    for (int iy = 0; iy < resY; ++iy) {
        for (int ix = 0; ix < resX; ++ix) {
            float dzdx = (h(ix + 1, iy) - h(ix - 1, iy)) / (2.0f * dx);
            float dzdy = (h(ix, iy + 1) - h(ix, iy - 1)) / (2.0f * dy);
            glm::vec3 n = glm::normalize(glm::vec3(-dzdx, -dzdy, 1.0f));
            vertices[static_cast<size_t>(iy) * resX + ix].normal = n;
        }
    }
}

void Terrain::uploadVertices() {
    if (!stagingMapped || vertexBuffer == VK_NULL_HANDLE) return;

    VkDeviceSize vSize = vertices.size() * sizeof(Vertex);
    std::memcpy(stagingMapped, vertices.data(), vSize);

    // Le GPU peut encore lire le vertex buffer de l'image precedente :
    // on attend avant d'ecraser son contenu.
    vkDeviceWaitIdle(ctx.device);

    CommandUtils::copyBuffer(ctx.device, ctx.commandPool, ctx.graphicsQueue,
                             stagingBuffer, vertexBuffer, vSize);
}

void Terrain::createGpuBuffers() {
    // Vertex buffer HOST_VISIBLE + mappe en permanence (editable).
    VkDeviceSize vSize = vertices.size() * sizeof(Vertex);

    // Staging : c'est le CPU qui ecrit ici, jamais le GPU qui y lit pour dessiner.
    BufferUtils::createBuffer(
        ctx.physicalDevice, ctx.device, vSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingMemory
    );
    vkMapMemory(ctx.device, stagingMemory, 0, vSize, 0, &stagingMapped);

    // Vertex buffer DEVICE_LOCAL : lu a ~450 Go/s au lieu de ~10 via le PCIe.
    BufferUtils::createBuffer(
        ctx.physicalDevice, ctx.device, vSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        vertexBuffer, vertexMemory
    );

    uploadVertices();

    // Index buffer statique device-local.
    VkDeviceSize iSize = indices.size() * sizeof(uint32_t);
    BufferUtils::createDeviceLocalBuffer(
        ctx.physicalDevice, ctx.device, ctx.commandPool, ctx.graphicsQueue,
        indices.data(), iSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        indexBuffer, indexMemory
    );

    buildShadowIndices();
    createShadowIndexBuffer();
}

// ---- Grille allegee pour la carte d'ombre : un sommet sur `shadowStride`. ----
void Terrain::buildShadowIndices() {
    shadowIndices.clear();
    shadowIndexCount = 0;
    if (resX < 2 || resY < 2) return;

    const int s = std::max(1, shadowStride);

    // On inclut TOUJOURS la derniere colonne et la derniere ligne,
    // sinon le bord du terrain disparaitrait de l'ombre.
    std::vector<int> cols, rows;
    for (int x = 0; x < resX - 1; x += s) cols.push_back(x);
    cols.push_back(resX - 1);
    for (int y = 0; y < resY - 1; y += s) rows.push_back(y);
    rows.push_back(resY - 1);

    shadowIndices.reserve((cols.size() - 1) * (rows.size() - 1) * 6);

    for (size_t j = 0; j + 1 < rows.size(); ++j) {
        for (size_t i = 0; i + 1 < cols.size(); ++i) {
            uint32_t v00 = static_cast<uint32_t>(rows[j]     * resX + cols[i]);
            uint32_t v10 = static_cast<uint32_t>(rows[j]     * resX + cols[i + 1]);
            uint32_t v01 = static_cast<uint32_t>(rows[j + 1] * resX + cols[i]);
            uint32_t v11 = static_cast<uint32_t>(rows[j + 1] * resX + cols[i + 1]);

            // Meme sens que la grille principale (CCW vu de +Z).
            shadowIndices.push_back(v00); shadowIndices.push_back(v10); shadowIndices.push_back(v11);
            shadowIndices.push_back(v00); shadowIndices.push_back(v11); shadowIndices.push_back(v01);
        }
    }
    shadowIndexCount = static_cast<uint32_t>(shadowIndices.size());
}

void Terrain::createShadowIndexBuffer() {
    if (shadowIndices.empty()) return;
    BufferUtils::createDeviceLocalBuffer(
        ctx.physicalDevice, ctx.device, ctx.commandPool, ctx.graphicsQueue,
        shadowIndices.data(), shadowIndices.size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        shadowIndexBuffer, shadowIndexMemory
    );
}

void Terrain::destroyShadowIndexBuffer() {
    if (shadowIndexBuffer) { vkDestroyBuffer(ctx.device, shadowIndexBuffer, nullptr); shadowIndexBuffer = VK_NULL_HANDLE; }
    if (shadowIndexMemory) { vkFreeMemory(ctx.device, shadowIndexMemory, nullptr);    shadowIndexMemory = VK_NULL_HANDLE; }
    shadowIndexCount = 0;
}

void Terrain::setShadowStride(int stride) {
    int s = std::max(1, std::min(stride, 16));
    if (s == shadowStride && shadowIndexBuffer != VK_NULL_HANDLE) return;

    shadowStride = s;
    vkDeviceWaitIdle(ctx.device);
    destroyShadowIndexBuffer();
    buildShadowIndices();
    createShadowIndexBuffer();
}

void Terrain::destroyGpuBuffers() {
    destroyShadowIndexBuffer();
    if (stagingMapped) { vkUnmapMemory(ctx.device, stagingMemory); stagingMapped = nullptr; }
    if (stagingBuffer) { vkDestroyBuffer(ctx.device, stagingBuffer, nullptr); stagingBuffer = VK_NULL_HANDLE; }
    if (stagingMemory) { vkFreeMemory(ctx.device, stagingMemory, nullptr);    stagingMemory = VK_NULL_HANDLE; }
    if (vertexBuffer) { vkDestroyBuffer(ctx.device, vertexBuffer, nullptr); vertexBuffer = VK_NULL_HANDLE; }
    if (vertexMemory) { vkFreeMemory(ctx.device, vertexMemory, nullptr);   vertexMemory = VK_NULL_HANDLE; }
    if (indexBuffer)  { vkDestroyBuffer(ctx.device, indexBuffer, nullptr);  indexBuffer = VK_NULL_HANDLE; }
    if (indexMemory)  { vkFreeMemory(ctx.device, indexMemory, nullptr);     indexMemory = VK_NULL_HANDLE; }
}

void Terrain::setUvTiling(float worldUnitsPerTile) {
    uvTiling = std::max(0.001f, worldUnitsPerTile);
    for (auto& v : vertices) {
        v.texCoord = glm::vec2(v.position.x / uvTiling, v.position.y / uvTiling);
    }
    uploadVertices();
}

void Terrain::setTexture(const std::string& path) {
    vkDeviceWaitIdle(ctx.device);
    texturePath = path;                              // <-- ajoute cette ligne
    diffuseTex = std::make_unique<Texture>(ctx, path);

    // On recree le descriptor set avec la nouvelle texture diffuse.
    descriptorSet = std::make_unique<DescriptorSet>(
        ctx.device, pool, layout, uniformBuffer,
        diffuseTex->getImageView(), diffuseTex->getSampler(),
        normalTex->getImageView(), normalTex->getSampler()
    );

    hasTexture = true;
}

float Terrain::heightAtWorld(float wx, float wy) const {
    if (resX < 2 || resY < 2) return 0.0f;

    float fx = (wx + sizeX * 0.5f) / sizeX * (resX - 1);
    float fy = (wy + sizeY * 0.5f) / sizeY * (resY - 1);

    if (!(fx >= 0.0f && fx <= resX - 1)) return 0.0f;   // attrape NaN + hors-terrain
    if (!(fy >= 0.0f && fy <= resY - 1)) return 0.0f;

    int ix = std::clamp(static_cast<int>(std::floor(fx)), 0, resX - 1);
    int iy = std::clamp(static_cast<int>(std::floor(fy)), 0, resY - 1);
    int ix1 = std::min(ix + 1, resX - 1);
    int iy1 = std::min(iy + 1, resY - 1);
    float tx = fx - ix, ty = fy - iy;

    float h00 = heightData[static_cast<size_t>(iy)  * resX + ix];
    float h10 = heightData[static_cast<size_t>(iy)  * resX + ix1];
    float h01 = heightData[static_cast<size_t>(iy1) * resX + ix];
    float h11 = heightData[static_cast<size_t>(iy1) * resX + ix1];

    float h0 = h00 * (1 - tx) + h10 * tx;
    float h1 = h01 * (1 - tx) + h11 * tx;
    return h0 * (1 - ty) + h1 * ty;
}

bool Terrain::raycast(const glm::vec3& ro, const glm::vec3& rd, glm::vec3& outHit) const {
    auto inside = [&](const glm::vec3& p) {
        return p.x >= -sizeX * 0.5f && p.x <= sizeX * 0.5f &&
               p.y >= -sizeY * 0.5f && p.y <= sizeY * 0.5f;
    };

    float cell = std::min(sizeX / (resX - 1), sizeY / (resY - 1));
    float dt = std::max(cell, 0.25f);
    const int maxSteps = 4000;

    float t = 0.0f;
    float prevDiff = ro.z - heightAtWorld(ro.x, ro.y);

    for (int i = 0; i < maxSteps; ++i) {
        t += dt;
        glm::vec3 varPoint = ro + rd * t;
        float diff = varPoint.z - heightAtWorld(varPoint.x, varPoint.y);

        if (prevDiff > 0.0f && diff <= 0.0f && inside(varPoint)) {
            float a = t - dt, b = t;
            for (int k = 0; k < 24; ++k) {
                float m = 0.5f * (a + b);
                glm::vec3 pm = ro + rd * m;
                float d = pm.z - heightAtWorld(pm.x, pm.y);
                if (d > 0.0f) a = m; else b = m;
            }
            outHit = ro + rd * (0.5f * (a + b));
            return true;
        }
        prevDiff = diff;
    }
    return false;
}

void Terrain::sculpt(const glm::vec3& center, float radius, float amount) {
    if (radius <= 0.0f) return;

    auto gx = [&](float wx){ return (wx + sizeX * 0.5f) / sizeX * (resX - 1); };
    auto gy = [&](float wy){ return (wy + sizeY * 0.5f) / sizeY * (resY - 1); };

    int minIx = std::clamp(static_cast<int>(std::floor(gx(center.x - radius))), 0, resX - 1);
    int maxIx = std::clamp(static_cast<int>(std::ceil (gx(center.x + radius))), 0, resX - 1);
    int minIy = std::clamp(static_cast<int>(std::floor(gy(center.y - radius))), 0, resY - 1);
    int maxIy = std::clamp(static_cast<int>(std::ceil (gy(center.y + radius))), 0, resY - 1);

    float r2 = radius * radius;

    for (int iy = minIy; iy <= maxIy; ++iy) {
        for (int ix = minIx; ix <= maxIx; ++ix) {
            size_t i = static_cast<size_t>(iy) * resX + ix;
            float wx = vertices[i].position.x;
            float wy = vertices[i].position.y;
            float d2 = (wx - center.x) * (wx - center.x) + (wy - center.y) * (wy - center.y);
            if (d2 > r2) continue;

            float t = std::sqrt(d2) / radius;   // 0 au centre, 1 au bord
            float varFallOf = 1.0f - t * t;     // dome
            varFallOf = varFallOf * varFallOf; // bords plus doux

            heightData[i] += amount * varFallOf;
            vertices[i].position.z = heightData[i];
        }
    }

    recomputeNormals();
    uploadVertices();
}

void Terrain::smooth(const glm::vec3& center, float radius, float strength) {
    if (radius <= 0.0f) return;

    auto gx = [&](float wx){ return (wx + sizeX * 0.5f) / sizeX * (resX - 1); };
    auto gy = [&](float wy){ return (wy + sizeY * 0.5f) / sizeY * (resY - 1); };

    int minIx = std::clamp(static_cast<int>(std::floor(gx(center.x - radius))), 0, resX - 1);
    int maxIx = std::clamp(static_cast<int>(std::ceil (gx(center.x + radius))), 0, resX - 1);
    int minIy = std::clamp(static_cast<int>(std::floor(gy(center.y - radius))), 0, resY - 1);
    int maxIy = std::clamp(static_cast<int>(std::ceil (gy(center.y + radius))), 0, resY - 1);

    float r2 = radius * radius;

    // Copie en lecture seule : on moyenne sur l'etat AVANT edition (pas de biais).
    std::vector<float> src = heightData;
    auto H = [&](int ix, int iy) -> float {
        ix = std::clamp(ix, 0, resX - 1);
        iy = std::clamp(iy, 0, resY - 1);
        return src[static_cast<size_t>(iy) * resX + ix];
    };

    for (int iy = minIy; iy <= maxIy; ++iy) {
        for (int ix = minIx; ix <= maxIx; ++ix) {
            size_t i = static_cast<size_t>(iy) * resX + ix;
            float wx = vertices[i].position.x;
            float wy = vertices[i].position.y;
            float d2 = (wx - center.x) * (wx - center.x) + (wy - center.y) * (wy - center.y);
            if (d2 > r2) continue;

            float t = std::sqrt(d2) / radius;
            float varFallOf = 1.0f - t * t; varFallOf = varFallOf * varFallOf;

            float avg = (H(ix-1,iy) + H(ix+1,iy) + H(ix,iy-1) + H(ix,iy+1)
                       + H(ix-1,iy-1) + H(ix+1,iy-1) + H(ix-1,iy+1) + H(ix+1,iy+1)
                       + H(ix,iy)) / 9.0f;

            float k = std::clamp(varFallOf * strength, 0.0f, 1.0f);
            heightData[i] += (avg - heightData[i]) * k;
            vertices[i].position.z = heightData[i];
        }
    }

    recomputeNormals();
    uploadVertices();
}

void Terrain::flatten(const glm::vec3& center, float radius, float strength, float target) {
    if (radius <= 0.0f) return;

    auto gx = [&](float wx){ return (wx + sizeX * 0.5f) / sizeX * (resX - 1); };
    auto gy = [&](float wy){ return (wy + sizeY * 0.5f) / sizeY * (resY - 1); };

    int minIx = std::clamp(static_cast<int>(std::floor(gx(center.x - radius))), 0, resX - 1);
    int maxIx = std::clamp(static_cast<int>(std::ceil (gx(center.x + radius))), 0, resX - 1);
    int minIy = std::clamp(static_cast<int>(std::floor(gy(center.y - radius))), 0, resY - 1);
    int maxIy = std::clamp(static_cast<int>(std::ceil (gy(center.y + radius))), 0, resY - 1);

    float r2 = radius * radius;

    for (int iy = minIy; iy <= maxIy; ++iy) {
        for (int ix = minIx; ix <= maxIx; ++ix) {
            size_t i = static_cast<size_t>(iy) * resX + ix;
            float wx = vertices[i].position.x;
            float wy = vertices[i].position.y;
            float d2 = (wx - center.x) * (wx - center.x) + (wy - center.y) * (wy - center.y);
            if (d2 > r2) continue;

            float t = std::sqrt(d2) / radius;
            float varFallOf = 1.0f - t * t; varFallOf = varFallOf * varFallOf;

            float k = std::clamp(varFallOf * strength, 0.0f, 1.0f);
            heightData[i] += (target - heightData[i]) * k;
            vertices[i].position.z = heightData[i];
        }
    }

    recomputeNormals();
    uploadVertices();
}

glm::mat4 Terrain::getModelMatrix() const {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
    m = m * glm::mat4_cast(orientation);
    m = glm::scale(m, scale);
    return m;
}

glm::vec3 Terrain::getEulerDegrees() const {
    return glm::degrees(glm::eulerAngles(orientation));
}

void Terrain::setEulerDegrees(const glm::vec3& deg) {
    orientation = glm::quat(glm::radians(deg));
}

void Terrain::saveHeightmap(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Impossible d'ecrire le relief : " + path);
    f.write(reinterpret_cast<const char*>(heightData.data()),
            static_cast<std::streamsize>(heightData.size() * sizeof(float)));
}

void Terrain::loadHeightmap(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Impossible de lire le relief : " + path);
    f.read(reinterpret_cast<char*>(heightData.data()),
           static_cast<std::streamsize>(heightData.size() * sizeof(float)));

    for (size_t i = 0; i < heightData.size() && i < vertices.size(); ++i)
        vertices[i].position.z = heightData[i];

    recomputeNormals();
    uploadVertices();
}

void Terrain::setHeights(const std::vector<float>& h) {
    if (h.size() != heightData.size()) return;

    heightData = h;
    // Resynchronise la geometrie : sans ca, les donnees changent mais pas l'affichage.
    for (size_t i = 0; i < heightData.size() && i < vertices.size(); ++i) {
        vertices[i].position.z = heightData[i];
    }

    markHeightsChanged();
    recomputeNormals();
    uploadVertices();
}

bool Terrain::loadHeightmapImage(const std::string& path, float minHeight, float maxHeight,
                                 std::string& outError) {
    int w = 0, h = 0, channels = 0;

    // On lit en 16 bits si l'image l'est (les heightmaps 8 bits font des marches).
    std::vector<float> img;
    if (stbi_is_16_bit(path.c_str())) {
        stbi_us* data = stbi_load_16(path.c_str(), &w, &h, &channels, 1);
        if (!data) { outError = stbi_failure_reason(); return false; }
        img.resize(static_cast<size_t>(w) * h);
        for (size_t i = 0; i < img.size(); ++i) img[i] = data[i] / 65535.0f;
        stbi_image_free(data);
    } else {
        stbi_uc* data = stbi_load(path.c_str(), &w, &h, &channels, 1);
        if (!data) { outError = stbi_failure_reason(); return false; }
        img.resize(static_cast<size_t>(w) * h);
        for (size_t i = 0; i < img.size(); ++i) img[i] = data[i] / 255.0f;
        stbi_image_free(data);
    }

    if (w < 2 || h < 2) { outError = "image trop petite"; return false; }

    // Echantillonnage bilineaire de l'image vers la grille du terrain.
    auto sample = [&](float u, float v) -> float {
        float fx = std::clamp(u, 0.0f, 1.0f) * (w - 1);
        float fy = std::clamp(v, 0.0f, 1.0f) * (h - 1);
        int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
        int x1 = std::min(x0 + 1, w - 1),          y1 = std::min(y0 + 1, h - 1);
        float tx = fx - x0, ty = fy - y0;

        float p00 = img[static_cast<size_t>(y0) * w + x0];
        float p10 = img[static_cast<size_t>(y0) * w + x1];
        float p01 = img[static_cast<size_t>(y1) * w + x0];
        float p11 = img[static_cast<size_t>(y1) * w + x1];
        return (p00 * (1 - tx) + p10 * tx) * (1 - ty)
             + (p01 * (1 - tx) + p11 * tx) * ty;
    };

    std::vector<float> newHeights(static_cast<size_t>(resX) * resY);
    for (int iy = 0; iy < resY; ++iy) {
        for (int ix = 0; ix < resX; ++ix) {
            float u = static_cast<float>(ix) / (resX - 1);
            float v = 1.0f - static_cast<float>(iy) / (resY - 1);   // image : origine en haut
            newHeights[static_cast<size_t>(iy) * resX + ix] =
                minHeight + sample(u, v) * (maxHeight - minHeight);
        }
    }

    setHeights(newHeights);   // resynchronise geometrie + normales + GPU
    return true;
}

void Terrain::createLayerResources() {
    VkDescriptorSetLayoutBinding b[5]{};
    b[0].binding = 0;
    b[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b[0].descriptorCount = 1;
    b[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    for (int i = 1; i <= 4; ++i) {
        b[i].binding = i;
        b[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 5;
    info.pBindings = b;
    if (vkCreateDescriptorSetLayout(ctx.device, &info, nullptr, &layerLayout) != VK_SUCCESS)
        throw std::runtime_error("Terrain : echec du layout des couches");

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = pool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &layerLayout;
    if (vkAllocateDescriptorSets(ctx.device, &alloc, &layerSet) != VK_SUCCESS)
        throw std::runtime_error("Terrain : echec du set des couches");

    // Damier par defaut sur les 4 couches.
    auto checker = generateCheckerboard(256, 8);
    for (int i = 0; i < 4; ++i)
        layers[i] = std::make_unique<Texture>(ctx, checker.data(), 256, 256);

    writeLayerSet();
}

void Terrain::writeLayerSet() {
    VkDescriptorBufferInfo buf{};
    buf.buffer = uniformBuffer;
    buf.offset = 0;
    buf.range  = VK_WHOLE_SIZE;

    VkDescriptorImageInfo imgs[4]{};
    VkWriteDescriptorSet w[5]{};

    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = layerSet;
    w[0].dstBinding = 0;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].descriptorCount = 1;
    w[0].pBufferInfo = &buf;

    for (int i = 0; i < 4; ++i) {
        imgs[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgs[i].imageView = layers[i]->getImageView();
        imgs[i].sampler   = layers[i]->getSampler();

        w[i + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i + 1].dstSet = layerSet;
        w[i + 1].dstBinding = i + 1;
        w[i + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[i + 1].descriptorCount = 1;
        w[i + 1].pImageInfo = &imgs[i];
    }

    vkUpdateDescriptorSets(ctx.device, 5, w, 0, nullptr);
}

void Terrain::setLayerTexture(int index, const std::string& path) {
    if (index < 0 || index > 3) return;
    vkDeviceWaitIdle(ctx.device);
    layers[index] = std::make_unique<Texture>(ctx, path);
    layerPaths[index] = path;
    writeLayerSet();     // meme set : le handle ne change pas
}