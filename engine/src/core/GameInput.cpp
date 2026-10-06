#include <engine/core/GameInput.hpp>
#include <engine/core/Localization.hpp>

#include <engine/scene/JsonParser.hpp>
#include <engine/scene/JsonWriter.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>

void GameInput::resetDefaults() {
    auto add = [&](const char* name, int key, int padButton, int padAxis, float sign) {
        GameAction a;
        a.name = name;
        a.binding.key = key;
        a.binding.padButton = padButton;
        a.binding.padAxis = padAxis;
        a.binding.axisSign = sign;
        actions.push_back(a);
    };

    // les codes GLFW designent une position physique : W est le Z d un clavier AZERTY
    actions.clear();
    add("move_forward", GLFW_KEY_W, -1, GLFW_GAMEPAD_AXIS_LEFT_Y, -1.0f);
    add("move_backward", GLFW_KEY_S, -1, GLFW_GAMEPAD_AXIS_LEFT_Y, 1.0f);
    add("move_left", GLFW_KEY_A, -1, GLFW_GAMEPAD_AXIS_LEFT_X, -1.0f);
    add("move_right", GLFW_KEY_D, -1, GLFW_GAMEPAD_AXIS_LEFT_X, 1.0f);
    add("jump", GLFW_KEY_SPACE, GLFW_GAMEPAD_BUTTON_A, -1, 1.0f);
    add("sprint", GLFW_KEY_LEFT_SHIFT, GLFW_GAMEPAD_BUTTON_LEFT_THUMB, -1, 1.0f);
    add("interact", GLFW_KEY_E, GLFW_GAMEPAD_BUTTON_X, -1, 1.0f);
    add("attack", GLFW_KEY_F, GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER, -1, 1.0f);
    add("pause", GLFW_KEY_ESCAPE, GLFW_GAMEPAD_BUTTON_START, -1, 1.0f);

    values.assign(actions.size(), 0.0f);
    previous.assign(actions.size(), 0.0f);
}

void GameInput::update(GLFWwindow* window, const GLFWgamepadstate* pad) {
    // l editeur peut ajouter ou retirer des actions : les tableaux suivent
    if (values.size() != actions.size()) {
        values.assign(actions.size(), 0.0f);
        previous.assign(actions.size(), 0.0f);
    }

    previous = values;
    for (size_t i = 0; i < actions.size(); ++i) {
        values[i] = InputMap::evaluate(actions[i].binding, window, pad);
    }
}

int GameInput::find(const std::string& name) const {
    for (size_t i = 0; i < actions.size(); ++i) {
        if (actions[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

void GameInput::save(const std::string& path) const {
    JsonWriter w;
    w.beginObject();
    w.key("version"); w.value(1);

    w.key("actions");
    w.beginArray();
    for (const auto& a : actions) {
        const InputBinding& b = a.binding;
        w.beginObject();
        w.key("name"); w.value(a.name);
        w.key("key"); w.value(b.key);
        w.key("altKey"); w.value(b.altKey);
        w.key("padButton"); w.value(b.padButton);
        w.key("padAxis"); w.value(b.padAxis);
        w.key("axisSign"); w.value(static_cast<double>(b.axisSign));
        w.endObject();
    }
    w.endArray();
    w.endObject();

    // meme ecriture atomique que les machines a etats 
    const std::string tmp = path + ".tmp";
    {
        std::ofstream file(tmp);
        if (!file.is_open()) throw std::runtime_error(Lang::fmt("Cannot write: %s", tmp.c_str()));
        file << w.toString();
        if (!file) throw std::runtime_error(Lang::fmt("Incomplete write: %s", tmp.c_str()));
    }
    std::filesystem::rename(tmp, path);
}

void GameInput::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) throw std::runtime_error(Lang::fmt("Cannot open: %s", path.c_str()));
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    JsonValue root = JsonParser::parse(content);
    if (!root.isObject() || !root.has("actions") || !root["actions"].isArray())
        throw std::runtime_error(Lang::fmt("Invalid file: %s", path.c_str()));

    auto num = [](const JsonValue& o, const char* k, double def) {
        return (o.has(k) && o[k].isNumber()) ?o[k].asDouble() : def;
    };

    std::vector<GameAction> loaded;
    for (const auto& o : root["actions"].asArray()) {
        if (!o.has("name") || !o["name"].isString()) continue;
        GameAction a;
        a.name = o["name"].asString();
        a.binding.key = static_cast<int>(num(o, "key", -1));
        a.binding.altKey = static_cast<int>(num(o, "altKey", -1));
        a.binding.padButton = static_cast<int>(num(o, "padButton", -1));
        a.binding.padAxis = static_cast<int>(num(o, "padAxis", -1));
        a.binding.axisSign = static_cast<float>(num(o, "axisSign", 1.0));
        loaded.push_back(a);
    }

    // on ne remplace qu une fois tout lu : un fichier casse ne vide pas les actions
    actions = std::move(loaded);
    values.assign(actions.size(), 0.0f);
    previous.assign(actions.size(), 0.0f);
}