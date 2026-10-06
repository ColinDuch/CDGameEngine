#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <engine/script/ScriptEngine.hpp>

#include <engine/core/Localization.hpp>
#include <engine/core/GameInput.hpp>

#include <engine/renderer/SceneObject.hpp>

extern "C" {
    #include <lua.h>
    #include <lualib.h>
    #include <lauxlib.h>
}

#include <cctype>
#include <filesystem>
#include <string>

/* 
---------------------------------------------------------------------------
    Helpers internes
---------------------------------------------------------------------------
*/
namespace {

    ScriptEngine* selfFromUpvalue(lua_State* L) {
        return static_cast<ScriptEngine*>(lua_touserdata(L, lua_upvalueindex(1)));
    }

    std::string luaError(lua_State* L) {
        const char* m = lua_tostring(L, -1);
        return m ? std::string(m) : std::string(Lang::tr("unknown Lua error"));
    }

    int keyFromName(const std::string& n) {
        if (n.size() == 1) {
            char c = static_cast<char>(std::toupper(static_cast<unsigned char>(n[0])));
            if (c >= 'A' && c <= 'Z') return GLFW_KEY_A + (c - 'A');
            if (c >= '0' && c <= '9') return GLFW_KEY_0 + (c - '0');
        }
        std::string s;
        for (char c : n) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (s == "space") return GLFW_KEY_SPACE;
        if (s == "shift") return GLFW_KEY_LEFT_SHIFT;
        if (s == "ctrl" || s == "control") return GLFW_KEY_LEFT_CONTROL;
        if (s == "alt") return GLFW_KEY_LEFT_ALT;
        if (s == "tab") return GLFW_KEY_TAB;
        if (s == "escape" || s == "esc") return GLFW_KEY_ESCAPE;
        if (s == "enter" || s == "return") return GLFW_KEY_ENTER;
        if (s == "up") return GLFW_KEY_UP;
        if (s == "down") return GLFW_KEY_DOWN;
        if (s == "left") return GLFW_KEY_LEFT;
        if (s == "right") return GLFW_KEY_RIGHT;
        return -1;
    }

} // namespace

/*
---------------------------------------------------------------------------
    Cycle de vie
---------------------------------------------------------------------------
*/
ScriptEngine::ScriptEngine(std::vector<std::shared_ptr<SceneObject>>& objects)
    : objects(objects) {}

ScriptEngine::~ScriptEngine() {
    unload();
}

void ScriptEngine::unload() {
    for (auto& in : instances) {
        if (in.L) lua_close(in.L);
    }
    instances.clear();
}

/*
--------------------------------------------------------------------------
 API exposee a Lua (table globale "Engine")
---------------------------------------------------------------------------
*/
void ScriptEngine::registerApi(lua_State* L) {
    lua_newtable(L);   // Engine = {}

    auto reg = [&](const char* name, lua_CFunction fn) {
        lua_pushstring(L, name);
        lua_pushlightuserdata(L, this);   // upvalue : l'instance du moteur
        lua_pushcclosure(L, fn, 1);
        lua_settable(L, -3);
    };

    reg("count", &ScriptEngine::api_count);
    reg("getPosition", &ScriptEngine::api_getPosition);
    reg("setPosition", &ScriptEngine::api_setPosition);
    reg("getRotation", &ScriptEngine::api_getRotation);
    reg("setRotation", &ScriptEngine::api_setRotation);
    reg("log", &ScriptEngine::api_log);
    reg("isKeyDown", &ScriptEngine::api_isKeyDown);
    reg("setBool", &ScriptEngine::api_setBool);
    reg("setFloat", &ScriptEngine::api_setFloat);
    reg("setTrigger", &ScriptEngine::api_setTrigger);
    reg("getAnimState", &ScriptEngine::api_getAnimState);
    reg("isActionDown", &ScriptEngine::api_isActionDown);
    reg("wasActionPressed", &ScriptEngine::api_wasActionPressed);
    reg("wasActionReleased", &ScriptEngine::api_wasActionReleased);
    reg("getActionValue", &ScriptEngine::api_getActionValue);
    reg("getAxis", &ScriptEngine::api_getAxis);

    lua_setglobal(L, "Engine");
}

SceneObject* ScriptEngine::objectAt(lua_State* L, int argIndex) {
    ScriptEngine* self = selfFromUpvalue(L);
    int i = static_cast<int>(luaL_checkinteger(L, argIndex));
    if (i < 0 || i >= static_cast<int>(self->objects.size())) {
        luaL_error(L, Lang::tr("invalid object index: %d"), i);
        return nullptr;   // inatteignable (luaL_error saute)
    }
    return self->objects[i].get();
}

