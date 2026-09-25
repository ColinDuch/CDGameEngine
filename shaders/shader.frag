#version 450

layout(binding = 0) uniform UniformBufferObject {
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

layout(push_constant) uniform PushConstants {
    mat4 model;
    int renderMode;
    int hasNormalMap;
    vec4 baseColor;
    float metallic;
    float roughness;
    float worldUvScale;
} pc;

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform sampler2D normalSampler;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) flat in int fragRenderMode;
layout(location = 4) in vec3 fragTangent;

layout(location = 0) out vec4 outColor;
layout(set = 1, binding = 0) uniform sampler2D envSampler;
layout(set = 1, binding = 1) uniform sampler2D shadowMap;

// ---------------------------------------------------------------------------
// SET 2 : materiau PBR partage (MaterialAsset). L'ordre des bindings doit
// correspondre EXACTEMENT a l'enum MapSlot du C++.
// ---------------------------------------------------------------------------
layout(set = 2, binding = 0) uniform MaterialParamsUBO {
    vec4  baseColor;        // rgb = teinte, a = opacite
    float metallic;
    float roughness;
    float aoStrength;
    float normalStrength;
    vec4  emissive;         // rgb = couleur, a = intensite
    float heightScale;
    float alphaCutoff;
    float uvTiling;
    int   mapFlags;         // bit i = carte i chargee
} mat;

layout(set = 2, binding = 1) uniform sampler2D mapAlbedo;
layout(set = 2, binding = 2) uniform sampler2D mapNormal;
layout(set = 2, binding = 3) uniform sampler2D mapRoughness;
layout(set = 2, binding = 4) uniform sampler2D mapMetallic;
layout(set = 2, binding = 5) uniform sampler2D mapAO;
layout(set = 2, binding = 6) uniform sampler2D mapHeight;
layout(set = 2, binding = 7) uniform sampler2D mapEmissive;
layout(set = 2, binding = 8) uniform sampler2D mapOpacity;

#define HAS_ALBEDO    1
#define HAS_NORMAL    2
#define HAS_ROUGHNESS 4
#define HAS_METALLIC  8
#define HAS_AO        16
#define HAS_HEIGHT    32
#define HAS_EMISSIVE  64
#define HAS_OPACITY   128

// Normale perturbee par une carte, dans l'espace tangent.
vec3 applyNormalMap(sampler2D smp, vec2 uv, float strength) {
    vec3 N = normalize(fragNormal);
    vec3 T = fragTangent;
    if (length(T) < 0.01) return N;

    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);

    vec3 tn = texture(smp, uv).rgb * 2.0 - 1.0;
    tn.xy *= strength;
    return normalize(mat3(T, B, N) * normalize(tn));
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

// Projection triplanaire : les UV viennent de la POSITION MONDE, pas du maillage.
// La texture ne s'etire donc jamais, quelle que soit l'echelle de l'objet.
vec3 sampleAlbedoWorld() {
    vec3 n = abs(normalize(fragNormal));
    vec3 w = n / max(n.x + n.y + n.z, 1e-5);   // poids des trois projections

    vec3 p = fragWorldPos / pc.worldUvScale;   // scale = taille reelle d'une tuile
    vec3 cx = texture(texSampler, p.zy).rgb;   // face regardant X
    vec3 cy = texture(texSampler, p.xz).rgb;   // face regardant Y
    vec3 cz = texture(texSampler, p.xy).rgb;   // face regardant Z

    return cx * w.x + cy * w.y + cz * w.z;
}

