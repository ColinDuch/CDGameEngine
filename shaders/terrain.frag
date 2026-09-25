#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) flat in int fragRenderMode;
layout(location = 4) in vec3 fragTangent;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 lightDirection;
    vec3 lightColor;
    float lightIntensity;
    float ambientStrength;
    vec3 cameraPosition;
    float shininess;
    float exposure;
    mat4 lightSpace;
    float shadowStrength;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D envSampler;
layout(set = 1, binding = 1) uniform sampler2D shadowMap;

// ---------------------------------------------------------------------------
// Les 4 couches du sol sont de VRAIS MaterialAsset : sets 2 a 5, avec
// EXACTEMENT les memes bindings que le materiau des objets (set 2 de shader.frag).
// height et opacity existent dans le layout mais ne sont pas echantillonnes ici :
// un parallax x4 couterait cher, et un sol n'a pas d'opacite.
// ---------------------------------------------------------------------------
#define LAYER_BLOCK(S, NAME)                                                   \
    layout(set = S, binding = 0) uniform NAME##UBO {                           \
        vec4  baseColor;                                                       \
        float metallic;                                                        \
        float roughness;                                                       \
        float aoStrength;                                                      \
        float normalStrength;                                                  \
        vec4  emissive;                                                        \
        float heightScale;                                                     \
        float alphaCutoff;                                                     \
        float uvTiling;                                                        \
        int   mapFlags;                                                        \
    } NAME;                                                                    \
    layout(set = S, binding = 1) uniform sampler2D NAME##Albedo;               \
    layout(set = S, binding = 2) uniform sampler2D NAME##Normal;               \
    layout(set = S, binding = 3) uniform sampler2D NAME##Rough;                \
    layout(set = S, binding = 4) uniform sampler2D NAME##Metal;                \
    layout(set = S, binding = 5) uniform sampler2D NAME##AO;                   \
    layout(set = S, binding = 7) uniform sampler2D NAME##Emissive;

LAYER_BLOCK(2, m0)
LAYER_BLOCK(3, m1)
LAYER_BLOCK(4, m2)
LAYER_BLOCK(5, m3)

layout(push_constant) uniform PushConstants {
    mat4 model;
    int renderMode;
    int hasNormalMap;
    vec4 baseColor;      // x=couche1<, y=couche2<, z=couche3<, w=seuil pente
    float metallic;
    float roughness;
} pc;

#define HAS_ALBEDO    1
#define HAS_NORMAL    2
#define HAS_ROUGHNESS 4
#define HAS_METALLIC  8
#define HAS_AO        16
#define HAS_EMISSIVE  64

// Echantillonne une couche et accumule sa contribution, ponderee par w.
void addLayer(float w,
              sampler2D sa, sampler2D sn, sampler2D sr, sampler2D sm, sampler2D so, sampler2D se,
              vec4 baseCol, vec4 emis, float metal, float rough, float aoS, float nrmS, float tiling, int flags,
              inout vec3 albedo, inout vec3 tn, inout float outRough,
              inout float outMetal, inout float outAO, inout vec3 outEm) {
    if (w <= 0.0001) return;

    vec2 uv = fragTexCoord * max(tiling, 0.0001);

    albedo += w * baseCol.rgb * (((flags & HAS_ALBEDO) != 0) ? texture(sa, uv).rgb : vec3(1.0));

    outRough += w * rough * (((flags & HAS_ROUGHNESS) != 0) ? texture(sr, uv).r : 1.0);
    outMetal += w * metal * (((flags & HAS_METALLIC)  != 0) ? texture(sm, uv).r : 1.0);
    outAO    += w * (((flags & HAS_AO) != 0) ? mix(1.0, texture(so, uv).r, aoS) : 1.0);
    outEm += w * emis.rgb * emis.a * (((flags & HAS_EMISSIVE) != 0) ? texture(se, uv).rgb : vec3(1.0));

    // Normales melangees en espace TANGENT : une seule TBN a la fin.
    vec3 n = ((flags & HAS_NORMAL) != 0)
             ? (texture(sn, uv).rgb * 2.0 - 1.0) * vec3(nrmS, nrmS, 1.0)
             : vec3(0.0, 0.0, 1.0);
    tn += w * n;
}

