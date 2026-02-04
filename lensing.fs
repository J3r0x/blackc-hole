#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 resolution;
uniform vec2 blackHolePos;
uniform float blackHoleRadius;
uniform float time;

// Constantes físicas escaladas
const float RS_SCALE = 1.0;
const float PHOTON_SPHERE = 1.5;
const float EINSTEIN_RING = 2.6;

// ===== FXAA IMPLEMENTATION =====
const float FXAA_SPAN_MAX = 8.0;
const float FXAA_REDUCE_MUL = 1.0 / 8.0;
const float FXAA_REDUCE_MIN = 1.0 / 128.0;

vec3 applyFXAA(sampler2D tex, vec2 uv, vec2 texelSize) {
    vec3 rgbNW = texture(tex, uv + vec2(-1.0, -1.0) * texelSize).rgb;
    vec3 rgbNE = texture(tex, uv + vec2(1.0, -1.0) * texelSize).rgb;
    vec3 rgbSW = texture(tex, uv + vec2(-1.0, 1.0) * texelSize).rgb;
    vec3 rgbSE = texture(tex, uv + vec2(1.0, 1.0) * texelSize).rgb;
    vec3 rgbM  = texture(tex, uv).rgb;

    vec3 luma = vec3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM  = dot(rgbM, luma);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    dir = min(vec2(FXAA_SPAN_MAX), max(vec2(-FXAA_SPAN_MAX), dir * rcpDirMin)) * texelSize;

    vec3 rgbA = 0.5 * (texture(tex, uv + dir * (1.0/3.0 - 0.5)).rgb +
                       texture(tex, uv + dir * (2.0/3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(tex, uv + dir * -0.5).rgb +
                                      texture(tex, uv + dir * 0.5).rgb);

    float lumaB = dot(rgbB, luma);

    if (lumaB < lumaMin || lumaB > lumaMax) {
        return rgbA;
    }
    return rgbB;
}

// Función para obtener color con distorsión gravitacional
vec4 getDistortedColor(vec2 uv, vec2 center, float aspect, float rs) {
    vec2 delta = uv - center;
    delta.x *= aspect;
    float dist = length(delta);

    vec2 distortedUV = uv;

    if (dist > rs * 0.1) {
        float deflection = rs * rs / (dist * dist + rs * 0.1);
        vec2 dir = normalize(delta);
        dir.x /= aspect;

        float wrapFactor = 1.0;
        if (dist < rs * 3.0 && dist > rs) {
            wrapFactor = 1.0 + 0.5 * exp(-(dist - rs * 1.5) * 5.0);
        }

        distortedUV = uv - dir * deflection * wrapFactor * 1.5;
    }

    return texture(texture0, distortedUV);
}

void main() {
    vec2 uv = fragTexCoord;
    vec2 center = blackHolePos;
    vec2 texelSize = 1.0 / resolution;

    float aspect = resolution.x / resolution.y;
    vec2 delta = uv - center;
    delta.x *= aspect;
    float dist = length(delta);
    float rs = blackHoleRadius * RS_SCALE;

    // ===== DISTORSIÓN GRAVITACIONAL CON SUPERSAMPLING =====
    vec2 distortedUV = uv;

    if (dist > rs * 0.1) {
        float deflection = rs * rs / (dist * dist + rs * 0.1);
        vec2 dir = normalize(delta);
        dir.x /= aspect;

        float wrapFactor = 1.0;
        if (dist < rs * 3.0 && dist > rs) {
            wrapFactor = 1.0 + 0.5 * exp(-(dist - rs * 1.5) * 5.0);
        }

        distortedUV = uv - dir * deflection * wrapFactor * 1.5;
    }

    // ===== APLICAR FXAA A LA TEXTURA DISTORSIONADA =====
    vec3 aaColor = applyFXAA(texture0, distortedUV, texelSize);
    vec4 texColor = vec4(aaColor, 1.0);

    // ===== BLOOM SIMPLE (resplandor de zonas brillantes) =====
    vec3 bloom = vec3(0.0);
    float bloomSamples = 12.0;
    float bloomRadius = 0.025;
    for (float i = 0.0; i < bloomSamples; i++) {
        float angle = i / bloomSamples * 6.28318;
        vec2 offset = vec2(cos(angle), sin(angle)) * bloomRadius;
        vec3 sampleColor = texture(texture0, distortedUV + offset).rgb;
        // Solo tomar partes brillantes
        float brightness = dot(sampleColor, vec3(0.299, 0.587, 0.114));
        if (brightness > 0.4) {
            bloom += sampleColor * (brightness - 0.4) * 0.5;
        }
    }
    texColor.rgb += bloom / bloomSamples;

    // ===== SOMBRA DEL AGUJERO NEGRO =====
    float shadow = 1.0;
    float eventHorizon = rs * 1.0;

    if (dist < eventHorizon) {
        shadow = 0.0;
    } else if (dist < eventHorizon * 1.3) {
        shadow = smoothstep(eventHorizon, eventHorizon * 1.3, dist);
        shadow *= shadow;
    }

    // ===== EFECTOS DE BRILLO =====
    vec3 warmGlow = vec3(1.0, 0.6, 0.3); // Más naranja

    // Solo un resplandor suave cerca del horizonte, sin anillos
    float innerGlow = 0.0;
    if (dist > rs && dist < rs * 2.5) {
        innerGlow = exp(-(dist - rs) * 4.0) * 0.2;
    }

    // ===== COMPOSICIÓN FINAL =====
    texColor.rgb *= shadow;
    texColor.rgb += warmGlow * innerGlow * shadow;

    if (dist < rs * 0.9) {
        texColor.rgb = vec3(0.0);
    }

    // Aberración cromática
    if (dist > rs * 0.9 && dist < rs * 1.5) {
        float chromatic = smoothstep(rs * 0.9, rs * 1.2, dist);
        texColor.r *= 1.0 + (1.0 - chromatic) * 0.1;
        texColor.b *= 1.0 - (1.0 - chromatic) * 0.05;
    }

    // ===== TONEMAPPING Y AJUSTES FINALES =====
    // Aumentar contraste cerca del agujero negro
    float contrastBoost = 1.0 + 0.3 * exp(-dist * 5.0);
    texColor.rgb = pow(texColor.rgb, vec3(1.0 / contrastBoost));

    // Tonemapping simple (evita clipping de blancos)
    texColor.rgb = texColor.rgb / (texColor.rgb + vec3(1.0));

    // Gamma correction
    texColor.rgb = pow(texColor.rgb, vec3(1.0 / 2.2)) * 1.2;

    finalColor = texColor;
}
