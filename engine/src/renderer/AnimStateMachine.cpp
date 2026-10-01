#include <engine/renderer/AnimStateMachine.hpp>
#include <engine/renderer/SceneObject.hpp>
#include <engine/renderer/Animation.hpp>

#include <algorithm>

/*
-----------------------------
Asset : definition partagee 
-----------------------------
*/

int AnimStateMachineAsset::findParam(const std::string& n) const {
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].name == n) return static_cast<int>(i);
    return -1;
}

int AnimStateMachineAsset::findState(const std::string& n) const {
    for (size_t i = 0; i < states.size(); ++i)
        if (states[i].name == n) return static_cast<int>(i);
    return -1;
}

void AnimStateMachineAsset::removeParam(int index) {
    if (index < 0 || index >= static_cast<int>(params.size())) return;
    params.erase(params.begin() + index);

    for (auto& tr : transitions) {
        for (auto& c : tr.conditions) {
            if (c.param == index) c.param = -1;     //on ne supprime pas la condition
            else if (c.param > index) --c.param;
        }
    }
    touch();
}

void AnimStateMachineAsset::removeState(int index) {
    if (index < 0 || index >= static_cast<int>(states.size())) return;
    states.erase(states.begin() + index);

    //une transition vers ou depuis cet etat n a plus de sens 
    transitions.erase(std::remove_if(transitions.begin(), transitions.end(), 
        [index](const AnimTransition& tr) { return tr.from == index || tr.to == index; }),
        transitions.end());
    
    for (auto& tr : transitions) {
        if (tr.from > index) --tr.from;     // -1 (n importe lequel) n est pas une touche 
        if (tr.to > index) --tr.to;
    }

    if (entry == index) entry = 0;
    else if (entry > index) --entry;
    touch();
}

/*
-------------------------------------------
Instance : execution par objet
-------------------------------------------
*/

void AnimStateMachineInstance::reset() {

    current = -1;
    values.clear();
    if (!asset) return;
    for (const auto& p : asset->params) values.push_back(p.defaultValue);
    seenRevision = asset->getRevision();

}

namespace {

    // cherche un parametre du bon, type, -1 sinon
    int paramOfType(const AnimStateMachineInstance& sm, const std::string& n, AnimParamType t) {
        if (!sm.asset) return -1;
        int i = sm.asset->findParam(n);
        if (i < 0 || i >= static_cast<int>(sm.values.size())) return -1;
        if (sm.asset->params[i].type != t) return -1;
        return i;

    }
}

bool AnimStateMachineInstance::setBool(const std::string& n, bool v) {
    int i = paramOfType(*this, n, AnimParamType::Bool);
    if (i < 0) return false;
    values[i] = v ? 1.0f : 0.0f;
    return true;
}

bool AnimStateMachineInstance::setFloat(const std::string& n, float v) {
    int i = paramOfType(*this, n, AnimParamType::Float);
    if (i < 0) return false;
    values[i] = v;
    return true;
}

bool AnimStateMachineInstance::setTrigger(const std::string& n) {
    int i = paramOfType(*this, n, AnimParamType::Trigger);
    if (i < 0) return false;
    values[i] = 1.0f;
    return true;
}

const char* AnimStateMachineInstance::currentStateName() const {
    if (!asset || current < 0 || current >= static_cast<int>(asset->states.size())) return nullptr;
    return asset->states[current].name.c_str();
}

/*
----------------------------------
Pas de simulation
----------------------------------
*/

namespace {

    bool conditionMet(const AnimStateMachineInstance& sm, const AnimCondition& c) {

        //parametre supprime ou invalide : la condition est fausse 
        if (c.param < 0 || c.param >= static_cast<int>(sm.values.size())) return false;
        float v =sm.values[c.param];
        switch (c.op) {
            case AnimCondition::Op::IsTrue: return v != 0.0f;
            case AnimCondition::Op::IsFalse: return v == 0.0f;
            case AnimCondition::Op::Greater: return v > c.value;
            case AnimCondition::Op::Less: return v < c.value;

        }
        return false;

    }

    int clipIndexByName(const std::vector<AnimationClip>& clips, const std::string& name) {
        for (size_t i = 0; i < clips.size(); ++i)
            if (clips[i].name == name) return static_cast<int>(i);
        return -1;
    }

    void applyState(SceneObject& obj, int index, const std::vector<AnimationClip>& clips, float fade) {
        AnimStateMachineInstance& sm = obj.stateMachine;
        const AnimState& st = sm.asset->states[index];
        sm.current = index;

        obj.animLoop = st.loop;
        obj.animSpeed = st.speed;
        obj.animPlaying = true;

        int clip = clipIndexByName(clips, st.clipName);
        if (clip >= 0) obj.playClip(clip, fade);
    }

    void clearTriggers(AnimStateMachineInstance& sm) {
        for (size_t i = 0; i < sm.values.size(); ++i)
            if (sm.asset->params[i].type == AnimParamType::Trigger) sm.values[i] = 0.0f;
    }
}

bool stepStateMachine(SceneObject& obj, const std::vector<AnimationClip>& clips) {

    AnimStateMachineInstance& sm = obj.stateMachine;
    if (!sm.active()) return false;
    const AnimStateMachineAsset& a = *sm.asset;

    // la structure a change depuis le dernier pas : les index ne sont plus fiables
    if (sm.seenRevision != a.getRevision() || sm.values.size() != a.params.size()) sm.reset();

    const int stateCount = static_cast<int>(a.states.size());
    bool changed = false;

    if (sm.current < 0 || sm.current >= stateCount) {
        int e = (a.entry >= 0 && a.entry < stateCount) ? a.entry : 0;
        applyState(obj, e, clips, 0.0f);
        changed = true;
    } else {
        for (const AnimTransition& tr : a.transitions) {
            if (tr.from >= 0 && tr.from != sm.current) continue;
            if (tr.to < 0 || tr.to >= stateCount) continue;
            if (tr.to == sm.current) continue;
            if (tr.waitEnd && !obj.animJustEnded) continue;

            bool all = true;
            for (const AnimCondition& c : tr.conditions) {
                if (!conditionMet(sm, c)) { all = false; break; }
            }
            
            if (!all) continue;

            applyState(obj, tr.to, clips, tr.fade);
            changed = true;
            break;      //une seule transition par image
        }
    }

    // un trigger ne vit qu une image : consomme ou perdu
    clearTriggers(sm);
    return changed;
}