int ScriptEngine::api_count(lua_State* L) {
    ScriptEngine* self = selfFromUpvalue(L);
    lua_pushinteger(L, static_cast<lua_Integer>(self->objects.size()));
    return 1;
}

int ScriptEngine::api_getPosition(lua_State* L) {
    SceneObject* o = objectAt(L, 1);
    lua_pushnumber(L, o->position.x);
    lua_pushnumber(L, o->position.y);
    lua_pushnumber(L, o->position.z);
    return 3;
}

int ScriptEngine::api_setPosition(lua_State* L) {
    SceneObject* o = objectAt(L, 1);
    o->position = glm::vec3(
        static_cast<float>(luaL_checknumber(L, 2)),
        static_cast<float>(luaL_checknumber(L, 3)),
        static_cast<float>(luaL_checknumber(L, 4)));
    return 0;
}

int ScriptEngine::api_getRotation(lua_State* L) {
    SceneObject* o = objectAt(L, 1);
    glm::vec3 e = o->getEulerDegrees();
    lua_pushnumber(L, e.x);
    lua_pushnumber(L, e.y);
    lua_pushnumber(L, e.z);
    return 3;
}

int ScriptEngine::api_setRotation(lua_State* L) {
    SceneObject* o = objectAt(L, 1);
    o->setEulerDegrees(glm::vec3(
        static_cast<float>(luaL_checknumber(L, 2)),
        static_cast<float>(luaL_checknumber(L, 3)),
        static_cast<float>(luaL_checknumber(L, 4))));
    return 0;
}

int ScriptEngine::api_log(lua_State* L) {
    ScriptEngine* self = selfFromUpvalue(L);
    self->logText += luaL_checkstring(L, 1);
    self->logText += "\n";
    return 0;
}

int ScriptEngine::api_isKeyDown(lua_State* L) {
    ScriptEngine* self = selfFromUpvalue(L);
    const char* name = luaL_checkstring(L, 1);
    int key = keyFromName(name);
    if (key < 0) return luaL_error(L, Lang::tr("unknown key: %s"), name);

    bool down = self->windowHandle && glfwGetKey(self->windowHandle, key) == GLFW_PRESS;
    lua_pushboolean(L, down ? 1 : 0);
    return 1;
}

int ScriptEngine::api_setBool(lua_State* L) {
    SceneObject* o = objectAt(L, 1);
    const char* name = luaL_checkstring(L, 2);
    if (!o->stateMachine.setBool(name, lua_toboolean(L, 3) != 0))
        return luaL_error(L, Lang::tr("setBool: no Bool parameter '%s'"), name);
    return 0;
}

int ScriptEngine::api_setFloat(lua_State* L) {
    SceneObject* o = objectAt(L, 1);
    const char* name = luaL_checkstring(L, 2);
    if (!o->stateMachine.setFloat(name, static_cast<float>(luaL_checknumber(L, 3))))
        return luaL_error(L, Lang::tr("setFloat: no Float parameter '%s'"), name);
    return 0;
}

int ScriptEngine::api_setTrigger(lua_State* L) {
    SceneObject* o = objectAt(L, 1);
    const char* name = luaL_checkstring(L, 2);
    if (!o->stateMachine.setTrigger(name))
        return luaL_error(L, Lang::tr("setTrigger: no Trigger parameter '%s'"), name);
    return 0;
}

int ScriptEngine::api_getAnimState(lua_State* L) {
    SceneObject* o = objectAt(L, 1);
    const char* state = o->stateMachine.currentStateName();
    if (state) lua_pushstring(L, state);
    else lua_pushnil(L);
    return 1;
}

/*
---------------------------------------------------------------------------
Actions de jeu (input.json) : clavier et manette confondus
---------------------------------------------------------------------------
*/
namespace {
    // index de l action nommee a l argument arg, erreur Lua si inconnue
    int actionArg(lua_State* L, const GameInput* in, int arg) {
        const char* name = luaL_checkstring(L, arg);
        int a = in ? in->find(name) : -1;
        if (a < 0) luaL_error(L, Lang::tr("unknown action: %s"), name);
        return a;
    }
}

int ScriptEngine::api_isActionDown(lua_State* L) {
    const GameInput* in = selfFromUpvalue(L)->gameInput;
    int a = actionArg(L, in, 1);
    lua_pushboolean(L, in->isDown(a) ? 1 : 0);
    return 1;
}

int ScriptEngine::api_wasActionPressed(lua_State* L) {
    const GameInput* in = selfFromUpvalue(L)->gameInput;
    int a = actionArg(L, in, 1);
    lua_pushboolean(L, in->wasPressed(a) ? 1 : 0);
    return 1;
}

