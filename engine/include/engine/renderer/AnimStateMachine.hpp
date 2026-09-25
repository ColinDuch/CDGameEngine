#pragma once

#include <string>
#include <vector>

/*
machine a etats d animation facon Unreal sans le graphe
les etats disent quel clips jouer 
les transitions quand en changer
les parametres sont pousses par le script en lua
le gameplay reste dasn le script et la choregraphie devient de la donnee
*/

struct AnimParam {
    enum class Type { Bool, Float, Trigger };
    std::string name;
    Type type = Type::Bool;
    bool bValue = false;
    float fValue = 0.0f;
};

struct AnimCondition {
    enum class Op { IsTrue, IsFalse, Greater, Less };
    int param = -1;     // index dans AnimStateMachine::params
    Op op = Op::IsTrue;
    float value = 0.0f;     // seuil, pour Greater et Less 
};

struct AnimTransition {
    int from = -1;      // -1 = depuis n importe quel etat
    int to = 0;
    float fade = 0.2f;
    bool waitEnd = false;       // n autorise qu une fois le clip
    std::vector<AnimCondition> conditions;  //toutes vraies = ET
};

struct AnimState {
    std::string name = "State";
    std::string clipName;       // par nom comme la persistance des clips 
    bool loop = true;
    float speed = 1.0f;
};

struct AnimStateMachine {
    std::vector<AnimState> states;
    std::vector<AnimTransition> transitions;
    std::vector<AnimParam> params;
    int entry = 0;

    // execution non serialise
    int current = -1;       // -1 = pas encore entre 

    bool empty() const { return states.empty(); }

    int findParam(const std::string& n) const;
    void setBool(const std::string& n, bool v);
    void setFloat(const std::string& n, float v);
    void setTrigger(const std::string& n);
    void clearTriggers();
    void reset() { current = -1; clearTriggers(); }
};

class SceneObject;
struct AnimationClip;

//avance la machine d une image Rend true si l etat a change 
bool stepStateMachine(SceneObject& obj, const std::vector<AnimationClip>& clips);