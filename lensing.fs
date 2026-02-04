#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 resolution;
uniform vec2 blackHolePos;
uniform float blackHoleRadius;
uniform float time;

// Schwarzschild metric parameters (normalized units where Rs = 1)
const float RS_SCALE = 1.0;
const float PHOTON_SPHERE = 1.5;  // Unstable photon orbit at r = 1.5 Rs
const float EINSTEIN_RING = 2.6;  // Critical impact parameter for lensing

// ===== FXAA (Fast Approximate Anti-Aliasing) =====
// Nvidia's FXAA 3.11 algorithm - edge detection based on luminance gradient
// Reduces aliasing without geometry information, ideal for post-process pipeline
const float FXAA_SPAN_MAX = 8.0;
const float FXAA_REDUCE_MUL = 1.0 / 8.0;
const float FXAA_REDUCE_MIN = 1.0 / 128.0;

vec3 applyFXAA(sampler2D tex, vec2 uv, vec2 texelSize) {
    // Sample 5-tap pattern: center + 4 diagonal corners
    vec3 rgbNW = texture(tex, uv + vec2(-1.0, -1.0) * texelSize).rgb;
    vec3 rgbNE = texture(tex, uv + vec2(1.0, -1.0) * texelSize).rgb;
    vec3 rgbSW = texture(tex, uv + vec2(-1.0, 1.0) * texelSize).rgb;
    vec3 rgbSE = texture(tex, uv + vec2(1.0, 1.0) * texelSize).rgb;
    vec3 rgbM  = texture(tex, uv).rgb;

    // Perceptual luminance weights (Rec. 709 standard)
    vec3 luma = vec3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM  = dot(rgbM, luma);

    // Local contrast range for edge detection threshold
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // Gradient direction perpendicular to detected edge
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    // Normalize direction with minimum threshold to avoid division artifacts
    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    // Clamp blur direction to maximum span
    dir = min(vec2(FXAA_SPAN_MAX), max(vec2(-FXAA_SPAN_MAX), dir * rcpDirMin)) * texelSize;

    // Two-tap blur along edge direction
    vec3 rgbA = 0.5 * (texture(tex, uv + dir * (1.0/3.0 - 0.5)).rgb +
                       texture(tex, uv + dir * (2.0/3.0 - 0.5)).rgb);
    // Four-tap blur for wider coverage
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(tex, uv + dir * -0.5).rgb +
                                      texture(tex, uv + dir * 0.5).rgb);

    float lumaB = dot(rgbB, luma);

    // Reject wider blur if it samples beyond the local contrast range (preserves edges)
    if (lumaB < lumaMin || lumaB > lumaMax) {
        return rgbA;
    }
    return rgbB;
}

// Legacy function for reference - not currently used
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
    float dist = length(delta);  // Impact parameter b (distance from optical axis)
    float rs = blackHoleRadius * RS_SCALE;

    // ===== GRAVITATIONAL LENSING =====
    // Approximates light deflection using weak-field Schwarzschild metric
    // True deflection angle: θ = 4GM/(c²b) = 2Rs/b
    vec2 distortedUV = uv;

    if (dist > rs * 0.1) {
        // Deflection magnitude falls off as 1/b² (simplified from exact solution)
        // Added softening term (rs * 0.1) prevents singularity at center
        float deflection = rs * rs / (dist * dist + rs * 0.1);
        vec2 dir = normalize(delta);
        dir.x /= aspect;

        // Strong-field correction near photon sphere
        // Light paths here can wrap partially around the black hole
        float wrapFactor = 1.0;
        if (dist < rs * 3.0 && dist > rs) {
            wrapFactor = 1.0 + 0.5 * exp(-(dist - rs * 1.5) * 5.0);
        }

        // Negative direction: light bends toward mass, so we sample "outward"
        distortedUV = uv - dir * deflection * wrapFactor * 1.5;
    }

    // Apply FXAA to reduce aliasing artifacts from distortion sampling
    vec3 aaColor = applyFXAA(texture0, distortedUV, texelSize);
    vec4 texColor = vec4(aaColor, 1.0);

    // ===== BLOOM (HDR Glow Simulation) =====
    // Approximates light scattering in camera lens/eye for bright sources
    // Real bloom would use separable Gaussian blur for efficiency
    vec3 bloom = vec3(0.0);
    float bloomSamples = 12.0;
    float bloomRadius = 0.025;
    for (float i = 0.0; i < bloomSamples; i++) {
        float angle = i / bloomSamples * 6.28318;
        vec2 offset = vec2(cos(angle), sin(angle)) * bloomRadius;
        vec3 sampleColor = texture(texture0, distortedUV + offset).rgb;
        // Threshold filter: only bright pixels contribute to bloom
        float brightness = dot(sampleColor, vec3(0.299, 0.587, 0.114));
        if (brightness > 0.4) {
            bloom += sampleColor * (brightness - 0.4) * 0.5;
        }
    }
    texColor.rgb += bloom / bloomSamples;

    // ===== EVENT HORIZON SHADOW =====
    // Region where all light paths terminate at singularity
    // Shadow edge is actually at ~2.6 Rs (photon capture radius) not Rs
    float shadow = 1.0;
    float eventHorizon = rs * 1.0;

    if (dist < eventHorizon) {
        shadow = 0.0;
    } else if (dist < eventHorizon * 1.3) {
        // Smooth falloff prevents hard edge artifacts
        shadow = smoothstep(eventHorizon, eventHorizon * 1.3, dist);
        shadow *= shadow;  // Quadratic falloff for softer transition
    }

    // ===== INNER ACCRETION GLOW =====
    // Represents emission from innermost stable orbit region
    // Color temperature ~10^7 K would be X-ray, shifted to visible for effect
    vec3 warmGlow = vec3(1.0, 0.6, 0.3);

    float innerGlow = 0.0;
    if (dist > rs && dist < rs * 2.5) {
        // Exponential falloff models optically thin emission
        innerGlow = exp(-(dist - rs) * 4.0) * 0.2;
    }

    // ===== COMPOSITING =====
    texColor.rgb *= shadow;
    texColor.rgb += warmGlow * innerGlow * shadow;

    // Enforce pure black inside event horizon (no light escape)
    if (dist < rs * 0.9) {
        texColor.rgb = vec3(0.0);
    }

    // ===== CHROMATIC ABERRATION =====
    // Simulates wavelength-dependent refraction near horizon
    // Red light deflects slightly less than blue in strong gravity
    if (dist > rs * 0.9 && dist < rs * 1.5) {
        float chromatic = smoothstep(rs * 0.9, rs * 1.2, dist);
        texColor.r *= 1.0 + (1.0 - chromatic) * 0.1;
        texColor.b *= 1.0 - (1.0 - chromatic) * 0.05;
    }

    // ===== TONE MAPPING & COLOR GRADING =====
    // Local contrast enhancement near black hole
    float contrastBoost = 1.0 + 0.3 * exp(-dist * 5.0);
    texColor.rgb = pow(texColor.rgb, vec3(1.0 / contrastBoost));

    // Reinhard tone mapping: maps HDR to displayable range
    // Preserves highlight detail better than simple clamp
    texColor.rgb = texColor.rgb / (texColor.rgb + vec3(1.0));

    // Gamma correction for sRGB display (2.2 standard)
    // 1.2 multiplier adds slight exposure boost
    texColor.rgb = pow(texColor.rgb, vec3(1.0 / 2.2)) * 1.2;

    finalColor = texColor;
}
