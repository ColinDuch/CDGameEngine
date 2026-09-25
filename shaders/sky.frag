#version 450

layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D envSampler;

layout(push_constant) uniform SkyPush {
    mat4 invViewProj;
    float exposure;
} pc;

void main() {
    // Reconstruit la direction du rayon a partir de deux points deprojetes.
    vec4 pNear = pc.invViewProj * vec4(inNdc, 0.0, 1.0);
    vec4 pFar  = pc.invViewProj * vec4(inNdc, 1.0, 1.0);
    vec3 dir = normalize(pFar.xyz / pFar.w - pNear.xyz / pNear.w);

    const float PI = 3.14159265;
    vec2 uv = vec2(atan(dir.y, dir.x) / (2.0 * PI) + 0.5,
                   0.5 - asin(clamp(dir.z, -1.0, 1.0)) / PI);

    // Meme tone mapping que les objets, sinon le fond parait plus lumineux que son reflet.
    // atan() a une couture ou u saute de ~1 a ~0 : dFdx explose et le GPU
    // choisit le plus petit mip -> bande floue verticale. On corrige les derivees.
    vec2 ddx = dFdx(uv);
    vec2 ddy = dFdy(uv);
    ddx.x -= round(ddx.x);
    ddy.x -= round(ddy.x);
    vec3 col = textureGrad(envSampler, uv, ddx, ddy).rgb;
    col *= pc.exposure;
    col = col / (col + vec3(1.0));
    outColor = vec4(col, 1.0);
}