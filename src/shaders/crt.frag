#version 450

layout(set = 0, binding = 0) uniform sampler2D u_texture;
layout(push_constant) uniform ShaderConstants {
    vec2 inputSize;
    vec2 outputSize;
    int filterMode;
} constants;
layout(location = 0) in vec2 v_texCoord;
layout(location = 0) out vec4 fragmentColor;

const int VIDEO_FILTER_CRT = 1;
const int VIDEO_FILTER_LCD = 2;
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

vec3 renderCrt()
{
    vec2 screenCoord = warp(v_texCoord);
    vec2 coord = (screenCoord - vec2(0.5)) * CONTENT_SCALE + vec2(0.5);
    if (coord.x < 0.0 || coord.x > 1.0 ||
        coord.y < 0.0 || coord.y > 1.0) {
        return vec3(0.0);
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
    return color;
}

// Adapted from lcd-grid-v2-gba-color.glslp for a single rendering pass:
// https://github.com/libretro/glsl-shaders/blob/master/handheld/lcd-grid-v2-gba-color.glslp
float lcdSmearXFunction(float z)
{
    float z2 = z * z;
    return z * (1.0 + z2 * (
        -2.0 / 3.0 + z2 * (
        -1.0 / 5.0 + z2 * (
         4.0 / 7.0 + z2 * (
        -1.0 / 9.0 + z2 * (
        -2.0 / 11.0 + z2 / 13.0))))));
}

float lcdSmearYFunction(float z)
{
    float z2 = z * z;
    return z * (1.0 + z2 * z2 * (
        -4.0 / 5.0 + z2 * (
         2.0 / 7.0 + z2 * (
         4.0 / 9.0 + z2 * (
        -4.0 / 11.0 + z2 / 13.0)))));
}

float lcdSmearX(float x, float footprint, float radius)
{
    float low = clamp((x - footprint * 0.5) / radius, -1.0, 1.0);
    float high = clamp((x + footprint * 0.5) / radius, -1.0, 1.0);
    return radius * (lcdSmearXFunction(high) - lcdSmearXFunction(low)) /
        footprint;
}

float lcdSmearY(float y, float footprint, float radius)
{
    float low = clamp((y - footprint * 0.5) / radius, -1.0, 1.0);
    float high = clamp((y + footprint * 0.5) / radius, -1.0, 1.0);
    return radius * (lcdSmearYFunction(high) - lcdSmearYFunction(low)) /
        footprint;
}

vec3 sampleLcdTexel(vec2 texel)
{
    vec2 clampedTexel =
        clamp(texel, vec2(0.0), constants.inputSize - vec2(1.0));
    ivec2 sourceTexel = ivec2(clampedTexel);
    return pow(vec3(1.5) * texelFetch(u_texture, sourceTexel, 0).rgb, vec3(2.2));
}

vec3 applyGbaColor(vec3 color)
{
    color = pow(max(color, vec3(0.0)), vec3(3.2));
    color = clamp(color * 0.94, 0.0, 1.0);
    mat3 colorMatrix = mat3(
         0.82, 0.125, 0.195,
         0.24, 0.665, 0.075,
        -0.06, 0.210, 0.730);
    return pow(clamp(colorMatrix * color, 0.0, 1.0), vec3(1.0 / 2.2));
}

vec3 renderLcd()
{
    vec2 texelPosition = v_texCoord * constants.inputSize - vec2(0.4999);
    vec2 topLeftTexel = floor(texelPosition);
    vec2 subpixelPosition = texelPosition - topLeftTexel;
    vec2 sourceFootprint = constants.inputSize / constants.outputSize;

    float horizontalPosition = subpixelPosition.x * 3.0;
    float horizontalFootprint = sourceFootprint.x * 3.0;
    vec3 leftMask = vec3(
        lcdSmearX(horizontalPosition + 1.0, horizontalFootprint, 1.5),
        lcdSmearX(horizontalPosition,       horizontalFootprint, 1.5),
        lcdSmearX(horizontalPosition - 1.0, horizontalFootprint, 1.5));
    vec3 rightMask = vec3(
        lcdSmearX(horizontalPosition - 2.0, horizontalFootprint, 1.5),
        lcdSmearX(horizontalPosition - 3.0, horizontalFootprint, 1.5),
        lcdSmearX(horizontalPosition - 4.0, horizontalFootprint, 1.5));
    leftMask = leftMask.bgr;
    rightMask = rightMask.bgr;

    float topMask =
        lcdSmearY(subpixelPosition.y, sourceFootprint.y, 0.63);
    float bottomMask =
        lcdSmearY(subpixelPosition.y - 1.0, sourceFootprint.y, 0.63);

    vec3 color =
        sampleLcdTexel(topLeftTexel) * leftMask * topMask +
        sampleLcdTexel(topLeftTexel + vec2(1.0, 0.0)) * rightMask * topMask +
        sampleLcdTexel(topLeftTexel + vec2(0.0, 1.0)) * leftMask * bottomMask +
        sampleLcdTexel(topLeftTexel + vec2(1.0, 1.0)) * rightMask * bottomMask;
    color *= pow(vec3(0.75), vec3(2.2));
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    return applyGbaColor(color);
}

void main()
{
    vec3 color;
    if (constants.filterMode == VIDEO_FILTER_CRT) {
        color = renderCrt();
    } else if (constants.filterMode == VIDEO_FILTER_LCD) {
        color = renderLcd();
    } else {
        color = texture(u_texture, v_texCoord).rgb;
    }
    fragmentColor = vec4(color, 1.0);
}
