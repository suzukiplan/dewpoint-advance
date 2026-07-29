#version 450

layout(set = 0, binding = 0) uniform sampler2D u_texture;
layout(push_constant) uniform ShaderConstants {
    vec2 inputSize;
    vec2 outputSize;
    int crtEnabled;
} constants;
layout(location = 0) in vec2 v_texCoord;
layout(location = 0) out vec4 fragmentColor;

const float PI = 3.14159265358979323846;
const float CRT_GAMMA = 2.1;
const float DISPLAY_GAMMA = 2.2;
const float CURVATURE = 0.02;
const float SCANLINE_WEIGHT = 0.30;
const float DOT_MASK_STRENGTH = 0.30;
const vec2 CONTENT_SCALE = vec2(1.0, 1.04);

vec2 warp(vec2 coord)
{
    vec2 centered = coord * 2.0 - 1.0;
    centered = vec2(
        centered.x * (1.0 + CURVATURE * centered.y * centered.y),
        centered.y * (1.0 + CURVATURE * centered.x * centered.x));
    return centered * 0.5 + 0.5;
}

float roundedScreenMask(vec2 coord)
{
    vec2 edgeDistance = abs(coord - vec2(0.5)) - vec2(0.475, 0.4625);
    float outsideDistance =
        length(max(edgeDistance, vec2(0.0))) +
        min(max(edgeDistance.x, edgeDistance.y), 0.0) -
        0.025;
    float antialiasWidth = 2.0 / min(constants.outputSize.x, constants.outputSize.y);
    return 1.0 - smoothstep(-antialiasWidth, antialiasWidth, outsideDistance);
}

float lanczos2(float distance)
{
    distance = abs(distance);
    if (distance < 0.00001) {
        return 1.0;
    }
    if (distance >= 2.0) {
        return 0.0;
    }
    float piDistance = PI * distance;
    return 2.0 * sin(piDistance) * sin(piDistance * 0.5) /
        (piDistance * piDistance);
}

vec3 sampleLinear(vec2 texel)
{
    vec2 coord = (texel + vec2(0.5)) / constants.inputSize;
    return pow(texture(u_texture, coord).rgb, vec3(CRT_GAMMA));
}

vec3 sampleScanline(vec2 baseTexel, float rowOffset, vec4 coefficients)
{
    vec3 color =
        sampleLinear(baseTexel + vec2(-1.0, rowOffset)) * coefficients.x +
        sampleLinear(baseTexel + vec2( 0.0, rowOffset)) * coefficients.y +
        sampleLinear(baseTexel + vec2( 1.0, rowOffset)) * coefficients.z +
        sampleLinear(baseTexel + vec2( 2.0, rowOffset)) * coefficients.w;
    return clamp(color, 0.0, 1.0);
}

vec3 scanlineBeam(float distance, vec3 color)
{
    vec3 intensity = sqrt(color);
    vec3 beamWidth =
        mix(vec3(SCANLINE_WEIGHT * 0.65), vec3(SCANLINE_WEIGHT * 1.15), intensity);
    vec3 scaledDistance = vec3(distance) / beamWidth;
    vec3 peak = mix(vec3(0.95), vec3(1.15), intensity);
    return peak * exp(-0.5 * scaledDistance * scaledDistance);
}

void main()
{
    if (constants.crtEnabled == 0) {
        fragmentColor = texture(u_texture, v_texCoord);
        return;
    }

    vec2 screenCoord = warp(v_texCoord);
    vec2 coord = (screenCoord - vec2(0.5)) * CONTENT_SCALE + vec2(0.5);
    if (coord.x < 0.0 || coord.x > 1.0 ||
        coord.y < 0.0 || coord.y > 1.0) {
        fragmentColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec2 texelPosition = coord * constants.inputSize - vec2(0.5);
    vec2 baseTexel = floor(texelPosition);
    vec2 subTexel = fract(texelPosition);

    vec4 coefficients = vec4(
        lanczos2(1.0 + subTexel.x),
        lanczos2(subTexel.x),
        lanczos2(1.0 - subTexel.x),
        lanczos2(2.0 - subTexel.x));
    coefficients /= dot(coefficients, vec4(1.0));

    vec3 currentLine = sampleScanline(baseTexel, 0.0, coefficients);
    vec3 nextLine = sampleScanline(baseTexel, 1.0, coefficients);

    float footprint = constants.inputSize.y / constants.outputSize.y;
    float sampleOffset = footprint / 3.0;
    vec3 currentWeight =
        (scanlineBeam(abs(subTexel.y - sampleOffset), currentLine) +
         scanlineBeam(subTexel.y, currentLine) +
         scanlineBeam(subTexel.y + sampleOffset, currentLine)) / 3.0;
    vec3 nextWeight =
        (scanlineBeam(abs(1.0 - subTexel.y + sampleOffset), nextLine) +
         scanlineBeam(1.0 - subTexel.y, nextLine) +
         scanlineBeam(abs(1.0 - subTexel.y - sampleOffset), nextLine)) / 3.0;

    vec3 color = currentLine * currentWeight + nextLine * nextWeight;
    float maskPhase = mod(floor(gl_FragCoord.x), 2.0);
    vec3 apertureMask = mix(
        vec3(1.0, 1.0 - DOT_MASK_STRENGTH, 1.0),
        vec3(1.0 - DOT_MASK_STRENGTH, 1.0, 1.0 - DOT_MASK_STRENGTH),
        maskPhase);
    color *= apertureMask;
    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / DISPLAY_GAMMA));
    color *= roundedScreenMask(screenCoord);
    fragmentColor = vec4(color, 1.0);
}
