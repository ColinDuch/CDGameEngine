#include <engine/core/GameInput.hpp>
#include <engine/core/Localization.hpp>

#include <engine/scene/JsonParser.hpp>
#include <engine/scene/JsonWriter.hpp>

#include <filesystem>
#include <algorithm>
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
    sequences.clear();      // un projet sans input.json part de zero
    resizeState();
}

namespace {
    bool keyHeld(GLFWwindow* w, int k) { return k >= 0 && glfwGetKey(w, k) == GLFW_PRESS; }

    bool hasPadModifier(const InputBinding& b) { return b.padModButton >= 0 || b.padModAxis >= 0; }

    // les deux liaisons utilisent-elles une meme touche ?
    bool shareKey(const InputBinding& a, const InputBinding& b) {
        for (int ka : { a.key, a.altKey }) {
            if (ka >= 0 && (ka == b.key || ka == b.altKey)) return true;
        }
        return false;
    }

    // ... un meme bouton, ou un meme sens d un meme axe ?
    bool sharePad(const InputBinding& a, const InputBinding& b) {
        if (a.padButton >= 0 && a.padButton == b.padButton) return true;
        return a.padAxis >= 0 && a.padAxis == b.padAxis && a.axisSign == b.axisSign;
    }
}

void GameInput::update(GLFWwindow* window, const GLFWgamepadstate* pad) {
    const size_t n = actions.size();

    // l editeur peut ajouter ou retirer des actions : les tableaux suivent
    if (values.size() != n) {
        values.assign(n, 0.0f);
        previous.assign(n, 0.0f);
    }
    previous = values;
    resizeState();

    // 1. clavier et manette separes, chacun avec son modificateur
    std::vector<float> keyPart(n, 0.0f), padPart(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        const InputBinding& b = actions[i].binding;

        const bool keyModOk = b.keyModifier < 0 || keyHeld(window, b.keyModifier);
        if (keyModOk && (keyHeld(window, b.key) || keyHeld(window, b.altKey))) keyPart[i] = 1.0f;

        const bool padModOk = !hasPadModifier(b)
                           || InputMap::padValue(pad, b.padModButton, b.padModAxis, b.padModSign) > 0.5f;
        if (padModOk) padPart[i] = InputMap::padValue(pad, b.padButton, b.padAxis, b.axisSign);
    }

    // 2. une combinaison active masque l action simple qui partage son entree :
    //    RT + Y declenche "special_attack" mais pas aussi "special" (Y seul)
    for (size_t s = 0; s < n; ++s) {
        const InputBinding& simple = actions[s].binding;
        for (size_t c = 0; c < n; ++c) {
            if (c == s) continue;
            const InputBinding& combo = actions[c].binding;

            if (keyPart[s] > 0.0f && keyPart[c] > 0.0f && simple.keyModifier < 0
                && combo.keyModifier >= 0 && shareKey(simple, combo))
                keyPart[s] = 0.0f;

            if (padPart[s] > 0.0f && padPart[c] > 0.0f && !hasPadModifier(simple)
                && hasPadModifier(combo) && sharePad(simple, combo))
                padPart[s] = 0.0f;
        }
    }

    for (size_t i = 0; i < n; ++i) values[i] = std::max(keyPart[i], padPart[i]);

    // 3. sequences, avec le temps reel ecoule depuis l image precedente
    const double now = glfwGetTime();
    const float dt = lastTime < 0.0 ? 0.0f : static_cast<float>(now - lastTime);
    lastTime = now;
    updateSequences(dt);
}

