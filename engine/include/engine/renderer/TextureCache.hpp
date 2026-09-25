#pragma once

#include <engine/renderer/Texture.hpp>
#include <engine/renderer/VulkanContext.hpp>

#include <cstddef>
#include <memory>
#include <string>

// Partage les Texture entre sous-meshes et objets.
// La table ne detient que des weak_ptr : la VRAM se libere quand plus
// personne n'utilise la texture, sans intervention.
namespace TextureCache {
    // Texture chargee depuis un fichier. Leve si le fichier est illisible.
    std::shared_ptr<Texture> get(const VulkanContext& ctx, const std::string& filepath);

    // Textures procedurales partagees (damier de diagnostic, normale plate).
    std::shared_ptr<Texture> getChecker(const VulkanContext& ctx);
    std::shared_ptr<Texture> getFlatNormal(const VulkanContext& ctx);

    // Retire les entrees dont plus personne ne se sert. Purement cosmetique.
    void purge();
    std::size_t liveCount();
}