void main() {
    if (fragRenderMode == 3) {
        outColor = vec4(0.4, 0.4, 0.4, 1.0);
        return;
    }

    vec3 baseColor;
    vec3 normal;

    // Valeurs de matiere : par defaut celles des push constants (modes hérités).
    float mMetallic  = pc.metallic;
    float mRoughness = pc.roughness;
    float mAO        = 1.0;
    vec3  mEmissive  = vec3(0.0);
    float mAlpha     = 1.0;

    if (fragRenderMode == 2) {
        vec3 n = abs(normalize(fragNormal));
        vec2 uv;
        if (n.x >= n.y && n.x >= n.z) {
            uv = fragWorldPos.yz;
        } else if (n.y >= n.x && n.y >= n.z) {
            uv = fragWorldPos.xz;
        } else {
            uv = fragWorldPos.xy;
        }
        float scale = 4.0;
        vec2 c = floor(uv * scale);
        float checker = mod(c.x + c.y, 2.0);
        baseColor = (checker < 1.0) ? vec3(0.05, 0.05, 0.05) : vec3(1.0, 0.0, 1.0);
        normal = normalize(fragNormal);
    } else if (fragRenderMode == 1) {
        float scale = 8.0;
        vec2 c = floor(fragTexCoord * scale);
        float checker = mod(c.x + c.y, 2.0);
        baseColor = (checker < 1.0) ? vec3(0.35) : vec3(0.85);
        normal = normalize(fragNormal);
    } else if (fragRenderMode == 4) {
        baseColor = pc.baseColor.rgb;   // couleur Kd du materiau
        normal = normalize(fragNormal);
    } else if (fragRenderMode == 5) {
        // Damier aligne sur le monde (Z-up, plan XY) : cale sur la grille, 1 unite/carreau.
        vec2 w = fragWorldPos.xy;
        float checker = mod(floor(w.x) + floor(w.y), 2.0);
        baseColor = (checker < 1.0) ? vec3(0.35) : vec3(0.85);
        normal = normalize(fragNormal);
    } else if (fragRenderMode == 6) {
        float h = fragWorldPos.z;
        float slope = 1.0 - normalize(fragNormal).z;

        float sandTop  = pc.baseColor.x;
        float grassTop = pc.baseColor.y;
        float rockTop  = pc.baseColor.z;
        float slopeThr = pc.baseColor.w;

        vec3 sand  = vec3(0.85, 0.74, 0.42);
        vec3 grass = vec3(0.33, 0.55, 0.22);
        vec3 rock  = vec3(0.46, 0.44, 0.41);
        vec3 snow  = vec3(0.97, 0.98, 1.00);

        float t1 = smoothstep(sandTop  - 1.0, sandTop  + 1.0, h);
        float t2 = smoothstep(grassTop - 1.0, grassTop + 1.0, h);
        float t3 = smoothstep(rockTop  - 1.0, rockTop  + 1.0, h);

        vec3 tint = mix(sand, grass, t1);
        tint = mix(tint, rock, t2);
        tint = mix(tint, snow, t3);

        float rockSlope = smoothstep(slopeThr - 0.1, slopeThr + 0.1, slope);
        tint = mix(tint, rock, rockSlope * (1.0 - t3));

        vec3 tex = texture(texSampler, fragTexCoord).rgb;
        baseColor = tint * (0.55 + 0.45 * tex);   // teinte DOMINANTE, texture en modulation
        normal = normalize(fragNormal);
    } else if (fragRenderMode == 7) {
        // ---------- MATERIAU PBR PARTAGE (set 2) ----------
        vec2 uv = fragTexCoord * mat.uvTiling;

        // Parallax : decale les UV selon le relief, vu de biais.
        if ((mat.mapFlags & HAS_HEIGHT) != 0 && mat.heightScale > 0.0) {
            vec3 Nn = normalize(fragNormal);
            vec3 T  = fragTangent;
            if (length(T) > 0.01) {
                T = normalize(T - dot(T, Nn) * Nn);
                vec3 B = cross(Nn, T);
                vec3 Vw = normalize(ubo.cameraPosition - fragWorldPos);
                vec3 Vt = vec3(dot(Vw, T), dot(Vw, B), dot(Vw, Nn));
                float h = texture(mapHeight, uv).r;
                uv -= (Vt.xy / max(Vt.z, 0.2)) * (h * mat.heightScale);
            }
        }

        vec4 alb = texture(mapAlbedo, uv);
        baseColor = mat.baseColor.rgb *
                    (((mat.mapFlags & HAS_ALBEDO) != 0) ? alb.rgb : vec3(1.0));

        // Cartes separees en niveaux de gris -> canal rouge.
        // (Une texture ORM packee utiliserait .g pour la rugosite et .b pour le metal.)
        mRoughness = mat.roughness *
                     (((mat.mapFlags & HAS_ROUGHNESS) != 0) ? texture(mapRoughness, uv).r : 1.0);
        mMetallic  = mat.metallic *
                     (((mat.mapFlags & HAS_METALLIC) != 0) ? texture(mapMetallic, uv).r : 1.0);

        mAO = ((mat.mapFlags & HAS_AO) != 0)
              ? mix(1.0, texture(mapAO, uv).r, mat.aoStrength) : 1.0;

        mEmissive = mat.emissive.rgb * mat.emissive.a *
                    (((mat.mapFlags & HAS_EMISSIVE) != 0) ? texture(mapEmissive, uv).rgb : vec3(1.0));

        mAlpha = mat.baseColor.a *
                 (((mat.mapFlags & HAS_OPACITY) != 0) ? texture(mapOpacity, uv).r
                  : (((mat.mapFlags & HAS_ALBEDO) != 0) ? alb.a : 1.0));

        normal = ((mat.mapFlags & HAS_NORMAL) != 0)
                 ? applyNormalMap(mapNormal, uv, mat.normalStrength)
                 : normalize(fragNormal);
    } else {
        baseColor = (pc.worldUvScale > 0.0) ? sampleAlbedoWorld()
                                            : texture(texSampler, fragTexCoord).rgb;

        if (pc.hasNormalMap == 1) {
            normal = applyNormalMap(normalSampler, fragTexCoord, 1.0);
        } else {
            normal = normalize(fragNormal);
        }
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

    // Distribution des microfacettes (GGX / Trowbridge-Reitz)
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / (3.14159265 * d * d);

    // Geometrie (Smith + Schlick-GGX)
    float k  = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float gV = NdotV / (NdotV * (1.0 - k) + k);
    float gL = NdotL / (NdotL * (1.0 - k) + k);
    float G  = gV * gL;

    // Fresnel (Schlick) : 0.04 pour les dielectriques, l'albedo pour les metaux
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);
    vec3 F  = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

    // Les metaux n'ont pas de diffus
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * baseColor / 3.14159265;

    vec3 radiance = ubo.lightColor * ubo.lightIntensity;
    float shadow = shadowFactor(fragWorldPos, N, L);
    vec3 result = (diffuse + specular) * radiance * NdotL * shadow;

    // ---- Environnement : carte equirectangulaire (set 1) ----
    const float PI_ENV = 3.14159265;
    vec3 Renv = reflect(-V, N);

    // Direction -> UV equirectangulaire (monde Z-up)
    vec2 uvR = vec2(atan(Renv.y, Renv.x) / (2.0 * PI_ENV) + 0.5,
                    0.5 - asin(clamp(Renv.z, -1.0, 1.0)) / PI_ENV);
    vec2 uvN = vec2(atan(N.y, N.x) / (2.0 * PI_ENV) + 0.5,
                    0.5 - asin(clamp(N.z, -1.0, 1.0)) / PI_ENV);

    // Mipmaps : un niveau eleve = moyenne floue du ciel.
    const float ENV_MAX_LOD = 9.0;
    vec3 envReflect = textureLod(envSampler, uvR, roughness * ENV_MAX_LOD).rgb;
    vec3 irradiance = textureLod(envSampler, uvN, ENV_MAX_LOD).rgb;
    vec3 envColor = envReflect;   // le flou est deja dans le niveau de mip

    vec3 kDamb = (vec3(1.0) - F0) * (1.0 - metallic);
    vec3 ambientDiffuse = kDamb * baseColor * irradiance;

    vec3 Fenv = F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - NdotV, 5.0);
    vec3 ambientSpecular = envColor * Fenv;

    // L'occlusion ambiante n'assombrit QUE l'ambiant, jamais la lumiere directe.
    ambientDiffuse  *= mAO;
    ambientSpecular *= mAO;

    // Ambiant diffus = lumiere de remplissage ; reflet = intensite d'environnement.
    // (ubo.shininess est recycle en "intensite environnement" : il ne servait plus depuis le PBR)
    float envI = max(ubo.shininess, 0.0);
    // L'ombre assombrit aussi l'ambiant, sinon elle parait toujours trop legere.
    float shadowAmb = mix(1.0, shadow, ubo.shadowStrength);
    result += (ambientDiffuse * ubo.ambientStrength * 3.0 + ambientSpecular * envI) * shadowAmb;

    // L'emissif s'ajoute en dernier : il ne depend d'aucune lumiere.
    result += mEmissive;

    // Exposition puis tone mapping (Reinhard)
    result *= ubo.exposure;
    result = result / (result + vec3(1.0));
    outColor = vec4(result, mAlpha);
}
