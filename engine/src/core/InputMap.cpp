#include <engine/core/InputMap.hpp>
#include <engine/core/Localization.hpp>

#include <algorithm>
#include <ostream>
#include <sstream>
#include <cctype>

namespace {
    // En dessous, on considere que le stick est au repos (il ne revient jamais a 0 pile).
    constexpr float kDeadzone = 0.25f;
}

void InputMap::resetDefaults() {
    auto set = [&](int a, const char* id, const char* label,
                   int key, int alt, int padBtn, int padAxis, float sign) {
        bindings[a] = InputBinding{ id, label, key, alt, padBtn, padAxis, sign };
    };

    set(ACTION_MOVE_FORWARD,   "move_forward",   "Move forward",
        GLFW_KEY_W, -1, -1, GLFW_GAMEPAD_AXIS_LEFT_Y, -1.0f);
    set(ACTION_MOVE_BACKWARD,  "move_backward",  "Move backward",
        GLFW_KEY_S, -1, -1, GLFW_GAMEPAD_AXIS_LEFT_Y,  1.0f);
    set(ACTION_MOVE_LEFT,      "move_left",      "Move left",
        GLFW_KEY_A, -1, -1, GLFW_GAMEPAD_AXIS_LEFT_X, -1.0f);
    set(ACTION_MOVE_RIGHT,     "move_right",     "Move right",
        GLFW_KEY_D, -1, -1, GLFW_GAMEPAD_AXIS_LEFT_X,  1.0f);
    set(ACTION_MOVE_UP,        "move_up",        "Move up",
        GLFW_KEY_SPACE, -1, GLFW_GAMEPAD_BUTTON_A, -1, 1.0f);
    set(ACTION_MOVE_DOWN,      "move_down",      "Move down",
        GLFW_KEY_LEFT_SHIFT, -1, GLFW_GAMEPAD_BUTTON_B, -1, 1.0f);
    set(ACTION_SPRINT,         "sprint",         "Sprint",
        GLFW_KEY_LEFT_CONTROL, -1, GLFW_GAMEPAD_BUTTON_LEFT_THUMB, -1, 1.0f);
    set(ACTION_TOGGLE_CURSOR,  "toggle_cursor",  "Toggle cursor",
        GLFW_KEY_TAB, -1, GLFW_GAMEPAD_BUTTON_BACK, -1, 1.0f);
    set(ACTION_DESELECT,       "deselect",       "Deselect",
        GLFW_KEY_ESCAPE, -1, -1, -1, 1.0f);
    set(ACTION_FOCUS_SELECTION,"focus_selection","Focus selection",
        GLFW_KEY_F, GLFW_KEY_KP_DECIMAL, -1, -1, 1.0f);
    set(ACTION_SNAP_MODIFIER,  "snap_modifier",  "Snap modifier",
        GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT, -1, -1, 1.0f);
    set(ACTION_JUMP,           "jump",           "Jump",
        GLFW_KEY_SPACE, -1, GLFW_GAMEPAD_BUTTON_A, -1, 1.0f);
    set(ACTION_INTERACT,       "interact",       "Interact",
        GLFW_KEY_E, -1, GLFW_GAMEPAD_BUTTON_X, -1, 1.0f);
    set(ACTION_ATTACK,         "attack",         "Attack",
        -1, -1, GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER, -1, 1.0f);
}

void InputMap::update(GLFWwindow* window) {
    std::copy(std::begin(values), std::end(values), std::begin(previous));

    // Premiere manette connectee qui expose un mapping standard.
    GLFWgamepadstate pad{};
    padConnected = false;
    padName.clear();
    for (int j = GLFW_JOYSTICK_1; j <= GLFW_JOYSTICK_LAST; ++j) {
        if (glfwJoystickIsGamepad(j) && glfwGetGamepadState(j, &pad)) {
            padConnected = true;
            const char* n = glfwGetGamepadName(j);
            padName = n ? n : "Gamepad";
            break;
        }
    }

    for (int i = 0; i < ACTION_COUNT; ++i) {
        const InputBinding& b = bindings[i];
        float v = 0.0f;

        if (b.key    >= 0 && glfwGetKey(window, b.key)    == GLFW_PRESS) v = 1.0f;
        if (b.altKey >= 0 && glfwGetKey(window, b.altKey) == GLFW_PRESS) v = 1.0f;

        if (padConnected) {
            if (b.padButton >= 0 && pad.buttons[b.padButton] == GLFW_PRESS) v = 1.0f;
            if (b.padAxis >= 0) {
                // Les gachettes reposent a -1, le signe s'en occupe.
                float a = pad.axes[b.padAxis] * b.axisSign;
                if (a > kDeadzone) {
                    v = std::max(v, (a - kDeadzone) / (1.0f - kDeadzone));
                }
            }
        }
        values[i] = v;
    }
}

bool  InputMap::isDown(int action) const { return values[action] > 0.5f; }
float InputMap::value(int action)  const { return values[action]; }

bool InputMap::wasPressed(int action) const {
    return values[action] > 0.5f && previous[action] <= 0.5f;
}

