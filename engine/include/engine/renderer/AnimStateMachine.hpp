#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/*
machine a etats d animation facon Unreal sans le graphe 
les etats disent quel clips jouer 
les transitions quand en changer 
les parametres sont pousses par le script en lua 
le gameplay reste dasn le script et la choregraphie devient de la donnee 

deux niveaux :
- AnimStateMachineAsset : la definition, partage (comme un materiau)
-AnimStateMachineInstance : l execution, une par objet (etat courant + valeurs)
*/

enum class AnimParamType { Bool, Float, Trigger };

struct AnimParamDef {

    std::string name;
    AnimParamType type = AnimParamType::Bool;
    float defaultValue = 0.0f;  // Bool / Trigger : 0 ou 1
    bool operator==(const AnimParamDef&) const = default;

};

struct AnimCondition {

    enum class Op { IsTrue, IsFalse, Greater, Less };
    int param = -1;     //index dans les params, -1 = parametre supprime
    Op op = Op::IsTrue;
    float value = 0.0f;
    bool operator==(const AnimCondition&) const = default;
};

struct AnimTransition {

    int from = -1;      //-1 = depuis n importe quel etat
    int to = 0;
    float fade = 0.2f;
    bool waitEnd = false;       // n autorise qu une fois le clip fini 
    std::vector<AnimCondition> conditions;  // toutes vraies = ET
    bool operator==(const AnimTransition&) const = default;

};

struct AnimState {

    std::string name ="State";
    std::string clipName;       //par nom comme la persitance des clips 
    bool loop = true;
    float speed = 1.0f;
    bool operator==(const AnimState&) const = default;

};

class AnimStateMachineAsset {

    public:

        std::string name = "State Machine";
        std::vector<AnimParamDef> params;
        std::vector<AnimState> states;
        std::vector<AnimTransition> transitions;
        int entry = 0;

        int findParam(const std::string& n) const;
        int findState(const std::string& n) const;

        //supressions qui gardent les index coherents 
        void removeParam(int index);

        // contenu identique (la revision n est pas comparee)
        bool sameContent(const AnimStateMachineAsset& o) const;
        // copie le contenu de o en gardant sa revision, puis touch()
        void restoreFrom(const AnimStateMachineAsset& o);

        void removeState(int index);

        //a appeler apres tout changement de structure (ajout/supression/type)
        void touch() { ++revision; }
        uint32_t getRevision() const { return revision; }
    
    private:

        uint32_t revision = 0;

};

struct AnimStateMachineInstance {

    std::shared_ptr<const AnimStateMachineAsset> asset;

    // execution, non serialise
    int current = -1;   // -1 = pas encore entre 
    std::vector<float> values;   // une valeur par parametre de l asset
    uint32_t seenRevision = 0;

    bool active() const { return asset && !asset->states.empty(); }
    void reset();       // retour a l etat d entree, valeurs par defaut 

    //false si le parametre n existe pas ou n a pas ce type
    bool setBool(const std::string& n, bool v);
    bool setFloat(const std::string& n, float v);
    bool setTrigger(const std::string&n);

    const char* currentStateName() const;   //nullptr si aucun

};

class SceneObject;
struct AnimationClip;

// avance la machine d une image. Rend true si l etat a change
bool stepStateMachine(SceneObject& obj, const std::vector<AnimationClip>& clips);
