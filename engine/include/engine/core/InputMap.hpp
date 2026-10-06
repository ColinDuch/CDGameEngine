#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iosfwd>
#include <string>

// Toutes les actions du moteur. L'ORDRE fixe l'ordre d'affichage du panneau.
// Ajouter une action = une entree ici + une ligne dans resetDefaults().
enum InputAction {
    ACTION_MOVE_FORWARD = 0,
    ACTION_MOVE_BACKWARD,
    ACTION_MOVE_LEFT,
    ACTION_MOVE_RIGHT,
    ACTION_MOVE_UP,
    ACTION_MOVE_DOWN,
    ACTION_SPRINT,
    ACTION_TOGGLE_CURSOR,
    ACTION_DESELECT,
    ACTION_FOCUS_SELECTION,
    ACTION_SNAP_MODIFIER,
    ACTION_COUNT
};

struct InputBinding {
    const char* id    = "";     // cle stable pour preferences.txt (ne change JAMAIS)
    const char* label = "";     // texte affiche, passe dans Lang::tr()
    int   key       = -1;       // GLFW_KEY_*        (-1 = non assigne)
    int   altKey    = -1;       // seconde touche
    int   padButton = -1;       // GLFW_GAMEPAD_BUTTON_*
    int   padAxis   = -1;       // GLFW_GAMEPAD_AXIS_*
    float axisSign  = 1.0f;     // sens de l'axe (+1 ou -1)
};

class InputMap {
public:
    InputMap() { resetDefaults(); }

    // A appeler UNE seule fois par frame, avant tout le reste.
    void update(GLFWwindow* window);

    bool  isDown(int action) const;      // seuil binaire
    bool  wasPressed(int action) const;  // front montant (une seule frame)
    float value(int action) const;       // 0..1, analogique si l'axe manette est utilise

    bool gamepadConnected() const { return padConnected; }
    const std::string& gamepadName() const { return padName; }

    // etat de la manette lu cette image, nullptr si aucune
    const GLFWgamepadstate* gamepadState() const { return padConnected ? &pad : nullptr; }

    // valeur 0..1 d une liaison : touches, bouton et axe de manette
    static float evaluate(const InputBinding& b, GLFWwindow* window, const GLFWgamepadstate* pad);

    InputBinding&       binding(int action)       { return bindings[action]; }
    const InputBinding& binding(int action) const { return bindings[action]; }
    void resetDefaults();

    // preferences.txt : une ligne "input.<id>=key,alt,bouton,axe,sens"
    void writeTo(std::ostream& os) const;
    bool readLine(const std::string& line);   // true si la ligne a ete consommee

    // Affichage et capture pour la reassignation.
    static std::string keyName(int key);
    static std::string padButtonName(int button);
    static int pollAnyKey(GLFWwindow* window);   // -1 si rien
    static int pollAnyPadButton();               // -1 si rien

private:
    InputBinding bindings[ACTION_COUNT];
    float values[ACTION_COUNT] = {};
    float previous[ACTION_COUNT] = {};

    bool padConnected = false;
    GLFWgamepadstate pad{};
    std::string padName;
};