#include <engine/scene/AnimStateMachineSerializer.hpp>
#include <engine/scene/JsonWriter.hpp>
#include <engine/scene/JsonParser.hpp>
#include <engine/core/Localization.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
    constexpr int FORMAT_VERSION = 1;   // a incrementer si le format change

    const char* typeToString(AnimParamType t) {
        switch (t) {
            case AnimParamType::Bool:    return "bool";
            case AnimParamType::Float:   return "float";
            case AnimParamType::Trigger: return "trigger";
        }
        return "bool";
    }

    AnimParamType typeFromString(const std::string& s) {
        if (s == "float")   return AnimParamType::Float;
        if (s == "trigger") return AnimParamType::Trigger;
        return AnimParamType::Bool;
    }

    const char* opToString(AnimCondition::Op op) {
        switch (op) {
            case AnimCondition::Op::IsTrue:  return "isTrue";
            case AnimCondition::Op::IsFalse: return "isFalse";
            case AnimCondition::Op::Greater: return "greater";
            case AnimCondition::Op::Less:    return "less";
        }
        return "isTrue";
    }

    AnimCondition::Op opFromString(const std::string& s) {
        if (s == "isFalse") return AnimCondition::Op::IsFalse;
        if (s == "greater") return AnimCondition::Op::Greater;
        if (s == "less")    return AnimCondition::Op::Less;
        return AnimCondition::Op::IsTrue;
    }

    // lecture tolerante : cle absente ou mauvais type = valeur par defaut
    float getFloat(const JsonValue& o, const char* k, float def) {
        return (o.has(k) && o[k].isNumber()) ? static_cast<float>(o[k].asDouble()) : def;
    }

    bool getBool(const JsonValue& o, const char* k, bool def) {
        return (o.has(k) && o[k].isNumber()) ? (o[k].asDouble() != 0.0) : def;
    }

    std::string getString(const JsonValue& o, const char* k, const std::string& def = "") {
        return (o.has(k) && o[k].isString()) ? o[k].asString() : def;
    }

    const JsonValue::Array& getArray(const JsonValue& o, const char* k) {
        static const JsonValue::Array empty;
        return (o.has(k) && o[k].isArray()) ? o[k].asArray() : empty;
    }
}

std::string AnimStateMachineSerializer::fileNameFor(const std::string& machineName) {
    std::string s = machineName.empty() ? std::string("StateMachine"): machineName;
    for (char& c : s) {
        if (c == '/' || c == '\\' || c == ':' || c == ' ') c ='_';
    }
    return s + ".animsm";
}

void AnimStateMachineSerializer::save(const AnimStateMachineAsset& sm, const std::string& path) {
    namespace fs = std::filesystem;
    JsonWriter w;
    w.beginObject();

    w.key("version"); w.value(FORMAT_VERSION);
    w.key("name");    w.value(sm.name);

    const bool entryValid = sm.entry >= 0 && sm.entry < static_cast<int>(sm.states.size());
    w.key("entry");   w.value(entryValid ? sm.states[sm.entry].name : std::string(""));

    w.key("params");
    w.beginArray();
    for (const auto& p : sm.params) {
        w.beginObject();
        w.key("name");    w.value(p.name);
        w.key("type");    w.value(std::string(typeToString(p.type)));
        w.key("default"); w.value(static_cast<double>(p.defaultValue));
        w.endObject();
    }
    w.endArray();

    w.key("states");
    w.beginArray();
    for (const auto& s : sm.states) {
        w.beginObject();
        w.key("name");  w.value(s.name);
        w.key("clip");  w.value(s.clipName);
        w.key("loop");  w.value(s.loop ? 1 : 0);
        w.key("speed"); w.value(static_cast<double>(s.speed));
        w.endObject();
    }
    w.endArray();

    w.key("transitions");
    w.beginArray();
    for (const auto& tr : sm.transitions) {
        w.beginObject();
        if (tr.from >= 0) { w.key("from"); w.value(sm.states[tr.from].name); }   // absent = n importe lequel
        w.key("to");      w.value(sm.states[tr.to].name);
        w.key("fade");    w.value(static_cast<double>(tr.fade));
        w.key("waitEnd"); w.value(tr.waitEnd ? 1 : 0);

        w.key("conditions");
        w.beginArray();
        for (const auto& c : tr.conditions) {
            const bool valid = c.param >= 0 && c.param < static_cast<int>(sm.params.size());
            w.beginObject();
            w.key("param"); w.value(valid ? sm.params[c.param].name : std::string(""));
            w.key("op");    w.value(std::string(opToString(c.op)));
            w.key("value"); w.value(static_cast<double>(c.value));
            w.endObject();
        }
        w.endArray();
        w.endObject();
    }
    w.endArray();

    w.endObject();

    // ecriture dans un fichier temporaire puis renommage :
    // un crash pendant l ecriture ne laisse jamais un fichier a moitie ecrit
    const std::string tmp = path + ".tmp";
    {
        std::ofstream file(tmp);
        if (!file.is_open()) throw std::runtime_error(Lang::fmt("Cannot write: %s", tmp.c_str()));
        file << w.toString();
        if (!file) throw std::runtime_error(Lang::fmt("Incomplete write: %s", tmp.c_str()));
    }
    fs::rename(tmp, path);
}

