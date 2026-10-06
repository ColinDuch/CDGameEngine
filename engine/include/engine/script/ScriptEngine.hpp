#pragma once

#include <memory>
#include <string>
#include <vector>

struct lua_State;
struct GLFWwindow;
class SceneObject;
class GameInput;

// Un etat Lua par script : le script de scene (code/main.lua) et un script
// par objet (SceneObject::scriptPath). Chaque script recoit un global `self`
// = index de son objet (-1 pour le script de scene).
class ScriptEngine {
public:
    explicit ScriptEngine(std::vector<std::shared_ptr<SceneObject>>& objects);
    ~ScriptEngine();

    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    void setWindow(GLFWwindow* w) { windowHandle = w; }
    void setGameInput(const GameInput* g) { gameInput = g; }

    void loadProject(const std::string& projectDir, std::string& outStatus);
    void callStart(std::string& outError);
    void callUpdate(float dt, std::string& outError);
    void unload();
    void callNotify(int objectIndex, const std::string& notifyName, std::string& outError);

    bool hasScript() const { return !instances.empty(); }
    int  scriptCount() const { return static_cast<int>(instances.size()); }

    const std::string& getLog() const { return logText; }
    void clearLog() { logText.clear(); }

private:
    struct Instance {
        lua_State* L = nullptr;
        int objectIndex = -1;      // -1 = script de scene
        std::string path;
        bool ok = false;
    };

    bool openScript(const std::string& path, int objectIndex, std::string& err);
    void registerApi(lua_State* L);

    static SceneObject* objectAt(lua_State* L, int argIndex);
    static int api_count(lua_State* L);
    static int api_getPosition(lua_State* L);
    static int api_setPosition(lua_State* L);
    static int api_getRotation(lua_State* L);
    static int api_setRotation(lua_State* L);
    static int api_log(lua_State* L);
    static int api_isKeyDown(lua_State* L);
    static int api_setBool(lua_State* L);
    static int api_setFloat(lua_State* L);
    static int api_setTrigger(lua_State* L);
    static int api_getAnimState(lua_State* L);
    static int api_isActionDown(lua_State* L);
    static int api_wasActionPressed(lua_State* L);
    static int api_wasActionReleased(lua_State* L);
    static int api_getActionValue(lua_State* L);
    static int api_getAxis(lua_State* L);

    std::vector<Instance> instances;
    std::vector<std::shared_ptr<SceneObject>>& objects;
    GLFWwindow* windowHandle = nullptr;
    const GameInput* gameInput = nullptr;
    std::string logText;
};