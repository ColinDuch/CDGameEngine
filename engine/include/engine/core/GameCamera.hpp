#pragma once

#include <cstdint>
#include <string>

/*
caméra de jeu : en Play, elle remplace la caméra de l'éditeur.
ses réglages sont enregistrés avec la scène, et les scripts peuvent les changer en jeu .
*/
struct GameCamera {
    enum class Mode { Editor, ThirdPerson, FirstPerson, TopDown };     // nouveaux modes : TOUJOURS à la fin
    Mode mode = Mode::Editor;       // Editor : la caméra de l'éditeur reste en Play
    uint32_t targetId = 0;          // objet suivi, par son id stable
    float distance = 5.0f;          // mètres entre la caméra et le point visé
    float height = 1.6f;            // point visé (ou les yeux), au-dessus de l'origine de la cible
    float pitch = -15.0f;           // degrés ; négatif = regarde vers le bas

    // le mode en toutes lettres : pour scene.json et pour les scripts Lua
    static const char* modeName(Mode m) {
        switch (m) {
            case Mode::ThirdPerson: return "third_person";
            case Mode::FirstPerson: return "first_person";
            case Mode::TopDown: return "top_down";
            default: return "editor";
        }
    }
    // false si le nom est inconnu (le mode n'est alors pas modifié)
    static bool modeFromName(const std::string& n, Mode& out) {
        if (n == "editor") { out = Mode::Editor; return true;}
        if (n == "third_person") { out = Mode::ThirdPerson; return true; }
        if (n == "first_person") { out = Mode::FirstPerson; return true; }
        if (n == "top_down") { out = Mode::TopDown; return true; }
        return false;
    }
};