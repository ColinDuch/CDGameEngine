#include <engine/renderer/TextureCache.hpp>
#include <engine/renderer/CheckerTexture.hpp>
#include <engine/renderer/DefaultNormal.hpp>

#include <unordered_map>

namespace {
    // weak_ptr : la table n'empeche jamais la liberation.
    std::unordered_map<std::string, std::weak_ptr<Texture>> entries;

    // Cles reservees aux textures procedurales (un '#' ne peut pas etre un chemin).
    const char* kChecker    = "#checker256";
    const char* kFlatNormal = "#flatnormal";
}

std::shared_ptr<Texture> TextureCache::get(const VulkanContext& ctx,
                                           const std::string& filepath) {
    auto it = entries.find(filepath);
    if (it != entries.end()) {
        if (auto existing = it->second.lock()) return existing;
        entries.erase(it);          // entree morte : on recharge
    }

    auto tex = std::make_shared<Texture>(ctx, filepath);
    entries[filepath] = tex;
    return tex;
}

std::shared_ptr<Texture> TextureCache::getChecker(const VulkanContext& ctx) {
    auto it = entries.find(kChecker);
    if (it != entries.end()) {
        if (auto existing = it->second.lock()) return existing;
        entries.erase(it);
    }

    auto pixels = generateCheckerboard(256, 8);
    auto tex = std::make_shared<Texture>(ctx, pixels.data(), 256, 256);
    entries[kChecker] = tex;
    return tex;
}

std::shared_ptr<Texture> TextureCache::getFlatNormal(const VulkanContext& ctx) {
    auto it = entries.find(kFlatNormal);
    if (it != entries.end()) {
        if (auto existing = it->second.lock()) return existing;
        entries.erase(it);
    }

    auto pixels = generateFlatNormal();
    auto tex = std::make_shared<Texture>(ctx, pixels.data(), 2, 2);
    entries[kFlatNormal] = tex;
    return tex;
}

void TextureCache::purge() {
    for (auto it = entries.begin(); it != entries.end(); ) {
        if (it->second.expired()) it = entries.erase(it);
        else                      ++it;
    }
}

std::size_t TextureCache::liveCount() {
    std::size_t n = 0;
    for (const auto& e : entries) {
        if (!e.second.expired()) ++n;
    }
    return n;
}