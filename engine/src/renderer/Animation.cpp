#include <engine/renderer/Animation.hpp>

#include <algorithm>

namespace {

    // trouve l'intervale en les cles d'animations et leurs facteur de mélanges 

    template <typename KeyT>
    size_t findKey(const std::vector<KeyT>& keys, float t, float& alpha) {
        alpha = 0.0f;
        if (keys.size() < 2) return 0;
        for (size_t k = 0; k + 1 < keys.size(); ++k) {
            if (t < keys[k + 1].time) {
                float span = keys[k + 1].time - keys[k].time;
                alpha = (span >1e-6f) ? (t - keys[k].time) / span : 0.0f;
                alpha = std::clamp(alpha, 0.0f, 1.0f);
                return k;
            }
        }
        return keys.size() - 1;
    }

    /* 
    "blend" est le facteur d'interpolation 
    (la position de t entre deux clés aka un nombre entre 0 et 1)
    */
    glm::vec3 sampleVec(const std::vector<VecKey>& keys, float t, const glm::vec3& fallback) {
        if (keys.empty()) return fallback;
        if (keys.size() == 1 ) return keys[0].value;
        float blend = 0.0f;
        size_t k = findKey(keys, t, blend);
        if (k + 1 >= keys.size()) return keys.back().value;
        return glm::mix(keys[k].value, keys[k + 1].value, blend);
    }

    glm::quat sampleQuat(const std::vector<QuatKey>& keys, float t) {
        if (keys.empty()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        if (keys.size() == 1) return keys[0].value;
        float blend = 0.0f;
        size_t k = findKey(keys, t, blend);
        if (k + 1 >= keys.size()) return keys.back().value;
        return glm::normalize(glm::slerp(keys[k].value, keys[k + 1].value, blend));
    }

}

std::vector<glm::mat4> AnimationClip::sampleLocals(const Skeleton& sk, float t) const {
    std::vector<glm::mat4> locals(sk.bones.size(), glm::mat4(1.0f));
    for (size_t i = 0; i < sk.bones.size(); ++i) locals[i] = sk.bones[i].localBind;

    for (const auto& ch : channels) {
        if (ch.bone <0 || ch.bone >= static_cast<int>(locals.size())) continue;

        glm::vec3 varPos = sampleVec(ch.positions, t, glm::vec3(0.0f));
        glm::quat varRot = sampleQuat(ch.rotations, t);
        glm::vec3 varScl = sampleVec(ch.scales, t, glm::vec3(1.0f));

        locals[ch.bone] = glm::translate(glm::mat4(1.0f), varPos)
                        * glm::mat4_cast(varRot)
                        * glm::scale(glm::mat4(1.0f), varScl);
    }
    return locals;
}

Pose AnimationClip::samplePose(const Skeleton& sk, float t) const {
    Pose pose = sk.bindPose();  //les os non animes gardent leurs pose de repos 

    for (const auto& ch : channels) {
        if (ch.bone < 0 || ch.bone >= static_cast<int>(pose.size())) continue;

        if (!ch.positions.empty()) pose[ch.bone].position = sampleVec(ch.positions, t, pose[ch.bone].position);
        if (!ch.rotations.empty()) pose[ch.bone].rotation = sampleQuat(ch.rotations, t);
        if (!ch.scales.empty()) pose[ch.bone].scale = sampleVec(ch.scales, t, pose[ch.bone].scale);
    }
    return pose;
}

Pose blendPoses(const Pose& a, const Pose& b, float w) {
    w = std::clamp(w, 0.0f, 1.0f);
    size_t n = std::max(a.size(), b.size());

    Pose out(n);
    for (size_t i = 0; i < n; ++i) {
        const BoneTransform& ta = (i < a.size()) ? a[i] : BoneTransform{};
        const BoneTransform& tb = (i < b.size()) ? b[i] : BoneTransform{};

        out[i].position = glm::mix(ta.position, tb.position, w);
        out[i].scale = glm::mix(ta.scale, tb.scale, w);
        out[i].rotation = glm::normalize(glm::slerp(ta.rotation, tb.rotation, w));
    }
    return out;
}