int ScriptEngine::api_wasActionReleased(lua_State* L) {
    const GameInput* in = selfFromUpvalue(L)->gameInput;
    int a = actionArg(L, in, 1);
    lua_pushboolean(L, in->wasReleased(a) ? 1 : 0);
    return 1;
}

int ScriptEngine::api_getActionValue(lua_State* L) {
    const GameInput* in = selfFromUpvalue(L)->gameInput;
    int a = actionArg(L, in, 1);
    lua_pushnumber(L, in->value(a));
    return 1;
}

// getAxis("move_left", "move_right") : -1..1, analogique au stick
int ScriptEngine::api_getAxis(lua_State* L) {
    const GameInput* in = selfFromUpvalue(L)->gameInput;
    int negative = actionArg(L, in, 1);
    int positive = actionArg(L, in, 2);
    lua_pushnumber(L, in->value(positive) - in->value(negative));
    return 1;
}

/*
---------------------------------------------------------------------------
Chargement : un etat Lua par script
---------------------------------------------------------------------------
*/
bool ScriptEngine::openScript(const std::string& path, int objectIndex, std::string& err) {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    registerApi(L);

    lua_pushinteger(L, objectIndex);
    lua_setglobal(L, "self");          // index de l'objet portant ce script (-1 = scene)

    if (luaL_dofile(L, path.c_str()) != LUA_OK) {
        err = luaError(L);
        lua_close(L);
        return false;
    }

    Instance in;
    in.L = L;
    in.objectIndex = objectIndex;
    in.path = path;
    in.ok = true;
    instances.push_back(in);
    return true;
}

void ScriptEngine::loadProject(const std::string& projectDir, std::string& outStatus) {
    unload();
    namespace fs = std::filesystem;

    int loaded = 0;
    std::string errors;

    // Script de scene (optionnel).
    fs::path mainPath = fs::path(projectDir) / "code" / "main.lua";
    if (fs::exists(mainPath)) {
        std::string err;
        if (openScript(mainPath.string(), -1, err)) loaded++;
        else errors += "main.lua : " + err + "\n";
    }

    // Un script par objet.
    for (size_t i = 0; i < objects.size(); ++i) {
        const std::string& rel = objects[i]->scriptPath;
        if (rel.empty()) continue;

        fs::path p = fs::path(projectDir) / rel;
        if (!fs::exists(p)) { errors += rel + " : " + Lang::tr("not found") + "\n"; continue; }

        std::string err;
        if (openScript(p.string(), static_cast<int>(i), err)) loaded++;
        else errors += rel + " : " + err + "\n";
    }

    outStatus = Lang::fmt("%d script(s) loaded", loaded);
    if (!errors.empty()) outStatus += "\n" + errors;
}

/*
---------------------------------------------------------------------------
Appels par frame
---------------------------------------------------------------------------
*/
void ScriptEngine::callStart(std::string& outError) {
    for (auto& in : instances) {
        if (!in.ok) continue;
        lua_getglobal(in.L, "start");
        if (!lua_isfunction(in.L, -1)) { lua_pop(in.L, 1); continue; }
        if (lua_pcall(in.L, 0, 0, 0) != LUA_OK) {
            outError += in.path + " (start) : " + luaError(in.L) + "\n";
            lua_pop(in.L, 1);
            in.ok = false;
        }
    }
}

void ScriptEngine::callUpdate(float dt, std::string& outError) {
    for (auto& in : instances) {
        if (!in.ok) continue;
        lua_getglobal(in.L, "update");
        if (!lua_isfunction(in.L, -1)) { lua_pop(in.L, 1); continue; }
        lua_pushnumber(in.L, dt);
        if (lua_pcall(in.L, 1, 0, 0) != LUA_OK) {
            outError += in.path + " (update) : " + luaError(in.L) + "\n";
            lua_pop(in.L, 1);
            in.ok = false;
        }
    }
}

void ScriptEngine::callNotify(int objectIndex, const std::string& notifyName, std::string& outError) {
    for (auto& in : instances) {
        if (!in.ok) continue;
        // -1 = script de scene (recois toutes les notifies)
        if (in.objectIndex != objectIndex && in.objectIndex != -1) continue;

        lua_getglobal(in.L, "onNotify");
        if (!lua_isfunction(in.L, -1)) { lua_pop(in.L, 1); continue; }
        
        lua_pushstring(in.L, notifyName.c_str());
        lua_pushinteger(in.L, objectIndex);
        if (lua_pcall(in.L, 2, 0, 0) != LUA_OK) {
            outError += in.path + " (onNotify) : " + luaError(in.L) + "\n";
            lua_pop(in.L, 1);
            in.ok = false;
        }
    }
}