// 1.0 = en pleine lumiere, 0.0 = totalement dans l'ombre.
// Moyenne sur 3x3 texels (PCF) pour adoucir les bords en escalier.
float shadowFactor(vec3 worldPos, vec3 N, vec3 L) {
    vec4 lp = ubo.lightSpace * vec4(worldPos, 1.0);
    vec3 proj = lp.xyz / lp.w;

    vec2 uv = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0)
        return 1.0;                       // hors de la zone couverte

    // Biais proportionnel a l'inclinaison : les surfaces rasantes en ont plus besoin.
    float bias = max(0.0015 * (1.0 - dot(N, L)), 0.0004);

    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float sum = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float d = texture(shadowMap, uv + vec2(x, y) * texel).r;
            sum += (proj.z - bias > d) ? 0.0 : 1.0;
        }
    }
    return sum / 9.0;
}

void main() {
    // ---------- Poids des 4 couches (altitude Z-up + pente) ----------
    float h = fragWorldPos.z;
    vec3  Ng = normalize(fragNormal);
    float slope = 1.0 - Ng.z;                 // 0 = plat, 1 = vertical

    // Largeur de fondu entre couches (pc.roughness recycle : petit = tranche net).
    float bw = max(pc.roughness, 0.01);
    float t1 = smoothstep(pc.baseColor.x - bw, pc.baseColor.x + bw, h);
    float t2 = smoothstep(pc.baseColor.y - bw, pc.baseColor.y + bw, h);
    float t3 = smoothstep(pc.baseColor.z - bw, pc.baseColor.z + bw, h);

    // Poids explicites : TOUT (couleur, normale, rugosite, AO...) utilise les memes.
    float w0 = (1.0 - t1);
    float w1 = t1 * (1.0 - t2);
    float w2 = t2 * (1.0 - t3);
    float w3 = t3;

    // Pentes raides -> couche 2 (roche), sauf sur les sommets (couche 3).
    float rockSlope = smoothstep(pc.baseColor.w - 0.1, pc.baseColor.w + 0.1, slope) * (1.0 - t3);
    w0 *= (1.0 - rockSlope);
    w1 *= (1.0 - rockSlope);
    w3 *= (1.0 - rockSlope);
    w2 = w2 * (1.0 - rockSlope) + rockSlope;

    float wsum = max(w0 + w1 + w2 + w3, 0.0001);
    w0 /= wsum; w1 /= wsum; w2 /= wsum; w3 /= wsum;

    // ---------- Melange des 4 materiaux ----------
    vec3  baseColor  = vec3(0.0);
    vec3  tn         = vec3(0.0);
    float mRoughness = 0.0;
    float mMetallic  = 0.0;
    float mAO        = 0.0;
    vec3  mEmissive  = vec3(0.0);

    addLayer(w0, m0Albedo, m0Normal, m0Rough, m0Metal, m0AO, m0Emissive,
             m0.baseColor, m0.emissive, m0.metallic, m0.roughness, m0.aoStrength, m0.normalStrength,
             m0.uvTiling, m0.mapFlags, baseColor, tn, mRoughness, mMetallic, mAO, mEmissive);
    addLayer(w1, m1Albedo, m1Normal, m1Rough, m1Metal, m1AO, m1Emissive,
             m1.baseColor, m1.emissive, m1.metallic, m1.roughness, m1.aoStrength, m1.normalStrength,
             m1.uvTiling, m1.mapFlags, baseColor, tn, mRoughness, mMetallic, mAO, mEmissive);
    addLayer(w2, m2Albedo, m2Normal, m2Rough, m2Metal, m2AO, m2Emissive,
             m2.baseColor, m2.emissive, m2.metallic, m2.roughness, m2.aoStrength, m2.normalStrength,
             m2.uvTiling, m2.mapFlags, baseColor, tn, mRoughness, mMetallic, mAO, mEmissive);
    addLayer(w3, m3Albedo, m3Normal, m3Rough, m3Metal, m3AO, m3Emissive,
             m3.baseColor, m3.emissive, m3.metallic, m3.roughness, m3.aoStrength, m3.normalStrength,
             m3.uvTiling, m3.mapFlags, baseColor, tn, mRoughness, mMetallic, mAO, mEmissive);

    vec3 normal = Ng;
    vec3 T = fragTangent;
    if (length(T) > 0.01 && length(tn) > 0.01) {
        T = normalize(T - dot(T, Ng) * Ng);
        vec3 B = cross(Ng, T);
        normal = normalize(mat3(T, B, Ng) * normalize(tn));
    }

    // Mode 5 : damier de debug cale sur le monde (1 unite par carreau).
    if (fragRenderMode == 5) {
        vec2 wxy = fragWorldPos.xy;
        float checker = mod(floor(wxy.x) + floor(wxy.y), 2.0);
        baseColor = (checker < 1.0) ? vec3(0.35) : vec3(0.85);
        normal = Ng;
        mRoughness = 0.9;
        mMetallic = 0.0;
        mAO = 1.0;
        mEmissive = vec3(0.0);
    }

    // ---------- PBR : Cook-Torrance (metallic-roughness) ----------
    vec3  N = normalize(normal);
    vec3  V = normalize(ubo.cameraPosition - fragWorldPos);
    vec3  L = normalize(-ubo.lightDirection);
    vec3  H = normalize(V + L);

    float metallic  = clamp(mMetallic, 0.0, 1.0);
    float roughness = clamp(mRoughness, 0.04, 1.0);
    float a  = roughness * roughness;
    float a2 = a * a;

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0001);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / (3.14159265 * d * d);

    float k  = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float gV = NdotV / (NdotV * (1.0 - k) + k);
    float gL = NdotL / (NdotL * (1.0 - k) + k);
    float G  = gV * gL;

    vec3 F0 = mix(vec3(0.04), baseColor, metallic);
    vec3 F  = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * baseColor / 3.14159265;

    vec3 radiance = ubo.lightColor * ubo.lightIntensity;
    float shadow = shadowFactor(fragWorldPos, N, L);
    vec3 result = (diffuse + specular) * radiance * NdotL * shadow;

    // ---- Environnement : carte equirectangulaire (set 1) ----
    const float PI_ENV = 3.14159265;
    vec3 Renv = reflect(-V, N);

    vec2 uvR = vec2(atan(Renv.y, Renv.x) / (2.0 * PI_ENV) + 0.5,
                    0.5 - asin(clamp(Renv.z, -1.0, 1.0)) / PI_ENV);
    vec2 uvN = vec2(atan(N.y, N.x) / (2.0 * PI_ENV) + 0.5,
                    0.5 - asin(clamp(N.z, -1.0, 1.0)) / PI_ENV);

    const float ENV_MAX_LOD = 9.0;
    vec3 envReflect = textureLod(envSampler, uvR, roughness * ENV_MAX_LOD).rgb;
    vec3 irradiance = textureLod(envSampler, uvN, ENV_MAX_LOD).rgb;

    vec3 kDamb = (vec3(1.0) - F0) * (1.0 - metallic);
    vec3 ambientDiffuse = kDamb * baseColor * irradiance;

    vec3 Fenv = F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - NdotV, 5.0);
    vec3 ambientSpecular = envReflect * Fenv;

    ambientDiffuse  *= mAO;
    ambientSpecular *= mAO;

    float envI = max(ubo.shininess, 0.0);
    // L'ombre assombrit aussi l'ambiant, sinon elle parait toujours trop legere.
    float shadowAmb = mix(1.0, shadow, ubo.shadowStrength);
    result += (ambientDiffuse * ubo.ambientStrength * 3.0 + ambientSpecular * envI) * shadowAmb;
    result += mEmissive;

    result *= ubo.exposure;
    result = result / (result + vec3(1.0));
    outColor = vec4(result, 1.0);
}