std::shared_ptr<AnimStateMachineAsset> AnimStateMachineSerializer::load(const std::string& path, std::string& warnings) {
    namespace fs = std::filesystem;
    std::ifstream file(path);
    if (!file.is_open()) throw std::runtime_error(Lang::fmt("Cannot open: %s", path.c_str()));
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    JsonValue root = JsonParser::parse(content);
    if (!root.isObject()) throw std::runtime_error(Lang::fmt("Invalid file: %s", path.c_str()));

    const int version = static_cast<int>(getFloat(root, "version", 1.0f));
    if (version > FORMAT_VERSION)
        throw std::runtime_error(Lang::fmt("Format newer than the engine: %s", path.c_str()));

    auto sm = std::make_shared<AnimStateMachineAsset>();
    sm->name = getString(root, "name", fs::path(path).stem().string());

    for (const auto& p : getArray(root, "params")) {
        AnimParamDef d;
        d.name = getString(p, "name");
        d.type = typeFromString(getString(p, "type", "bool"));
        d.defaultValue = getFloat(p, "default", 0.0f);
        sm->params.push_back(d);
    }

    for (const auto& s : getArray(root, "states")) {
        AnimState st;
        st.name = getString(s, "name", "State");
        st.clipName = getString(s, "clip");
        st.loop = getBool(s, "loop", true);
        st.speed = getFloat(s, "speed", 1.0f);
        sm->states.push_back(st);
    }

    const int entry = sm->findState(getString(root, "entry"));
    sm->entry = entry >= 0 ? entry : 0;

    for (const auto& t : getArray(root, "transitions")) {
        AnimTransition tr;

        if (t.has("from")) {
            const std::string fromName = getString(t, "from");
            tr.from = sm->findState(fromName);
            if (tr.from < 0) {
                warnings += " | " + Lang::fmt("Transition skipped, unknown state '%s' [%s]",
                    fromName.c_str(), sm->name.c_str());
                continue;
            }
        }

        const std::string toName = getString(t, "to");
        tr.to = sm->findState(toName);
        if (tr.to < 0) {
            warnings += " | " + Lang::fmt("Transition skipped, unknown state '%s' [%s]",
                toName.c_str(), sm->name.c_str());
            continue;
        }

        tr.fade = getFloat(t, "fade", 0.2f);
        tr.waitEnd = getBool(t, "waitEnd", false);

        for (const auto& c : getArray(t, "conditions")) {
            AnimCondition cond;
            const std::string paramName = getString(c, "param");
            cond.param = sm->findParam(paramName);   // -1 si inconnu : la condition reste FAUSSE
            if (cond.param < 0)
                warnings += " | " + Lang::fmt("Condition blocked, unknown parameter '%s' [%s]",
                                              paramName.c_str(), sm->name.c_str());
            cond.op = opFromString(getString(c, "op", "isTrue"));
            cond.value = getFloat(c, "value", 0.0f);
            tr.conditions.push_back(cond);
        }

        sm->transitions.push_back(tr);
    }

    return sm;
}