std::string InputMap::keyName(int key) {
    if (key < 0) return "—";
    switch (key) {
        case GLFW_KEY_SPACE:         return Lang::tr("Space");
        case GLFW_KEY_ESCAPE:        return Lang::tr("Escape");
        case GLFW_KEY_ENTER:         return Lang::tr("Enter");
        case GLFW_KEY_TAB:           return Lang::tr("Tab");
        case GLFW_KEY_BACKSPACE:     return Lang::tr("Backspace");
        case GLFW_KEY_DELETE:        return Lang::tr("Delete");
        case GLFW_KEY_LEFT:          return Lang::tr("Left");
        case GLFW_KEY_RIGHT:         return Lang::tr("Right");
        case GLFW_KEY_UP:            return Lang::tr("Up");
        case GLFW_KEY_DOWN:          return Lang::tr("Down");
        case GLFW_KEY_LEFT_SHIFT:    return Lang::tr("Left Shift");
        case GLFW_KEY_RIGHT_SHIFT:   return Lang::tr("Right Shift");
        case GLFW_KEY_LEFT_CONTROL:  return Lang::tr("Left Ctrl");
        case GLFW_KEY_RIGHT_CONTROL: return Lang::tr("Right Ctrl");
        case GLFW_KEY_LEFT_ALT:      return Lang::tr("Left Alt");
        case GLFW_KEY_RIGHT_ALT:     return Lang::tr("Right Alt");
        case GLFW_KEY_KP_DECIMAL:    return Lang::fmt("Numpad %s", ".");
        default: break;
    }
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F25)
        return "F" + std::to_string(key - GLFW_KEY_F1 + 1);
    if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9)
        return Lang::fmt("Numpad %s", std::to_string(key - GLFW_KEY_KP_0).c_str());

    // Touches imprimables : nom dependant de la disposition clavier (AZERTY, etc.).
    const char* n = glfwGetKeyName(key, 0);
    if (n && *n) {
        std::string s(n);
        for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }
    return Lang::fmt("Key %d", key);
}

std::string InputMap::padButtonName(int b) {
    switch (b) {
        case GLFW_GAMEPAD_BUTTON_A:            return Lang::tr("A / Cross");
        case GLFW_GAMEPAD_BUTTON_B:            return Lang::tr("B / Circle");
        case GLFW_GAMEPAD_BUTTON_X:            return Lang::tr("X / Square");
        case GLFW_GAMEPAD_BUTTON_Y:            return Lang::tr("Y / Triangle");
        case GLFW_GAMEPAD_BUTTON_LEFT_BUMPER:  return "LB";
        case GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER: return "RB";
        case GLFW_GAMEPAD_BUTTON_BACK:         return "Select";
        case GLFW_GAMEPAD_BUTTON_START:        return "Start";
        case GLFW_GAMEPAD_BUTTON_GUIDE:        return "Guide";
        case GLFW_GAMEPAD_BUTTON_LEFT_THUMB:   return "L3";
        case GLFW_GAMEPAD_BUTTON_RIGHT_THUMB:  return "R3";
        case GLFW_GAMEPAD_BUTTON_DPAD_UP:      return Lang::tr("D-Pad Up");
        case GLFW_GAMEPAD_BUTTON_DPAD_RIGHT:   return Lang::tr("D-Pad Right");
        case GLFW_GAMEPAD_BUTTON_DPAD_DOWN:    return Lang::tr("D-Pad Down");
        case GLFW_GAMEPAD_BUTTON_DPAD_LEFT:    return Lang::tr("D-Pad Left");
        default: return "—";
    }
}

int InputMap::pollAnyKey(GLFWwindow* window) {
    for (int k = GLFW_KEY_SPACE; k <= GLFW_KEY_LAST; ++k) {
        if (glfwGetKey(window, k) == GLFW_PRESS) return k;
    }
    return -1;
}

int InputMap::pollAnyPadButton() {
    GLFWgamepadstate pad{};
    for (int j = GLFW_JOYSTICK_1; j <= GLFW_JOYSTICK_LAST; ++j) {
        if (!glfwJoystickIsGamepad(j) || !glfwGetGamepadState(j, &pad)) continue;
        for (int b = 0; b <= GLFW_GAMEPAD_BUTTON_LAST; ++b) {
            if (pad.buttons[b] == GLFW_PRESS) return b;
        }
    }
    return -1;
}

void InputMap::writeTo(std::ostream& os) const {
    for (int i = 0; i < ACTION_COUNT; ++i) {
        const InputBinding& b = bindings[i];
        os << "input." << b.id << "=" << b.key << "," << b.altKey << ","
           << b.padButton << "," << b.padAxis << "," << b.axisSign << "\n";
    }
}

bool InputMap::readLine(const std::string& line) {
    const std::string prefix = "input.";
    if (line.rfind(prefix, 0) != 0) return false;

    size_t eq = line.find('=');
    if (eq == std::string::npos) return true;

    std::string id = line.substr(prefix.size(), eq - prefix.size());
    int idx = -1;
    for (int i = 0; i < ACTION_COUNT; ++i) {
        if (id == bindings[i].id) { idx = i; break; }
    }
    if (idx < 0) return true;   // action inconnue (ancien fichier) : ligne ignoree

    InputBinding& b = bindings[idx];
    std::stringstream ss(line.substr(eq + 1));
    std::string tok;
    int field = 0;
    while (std::getline(ss, tok, ',')) {
        try {
            switch (field) {
                case 0: b.key       = std::stoi(tok); break;
                case 1: b.altKey    = std::stoi(tok); break;
                case 2: b.padButton = std::stoi(tok); break;
                case 3: b.padAxis   = std::stoi(tok); break;
                case 4: b.axisSign  = std::stof(tok); break;
                default: break;
            }
        } catch (...) {}
        ++field;
    }
    return true;
}