int GameInput::find(const std::string& name) const {
    for (size_t i = 0; i < actions.size(); ++i) {
        if (actions[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

void GameInput::resizeState() {
    values.assign(actions.size(), 0.0f);
    previous.assign(actions.size(), 0.0f);
    consumed.assign(actions.size(), 0);
    seqState.assign(sequences.size(), SequenceState{});
}

int GameInput::findSequence(const std::string& name) const {
    for (size_t i = 0; i < sequences.size(); ++i) {
        if (sequences[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

void GameInput::renameAction(const std::string& from, const std::string& to) {
    if (from.empty() || from == to) return;
    for (auto& sq : sequences) {
        for (auto& step : sq.steps) {
            for (auto& name : step) {
                if (name == from) name = to;
            }
        }
    }
}

// une etape est validee quand toutes ses actions sont tenues
// et qu au moins l une d elles vient d etre appuyee : bas tenu + avant appuye = diagonale
bool GameInput::stepMatched(const std::vector<std::string>& step) const {
    if (step.empty()) return false;
    bool fresh = false;
    for (const auto& name : step) {
        const int a = find(name);
        if (a < 0 || !isDown(a)) return false;
        if (pressedRaw(a)) fresh = true;
    }
    return fresh;
}

void GameInput::updateSequences(float dt) {
    if (seqState.size() != sequences.size()) seqState.assign(sequences.size(), SequenceState{});

    for (size_t i = 0; i < sequences.size(); ++i) {
        const GameSequence& sq = sequences[i];
        SequenceState& st = seqState[i];
        st.done = false;
        if (sq.steps.empty()) continue;

        // trop lent : on repart du debut
        if (st.progress > 0) {
            st.timer += dt;
            if (st.timer > sq.window) st.progress = 0;
        }

        if (stepMatched(sq.steps[st.progress])) {
            ++st.progress;
            st.timer = 0.0f;
        } else if (st.progress > 0 && stepMatched(sq.steps[0])) {
            // la premiere etape refaite : nouveau depart plutot qu un echec
            st.progress = 1;
            st.timer = 0.0f;
        }

        if (st.progress >= static_cast<int>(sq.steps.size())) {
            st.done = true;
            st.progress = 0;
            for (const auto& name : sq.steps.back()) {
                const int a = find(name);
                if (a >= 0) consumed[a] = 1;
            }
        }
    }
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
        w.key("keyModifier");  w.value(b.keyModifier);
        w.key("padModButton"); w.value(b.padModButton);
        w.key("padModAxis");   w.value(b.padModAxis);
        w.key("padModSign");   w.value(static_cast<double>(b.padModSign));
        w.endObject();
    }
    w.endArray();

    w.key("sequences");
    w.beginArray();
    for (const auto& sq : sequences) {
        w.beginObject();
        w.key("name");   w.value(sq.name);
        w.key("window"); w.value(static_cast<double>(sq.window));
        w.key("steps");
        w.beginArray();
        for (const auto& step : sq.steps) {
            w.beginArray();
            for (const auto& name : step) w.value(name);
            w.endArray();
        }
        w.endArray();
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
        a.binding.keyModifier  = static_cast<int>(num(o, "keyModifier", -1));
        a.binding.padModButton = static_cast<int>(num(o, "padModButton", -1));
        a.binding.padModAxis   = static_cast<int>(num(o, "padModAxis", -1));
        a.binding.padModSign   = static_cast<float>(num(o, "padModSign", 1.0));
        loaded.push_back(a);
    }

    // sequences : absentes des anciens fichiers
    std::vector<GameSequence> loadedSeq;
    if (root.has("sequences") && root["sequences"].isArray()) {
        for (const auto& o : root["sequences"].asArray()) {
            if (!o.has("name") || !o["name"].isString()) continue;
            GameSequence sq;
            sq.name = o["name"].asString();
            sq.window = static_cast<float>(num(o, "window", 0.35));
            if (o.has("steps") && o["steps"].isArray()) {
                for (const auto& st : o["steps"].asArray()) {
                    std::vector<std::string> step;
                    if (st.isArray()) {
                        for (const auto& n : st.asArray()) {
                            if (n.isString()) step.push_back(n.asString());
                        }
                    }
                    sq.steps.push_back(step);
                }
            }
            loadedSeq.push_back(sq);
        }
    }

    actions = std::move(loaded);
    sequences = std::move(loadedSeq);
    resizeState();
}