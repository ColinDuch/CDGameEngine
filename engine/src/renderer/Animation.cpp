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

namespace {
    // la racine du deplacement : l os anime le plus haut dans la hierarchie qui se deplace vraiment
    // (chez Mixamo, les hanches). Les os au-dessus ont parfois des cles, mais immobiles.
    int travelChannel(const std::vector<AnimChannel>& channels, const Skeleton& sk) {
        int best = -1, bestDepth = 1 << 30;
        for (size_t c = 0; c < channels.size(); ++c) {
            const AnimChannel& ch = channels[c];
            if (ch.bone < 0 || ch.bone >= static_cast<int>(sk.bones.size()) || ch.positions.size() < 2) continue;
            const glm::vec3 travel = ch.positions.back().value - ch.positions.front().value;
            if (glm::dot(travel, travel) < 1e-8f) continue;          // ne va nulle part
            int depth = 0;
            for (int b = sk.bones[ch.bone].parent; b >= 0; b = sk.bones[b].parent) ++depth;
            if (depth < bestDepth) {
                bestDepth = depth;
                best = static_cast<int>(c);
            }
        }
        return best;
    }

    // retire la part de la position qui va dans le sens du trajet (du premier au dernier point) :
    // le corps ne s eloigne plus, mais le rebond vertical et le balancement lateral restent.
    // Le sens est calcule dans le repere de l os : ca marche quel que soit l axe vertical du modele.
    glm::vec3 removeTravel(const AnimChannel& ch, const glm::vec3& p) {
        const glm::vec3 start = ch.positions.front().value;
        const glm::vec3 dir = glm::normalize(ch.positions.back().value - start);
        return p - dir * glm::dot(p - start, dir);
    }

}

Pose AnimationClip::samplePose(const Skeleton& sk, float t) const {
    Pose pose = sk.bindPose();  //les os non animes gardent leurs pose de repos 

    for (const auto& ch : channels) {
        if (ch.bone < 0 || ch.bone >= static_cast<int>(pose.size())) continue;

        if (!ch.positions.empty()) pose[ch.bone].position = sampleVec(ch.positions, t, pose[ch.bone].position);
        if (!ch.rotations.empty()) pose[ch.bone].rotation = sampleQuat(ch.rotations, t);
        if (!ch.scales.empty()) pose[ch.bone].scale = sampleVec(ch.scales, t, pose[ch.bone].scale);
    }
    if (inPlace) {
        const int c = travelChannel(channels, sk);
        if (c >= 0) {
            const AnimChannel& ch = channels[c];
            pose[ch.bone].position = removeTravel(ch, pose[ch.bone].position);
        }
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
