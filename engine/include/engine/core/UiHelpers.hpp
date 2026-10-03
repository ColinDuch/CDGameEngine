#pragma once

#include <engine/core/Localization.hpp>

#include <string>

// Fenetres et popups : le TEXTE suit la langue, l'ID (apres ###) reste fixe.
// Sans ca, changer de langue casserait l'agencement docke (ImGui identifie par le nom).
inline std::string trWin(const char* key, const char* id) {
    return std::string(Lang::tr(key)) + "###" + id;
}