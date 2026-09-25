#include <engine/renderer/AnimStateMachine.hpp>
#include <engine/renderer/SceneObject.hpp>
#include <engine/renderer/Animation.hpp>

int AnimStateMachine::findParam(const std::string& n) const {
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].name == n) return static_cast<int>(i);
    return -1;
}

void AnimStateMachine::setBool(const std::string& n, bool v) {
    int i = findParam(n);
    if (i >= 0) params[i].bValue = v;
}

void AnimStateMachine::setFloat(const std::string& n, float v) {
    int i = findParam(n);
    if (i >= 0) params[i].fValue = v;
}

void AnimStateMachine::setTrigger(const std::string& n) {
    int i = findParam(n);
    if (i >= 0) params[i].bValue = true;
}

void AnimStateMachine::clearTriggers() {
    for (auto& p : params)
        if (p.type == AnimParam::Type::Trigger) p.bValue = false;
}

namespace {
    bool conditionMet(const AnimStateMachine& sm, const AnimCondition& c) {
        if (c.param < 0 || c.param >= static_cast<int>(sm.params.size())) return false;
        const AnimParam& p =sm.params[c.param];
        switch (c.op) {
            case AnimCondition::Op::IsTrue: return p.bValue;
            case AnimCondition::Op::IsFalse: return !p.bValue;
            case AnimCondition::Op::Greater: return p.fValue > c.value;
            case AnimCondition::Op::Less: return p.fValue < c.value;
        }
        return false;
    }

    int clipIndexByName(const std::vector<AnimationClip>& clips, const std::string& name) {
        for (size_t i = 0; i < clips.size(); ++i)
            if (clips[i].name == name) return static_cast<int>(i);
        return -1;
    }

    void applyState(SceneObject& obj, AnimStateMachine& sm, int index, const std::vector<AnimationClip>& clips, float fade) {
        const AnimState& st = sm.states[index];
        sm.current = index;

        obj.animLoop = st.loop;
        obj.animSpeed = st.speed;
        obj.animPlaying = true;

        int clip = clipIndexByName(clips, st.clipName);
        if (clip >= 0) obj.playClip(clip, fade); 
    }
}

bool stepStateMachine(SceneObject& obj, const std::vector<AnimationClip>& clips) {
    AnimStateMachine& sm = obj.stateMachine;
    if (sm.states.empty()) return false;

    if (sm.current < 0 || sm.current >= static_cast<int>(sm.states.size())) {
        int e = (sm.entry >= 0 && sm.entry < static_cast<int>(sm.states.size())) ? sm.entry : 0;
        applyState(obj, sm, e, clips, 0.0f);
        sm.clearTriggers();
        return true;
    }

    for (const AnimTransition& tr : sm.transitions) {
        if (tr.from >= 0 && tr.from != sm.current) continue;
        if (tr.to < 0 || tr.to >= static_cast<int>(sm.states.size())) continue;
        if (tr.to == sm.current) continue;
        if (tr.waitEnd && !obj.animJustEnded) continue;

        bool all = true;
        for (const AnimCondition& c : tr.conditions) {
            if (!conditionMet(sm, c)) { all = false; break ; }
        }
        if (!all) continue;

        applyState(obj, sm, tr.to, clips, tr.fade);
        sm.clearTriggers();
        return true;
    }
    return false;
}