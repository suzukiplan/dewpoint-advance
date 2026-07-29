/**
 * Dewpoint Advance Runtime (SDL2)
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 SUZUKI PLAN.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "dewpoint_runtime.h"
#include "dewpoint_define.h"
#include "mgbahelper.h"
#include "pathutil.h"
#include "steam.hpp"
#include "video_renderer.h"
#include "vulkan_renderer.h"
#ifdef __APPLE__
#include "metal_renderer.h"
#endif

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <mgba/core/log.h>
#include <SDL.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

extern "C" {
extern const uint8_t game_rom[];
extern const size_t game_rom_size;
}

namespace
{
constexpr int WINDOW_SCALE = 3;
constexpr int WINDOW_ASPECT_WIDTH = 3;
constexpr int WINDOW_ASPECT_HEIGHT = 2;
constexpr int WINDOW_MIN_WIDTH = 240;
constexpr int WINDOW_MIN_HEIGHT = 160;
constexpr int AUDIO_FREQUENCY = 44100;
constexpr int AUDIO_CHANNELS = 2;
constexpr int AUDIO_SAMPLES = 512;
constexpr Uint32 TARGET_QUEUED_AUDIO_SIZE = AUDIO_FREQUENCY * AUDIO_CHANNELS * sizeof(int16_t) / 20;
constexpr int MAX_AUDIO_REFILL_FRAMES = 8;

static_assert(
    WINDOW_MIN_WIDTH * WINDOW_ASPECT_HEIGHT == WINDOW_MIN_HEIGHT * WINDOW_ASPECT_WIDTH,
    "Minimum window size must match the window aspect ratio");

using GlBoolean = unsigned char;
using GlBitfield = unsigned int;
using GlChar = char;
using GlEnum = unsigned int;
using GlFloat = float;
using GlInt = int;
using GlSizei = int;
using GlSizePtr = std::ptrdiff_t;
using GlUint = unsigned int;

constexpr GlBoolean GL_FALSE_VALUE = 0;
constexpr GlEnum GL_ARRAY_BUFFER_VALUE = 0x8892;
constexpr GlEnum GL_COLOR_BUFFER_BIT_VALUE = 0x00004000;
constexpr GlEnum GL_COMPILE_STATUS_VALUE = 0x8B81;
constexpr GlEnum GL_FLOAT_VALUE = 0x1406;
constexpr GlEnum GL_FRAGMENT_SHADER_VALUE = 0x8B30;
constexpr GlEnum GL_INFO_LOG_LENGTH_VALUE = 0x8B84;
constexpr GlEnum GL_LINK_STATUS_VALUE = 0x8B82;
constexpr GlEnum GL_NEAREST_VALUE = 0x2600;
constexpr GlEnum GL_RGBA_VALUE = 0x1908;
constexpr GlEnum GL_STATIC_DRAW_VALUE = 0x88E4;
constexpr GlEnum GL_TEXTURE0_VALUE = 0x84C0;
constexpr GlEnum GL_TEXTURE_2D_VALUE = 0x0DE1;
constexpr GlEnum GL_TEXTURE_MAG_FILTER_VALUE = 0x2800;
constexpr GlEnum GL_TEXTURE_MIN_FILTER_VALUE = 0x2801;
constexpr GlEnum GL_TEXTURE_WRAP_S_VALUE = 0x2802;
constexpr GlEnum GL_TEXTURE_WRAP_T_VALUE = 0x2803;
constexpr GlEnum GL_TRIANGLES_VALUE = 0x0004;
constexpr GlEnum GL_UNPACK_ALIGNMENT_VALUE = 0x0CF5;
constexpr GlEnum GL_UNSIGNED_BYTE_VALUE = 0x1401;
constexpr GlEnum GL_VERTEX_SHADER_VALUE = 0x8B31;
constexpr GlEnum GL_CLAMP_TO_EDGE_VALUE = 0x812F;

const char* VIDEO_VERTEX_SHADER = R"GLSL(
#version 120

attribute vec2 a_position;
varying vec2 v_texCoord;

void main()
{
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_texCoord = vec2(a_position.x * 0.5 + 0.5, 0.5 - a_position.y * 0.5);
}
)GLSL";

// The CRT path is an independent implementation of the crt-geom rendering
// model. The LCD path adapts lcd-grid-v2-gba-color.glslp's subpixel response
// and GBA color transform to this renderer's single-pass GLSL 1.20 pipeline.
// https://github.com/libretro/glsl-shaders/blob/master/handheld/lcd-grid-v2-gba-color.glslp
const char* VIDEO_FRAGMENT_SHADER = R"GLSL(
#version 120

uniform sampler2D u_texture;
uniform vec2 u_inputSize;
uniform vec2 u_outputSize;
uniform int u_filterMode;
varying vec2 v_texCoord;

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
    float antialiasWidth = 2.0 / min(u_outputSize.x, u_outputSize.y);
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
    vec2 coord = (texel + vec2(0.5)) / u_inputSize;
    return pow(texture2D(u_texture, coord).rgb, vec3(CRT_GAMMA));
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

    vec2 texelPosition = coord * u_inputSize - vec2(0.5);
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

    // Three beam samples per output pixel keep scanlines stable during
    // non-integer scaling and mirror crt-geom's oversampling behavior.
    float footprint = u_inputSize.y / u_outputSize.y;
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
    vec2 clampedTexel = clamp(texel, vec2(0.0), u_inputSize - vec2(1.0));
    vec2 coord = (clampedTexel + vec2(0.5)) / u_inputSize;
    return pow(vec3(1.5) * texture2D(u_texture, coord).rgb, vec3(2.2));
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
    vec2 texelPosition = v_texCoord * u_inputSize - vec2(0.4999);
    vec2 topLeftTexel = floor(texelPosition);
    vec2 subpixelPosition = texelPosition - topLeftTexel;
    vec2 sourceFootprint = u_inputSize / u_outputSize;

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
    if (u_filterMode == VIDEO_FILTER_CRT) {
        color = renderCrt();
    } else if (u_filterMode == VIDEO_FILTER_LCD) {
        color = renderLcd();
    } else {
        color = texture2D(u_texture, v_texCoord).rgb;
    }
    gl_FragColor = vec4(color, 1.0);
}
)GLSL";

struct GlApi {
    GlUint (*createShader)(GlEnum);
    void (*shaderSource)(GlUint, GlSizei, const GlChar* const*, const GlInt*);
    void (*compileShader)(GlUint);
    void (*getShaderiv)(GlUint, GlEnum, GlInt*);
    void (*getShaderInfoLog)(GlUint, GlSizei, GlSizei*, GlChar*);
    void (*deleteShader)(GlUint);
    GlUint (*createProgram)();
    void (*attachShader)(GlUint, GlUint);
    void (*linkProgram)(GlUint);
    void (*getProgramiv)(GlUint, GlEnum, GlInt*);
    void (*getProgramInfoLog)(GlUint, GlSizei, GlSizei*, GlChar*);
    void (*deleteProgram)(GlUint);
    void (*useProgram)(GlUint);
    GlInt (*getAttribLocation)(GlUint, const GlChar*);
    GlInt (*getUniformLocation)(GlUint, const GlChar*);
    void (*uniform1i)(GlInt, GlInt);
    void (*uniform2f)(GlInt, GlFloat, GlFloat);
    void (*genTextures)(GlSizei, GlUint*);
    void (*bindTexture)(GlEnum, GlUint);
    void (*texParameteri)(GlEnum, GlEnum, GlInt);
    void (*texImage2D)(
        GlEnum,
        GlInt,
        GlInt,
        GlSizei,
        GlSizei,
        GlInt,
        GlEnum,
        GlEnum,
        const void*);
    void (*texSubImage2D)(
        GlEnum,
        GlInt,
        GlInt,
        GlInt,
        GlSizei,
        GlSizei,
        GlEnum,
        GlEnum,
        const void*);
    void (*deleteTextures)(GlSizei, const GlUint*);
    void (*activeTexture)(GlEnum);
    void (*pixelStorei)(GlEnum, GlInt);
    void (*genBuffers)(GlSizei, GlUint*);
    void (*bindBuffer)(GlEnum, GlUint);
    void (*bufferData)(GlEnum, GlSizePtr, const void*, GlEnum);
    void (*deleteBuffers)(GlSizei, const GlUint*);
    void (*vertexAttribPointer)(GlUint, GlInt, GlEnum, GlBoolean, GlSizei, const void*);
    void (*enableVertexAttribArray)(GlUint);
    void (*disableVertexAttribArray)(GlUint);
    void (*drawArrays)(GlEnum, GlInt, GlSizei);
    void (*viewport)(GlInt, GlInt, GlSizei, GlSizei);
    void (*clearColor)(GlFloat, GlFloat, GlFloat, GlFloat);
    void (*clear)(GlBitfield);

    template <typename Function>
    bool load(Function* function, const char* name)
    {
        *function = reinterpret_cast<Function>(SDL_GL_GetProcAddress(name));
        if (!*function) {
            std::cerr << "Failed to load OpenGL function " << name << ": "
                      << SDL_GetError() << '\n';
            return false;
        }
        return true;
    }

    bool load()
    {
        return load(&createShader, "glCreateShader") &&
               load(&shaderSource, "glShaderSource") &&
               load(&compileShader, "glCompileShader") &&
               load(&getShaderiv, "glGetShaderiv") &&
               load(&getShaderInfoLog, "glGetShaderInfoLog") &&
               load(&deleteShader, "glDeleteShader") &&
               load(&createProgram, "glCreateProgram") &&
               load(&attachShader, "glAttachShader") &&
               load(&linkProgram, "glLinkProgram") &&
               load(&getProgramiv, "glGetProgramiv") &&
               load(&getProgramInfoLog, "glGetProgramInfoLog") &&
               load(&deleteProgram, "glDeleteProgram") &&
               load(&useProgram, "glUseProgram") &&
               load(&getAttribLocation, "glGetAttribLocation") &&
               load(&getUniformLocation, "glGetUniformLocation") &&
               load(&uniform1i, "glUniform1i") &&
               load(&uniform2f, "glUniform2f") &&
               load(&genTextures, "glGenTextures") &&
               load(&bindTexture, "glBindTexture") &&
               load(&texParameteri, "glTexParameteri") &&
               load(&texImage2D, "glTexImage2D") &&
               load(&texSubImage2D, "glTexSubImage2D") &&
               load(&deleteTextures, "glDeleteTextures") &&
               load(&activeTexture, "glActiveTexture") &&
               load(&pixelStorei, "glPixelStorei") &&
               load(&genBuffers, "glGenBuffers") &&
               load(&bindBuffer, "glBindBuffer") &&
               load(&bufferData, "glBufferData") &&
               load(&deleteBuffers, "glDeleteBuffers") &&
               load(&vertexAttribPointer, "glVertexAttribPointer") &&
               load(&enableVertexAttribArray, "glEnableVertexAttribArray") &&
               load(&disableVertexAttribArray, "glDisableVertexAttribArray") &&
               load(&drawArrays, "glDrawArrays") &&
               load(&viewport, "glViewport") &&
               load(&clearColor, "glClearColor") &&
               load(&clear, "glClear");
    }
};

class OpenGlRenderer final : public VideoRenderer
{
  private:
    SDL_Window* window;
    SDL_GLContext context;
    GlApi gl;
    GlUint program;
    GlUint texture;
    GlUint vertexBuffer;
    GlInt positionLocation;
    GlInt inputSizeLocation;
    GlInt outputSizeLocation;
    GlInt filterModeLocation;
    int inputWidth;
    int inputHeight;
    bool vsyncEnabled;

    bool compileShader(GlEnum type, const char* source, GlUint* shader)
    {
        *shader = gl.createShader(type);
        if (!*shader) {
            std::cerr << "Failed to create video shader\n";
            return false;
        }

        gl.shaderSource(*shader, 1, &source, nullptr);
        gl.compileShader(*shader);

        GlInt compiled = 0;
        gl.getShaderiv(*shader, GL_COMPILE_STATUS_VALUE, &compiled);
        if (compiled) {
            return true;
        }

        GlInt logLength = 0;
        gl.getShaderiv(*shader, GL_INFO_LOG_LENGTH_VALUE, &logLength);
        std::vector<GlChar> log(static_cast<size_t>(std::max(logLength, 1)));
        gl.getShaderInfoLog(*shader, static_cast<GlSizei>(log.size()), nullptr, log.data());
        std::cerr << "Failed to compile video shader: " << log.data() << '\n';
        gl.deleteShader(*shader);
        *shader = 0;
        return false;
    }

    bool createProgram()
    {
        GlUint vertexShader = 0;
        GlUint fragmentShader = 0;
        if (!compileShader(GL_VERTEX_SHADER_VALUE, VIDEO_VERTEX_SHADER, &vertexShader) ||
            !compileShader(GL_FRAGMENT_SHADER_VALUE, VIDEO_FRAGMENT_SHADER, &fragmentShader)) {
            if (vertexShader) {
                gl.deleteShader(vertexShader);
            }
            return false;
        }

        program = gl.createProgram();
        if (!program) {
            std::cerr << "Failed to create video shader program\n";
            gl.deleteShader(fragmentShader);
            gl.deleteShader(vertexShader);
            return false;
        }
        gl.attachShader(program, vertexShader);
        gl.attachShader(program, fragmentShader);
        gl.linkProgram(program);
        gl.deleteShader(fragmentShader);
        gl.deleteShader(vertexShader);

        GlInt linked = 0;
        gl.getProgramiv(program, GL_LINK_STATUS_VALUE, &linked);
        if (!linked) {
            GlInt logLength = 0;
            gl.getProgramiv(program, GL_INFO_LOG_LENGTH_VALUE, &logLength);
            std::vector<GlChar> log(static_cast<size_t>(std::max(logLength, 1)));
            gl.getProgramInfoLog(
                program,
                static_cast<GlSizei>(log.size()),
                nullptr,
                log.data());
            std::cerr << "Failed to link video shader: " << log.data() << '\n';
            return false;
        }

        positionLocation = gl.getAttribLocation(program, "a_position");
        const GlInt textureLocation = gl.getUniformLocation(program, "u_texture");
        inputSizeLocation = gl.getUniformLocation(program, "u_inputSize");
        outputSizeLocation = gl.getUniformLocation(program, "u_outputSize");
        filterModeLocation = gl.getUniformLocation(program, "u_filterMode");
        if (positionLocation < 0 || textureLocation < 0 ||
            inputSizeLocation < 0 || outputSizeLocation < 0 || filterModeLocation < 0) {
            std::cerr << "Failed to locate video shader inputs\n";
            return false;
        }

        gl.useProgram(program);
        gl.uniform1i(textureLocation, 0);
        return true;
    }

  public:
    OpenGlRenderer()
        : window(nullptr), context(nullptr), gl{}, program(0), texture(0), vertexBuffer(0), positionLocation(-1), inputSizeLocation(-1), outputSizeLocation(-1), filterModeLocation(-1), inputWidth(0), inputHeight(0), vsyncEnabled(false)
    {
    }

    ~OpenGlRenderer() override
    {
        shutdown();
    }

    bool initialize(
        SDL_Window* targetWindow,
        int width,
        int height,
        VideoFilter filter) override
    {
        window = targetWindow;
        inputWidth = width;
        inputHeight = height;
        context = SDL_GL_CreateContext(window);
        if (!context) {
            std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << '\n';
            return false;
        }
        if (SDL_GL_MakeCurrent(window, context) != 0) {
            std::cerr << "SDL_GL_MakeCurrent failed: " << SDL_GetError() << '\n';
            return false;
        }

        if (SDL_GL_SetSwapInterval(1) == 0) {
            vsyncEnabled = true;
        } else {
            std::cerr << "Failed to enable OpenGL VSync; using frame pacing fallback: "
                      << SDL_GetError() << '\n';
            SDL_GL_SetSwapInterval(0);
        }

        if (!gl.load() || !createProgram()) {
            return false;
        }
        gl.uniform1i(filterModeLocation, static_cast<GlInt>(filter));

        gl.activeTexture(GL_TEXTURE0_VALUE);
        gl.genTextures(1, &texture);
        gl.bindTexture(GL_TEXTURE_2D_VALUE, texture);
        gl.texParameteri(
            GL_TEXTURE_2D_VALUE,
            GL_TEXTURE_MIN_FILTER_VALUE,
            static_cast<GlInt>(GL_NEAREST_VALUE));
        gl.texParameteri(
            GL_TEXTURE_2D_VALUE,
            GL_TEXTURE_MAG_FILTER_VALUE,
            static_cast<GlInt>(GL_NEAREST_VALUE));
        gl.texParameteri(
            GL_TEXTURE_2D_VALUE,
            GL_TEXTURE_WRAP_S_VALUE,
            static_cast<GlInt>(GL_CLAMP_TO_EDGE_VALUE));
        gl.texParameteri(
            GL_TEXTURE_2D_VALUE,
            GL_TEXTURE_WRAP_T_VALUE,
            static_cast<GlInt>(GL_CLAMP_TO_EDGE_VALUE));
        gl.pixelStorei(GL_UNPACK_ALIGNMENT_VALUE, 1);
        gl.texImage2D(
            GL_TEXTURE_2D_VALUE,
            0,
            static_cast<GlInt>(GL_RGBA_VALUE),
            inputWidth,
            inputHeight,
            0,
            GL_RGBA_VALUE,
            GL_UNSIGNED_BYTE_VALUE,
            nullptr);

        const GlFloat vertices[] = {
            -1.0F,
            -1.0F,
            1.0F,
            -1.0F,
            -1.0F,
            1.0F,
            -1.0F,
            1.0F,
            1.0F,
            -1.0F,
            1.0F,
            1.0F,
        };
        gl.genBuffers(1, &vertexBuffer);
        gl.bindBuffer(GL_ARRAY_BUFFER_VALUE, vertexBuffer);
        gl.bufferData(
            GL_ARRAY_BUFFER_VALUE,
            static_cast<GlSizePtr>(sizeof(vertices)),
            vertices,
            GL_STATIC_DRAW_VALUE);
        return true;
    }

    bool usesVsync() const override
    {
        return vsyncEnabled;
    }

    void present(const void* pixels) override
    {
        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_GL_GetDrawableSize(window, &drawableWidth, &drawableHeight);
        if (drawableWidth <= 0 || drawableHeight <= 0) {
            return;
        }

        int viewportWidth = drawableWidth;
        int viewportHeight =
            viewportWidth * WINDOW_ASPECT_HEIGHT / WINDOW_ASPECT_WIDTH;
        if (viewportHeight > drawableHeight) {
            viewportHeight = drawableHeight;
            viewportWidth =
                viewportHeight * WINDOW_ASPECT_WIDTH / WINDOW_ASPECT_HEIGHT;
        }
        const int viewportX = (drawableWidth - viewportWidth) / 2;
        const int viewportY = (drawableHeight - viewportHeight) / 2;

        gl.clearColor(0.0F, 0.0F, 0.0F, 1.0F);
        gl.clear(GL_COLOR_BUFFER_BIT_VALUE);
        gl.viewport(viewportX, viewportY, viewportWidth, viewportHeight);
        gl.useProgram(program);
        gl.activeTexture(GL_TEXTURE0_VALUE);
        gl.bindTexture(GL_TEXTURE_2D_VALUE, texture);
        gl.texSubImage2D(
            GL_TEXTURE_2D_VALUE,
            0,
            0,
            0,
            inputWidth,
            inputHeight,
            GL_RGBA_VALUE,
            GL_UNSIGNED_BYTE_VALUE,
            pixels);
        gl.uniform2f(
            inputSizeLocation,
            static_cast<GlFloat>(inputWidth),
            static_cast<GlFloat>(inputHeight));
        gl.uniform2f(
            outputSizeLocation,
            static_cast<GlFloat>(viewportWidth),
            static_cast<GlFloat>(viewportHeight));
        gl.bindBuffer(GL_ARRAY_BUFFER_VALUE, vertexBuffer);
        gl.vertexAttribPointer(
            static_cast<GlUint>(positionLocation),
            2,
            GL_FLOAT_VALUE,
            GL_FALSE_VALUE,
            0,
            nullptr);
        gl.enableVertexAttribArray(static_cast<GlUint>(positionLocation));
        gl.drawArrays(GL_TRIANGLES_VALUE, 0, 6);
        gl.disableVertexAttribArray(static_cast<GlUint>(positionLocation));
        SDL_GL_SwapWindow(window);
    }

    void shutdown() override
    {
        if (!context) {
            return;
        }
        SDL_GL_MakeCurrent(window, context);
        if (vertexBuffer && gl.deleteBuffers) {
            gl.deleteBuffers(1, &vertexBuffer);
            vertexBuffer = 0;
        }
        if (texture && gl.deleteTextures) {
            gl.deleteTextures(1, &texture);
            texture = 0;
        }
        if (program && gl.deleteProgram) {
            gl.deleteProgram(program);
            program = 0;
        }
        SDL_GL_DeleteContext(context);
        context = nullptr;
    }
};

struct WindowConfig {
    int32_t fullscreen;
    int32_t width;
    int32_t height;
    int32_t x;
    int32_t y;
};

static_assert(sizeof(WindowConfig) == 20, "WindowConfig must use five 4-byte fields");

class ScopedLogger
{
  private:
    mStandardLogger logger;

  public:
    ScopedLogger()
        : logger{}
    {
        mStandardLoggerInit(&logger);
        logger.logToStdout = true;
        logger.d.filter->defaultLevels = mLOG_FATAL | mLOG_ERROR | mLOG_WARN | mLOG_GAME_ERROR;
        mLogSetDefaultLogger(&logger.d);
    }

    ~ScopedLogger()
    {
        mLogSetDefaultLogger(nullptr);
        mStandardLoggerDeinit(&logger);
    }
};

class ScopedSdl
{
  private:
    bool initialized;

  public:
    ScopedSdl()
        : initialized(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) == 0)
    {
        if (initialized) {
            // SDL2 enables text input on desktop by default. This runtime only
            // handles game controls, so keep macOS from opening its accent menu
            // when a letter key is held down.
            SDL_StopTextInput();
        }
    }

    ~ScopedSdl()
    {
        if (initialized) {
            SDL_Quit();
        }
    }

    explicit operator bool() const { return initialized; }
};

void updateKeyState(mGBAHelper::KeyState* state, SDL_Keycode key, bool pressed)
{
    switch (key) {
        case SDLK_UP: state->up = pressed; break;
        case SDLK_DOWN: state->down = pressed; break;
        case SDLK_LEFT: state->left = pressed; break;
        case SDLK_RIGHT: state->right = pressed; break;
        case SDLK_z: state->b = pressed; break;
        case SDLK_x: state->a = pressed; break;
        case SDLK_a: state->l = pressed; break;
        case SDLK_s: state->r = pressed; break;
        case SDLK_SPACE: state->start = pressed; break;
        case SDLK_ESCAPE: state->select = pressed; break;
    }
}

void updateGbaKeyState(
    mGBAHelper::KeyState* state,
    const mGBAHelper::KeyState& keyboardState,
    const CSteam::ButtonState& steamState)
{
    state->up = keyboardState.up || steamState.up;
    state->down = keyboardState.down || steamState.down;
    state->left = keyboardState.left || steamState.left;
    state->right = keyboardState.right || steamState.right;
    state->a = keyboardState.a || steamState.a;
    state->b = keyboardState.b || steamState.b;
    state->l = keyboardState.l || steamState.l;
    state->r = keyboardState.r || steamState.r;
    state->start = keyboardState.start || steamState.start;
    state->select = keyboardState.select || steamState.select;
}

bool readFile(const char* path, std::vector<uint8_t>* data)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return false;
    }

    const std::streamsize size = input.tellg();
    if (size <= 0 || static_cast<uintmax_t>(size) > std::numeric_limits<size_t>::max()) {
        return false;
    }
    data->resize(static_cast<size_t>(size));
    input.seekg(0);
    return static_cast<bool>(input.read(reinterpret_cast<char*>(data->data()), size));
}

WindowConfig defaultWindowConfig()
{
    return WindowConfig{
        -1,
        GBA_VRAM_WIDTH * WINDOW_SCALE,
        GBA_VRAM_HEIGHT * WINDOW_SCALE,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
    };
}

void constrainWindowSize(
    int requestedWidth,
    int requestedHeight,
    int previousWidth,
    int previousHeight,
    int* width,
    int* height)
{
    const int64_t widthChange =
        std::abs(static_cast<int64_t>(requestedWidth) - previousWidth) * WINDOW_ASPECT_HEIGHT;
    const int64_t heightChange =
        std::abs(static_cast<int64_t>(requestedHeight) - previousHeight) * WINDOW_ASPECT_WIDTH;
    const bool resizeFromWidth = widthChange >= heightChange;
    const int aspectDimension = resizeFromWidth ? WINDOW_ASPECT_WIDTH : WINDOW_ASPECT_HEIGHT;
    const int requestedDimension = std::max(
        resizeFromWidth ? requestedWidth : requestedHeight,
        resizeFromWidth ? WINDOW_MIN_WIDTH : WINDOW_MIN_HEIGHT);
    const int64_t aspectUnits = std::max<int64_t>(
        WINDOW_MIN_WIDTH / WINDOW_ASPECT_WIDTH,
        (static_cast<int64_t>(requestedDimension) + aspectDimension / 2) / aspectDimension);
    const int64_t constrainedUnits = std::min<int64_t>(
        aspectUnits,
        std::numeric_limits<int>::max() / WINDOW_ASPECT_WIDTH);
    *width = static_cast<int>(constrainedUnits * WINDOW_ASPECT_WIDTH);
    *height = static_cast<int>(constrainedUnits * WINDOW_ASPECT_HEIGHT);
}

WindowConfig loadWindowConfig(const std::string& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        bool exists = false;
        bool isDirectory = false;
        if (!DewpointPath::inspect(path, &exists, &isDirectory, nullptr) || exists) {
            std::cerr << "Failed to read window configuration: " << path << '\n';
        }
        return defaultWindowConfig();
    }

    WindowConfig config{};
    if (input.tellg() != static_cast<std::streamsize>(sizeof(config))) {
        std::cerr << "Invalid window configuration size: " << path << '\n';
        return defaultWindowConfig();
    }
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(&config), sizeof(config)) ||
        (config.fullscreen != -1 && config.fullscreen != 0) || config.width <= 0 || config.height <= 0) {
        std::cerr << "Invalid window configuration: " << path << '\n';
        return defaultWindowConfig();
    }
    constrainWindowSize(
        config.width,
        config.height,
        config.width,
        config.height,
        &config.width,
        &config.height);
    return config;
}

bool saveWindowConfig(
    const std::string& path,
    SDL_Window* window,
    int windowedWidth,
    int windowedHeight,
    int windowedX,
    int windowedY)
{
    const bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    if (!fullscreen) {
        SDL_GetWindowSize(window, &windowedWidth, &windowedHeight);
        SDL_GetWindowPosition(window, &windowedX, &windowedY);
    }

    const WindowConfig config{
        fullscreen ? -1 : 0,
        windowedWidth,
        windowedHeight,
        windowedX,
        windowedY,
    };
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(reinterpret_cast<const char*>(&config), sizeof(config));
    output.flush();
    return static_cast<bool>(output);
}

bool isFullscreen(SDL_Window* window)
{
    return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

void updateCursorVisibility(SDL_Window* window)
{
    const int toggle = isFullscreen(window) ? SDL_DISABLE : SDL_ENABLE;
    if (SDL_ShowCursor(toggle) < 0) {
        std::cerr << "SDL_ShowCursor failed: " << SDL_GetError() << '\n';
    }
}

void printUsage(const char* executable)
{
    std::cerr << "Usage: " << executable
              << " [-s <save.dat>] [-c <config.dat>] [-f <crt|lcd|no>] [rom.gba]\n";
}

bool getApplicationInstallDirectory(std::string* installDirectory)
{
    char* basePath = SDL_GetBasePath();
    if (!basePath) {
        std::cerr << "Failed to get application installation directory: " << SDL_GetError() << '\n';
        return false;
    }

    *installDirectory = basePath;
    SDL_free(basePath);
    return true;
}

bool getSteamInstallDirectory(std::string* installDirectory)
{
    auto* apps = SteamApps();
    auto* utils = SteamUtils();
    if (!apps || !utils) {
        std::cerr << "Failed to access Steam installation information\n";
        return false;
    }

    std::vector<char> pathBuffer(4096);
    if (apps->GetAppInstallDir(utils->GetAppID(), pathBuffer.data(), pathBuffer.size()) == 0) {
        std::cerr << "Failed to get Steam App installation directory\n";
        return false;
    }

    *installDirectory = pathBuffer.data();
    return true;
}

bool redirectLogsToFile(const std::string& path)
{
    std::cout.flush();
    std::cerr.flush();

#ifdef _WIN32
    FILE* logFile = std::fopen(path.c_str(), "w");
    const auto redirect = [](FILE* source, FILE* destination) {
        return _dup2(_fileno(source), _fileno(destination)) == 0;
    };
#else
    FILE* logFile = std::fopen(path.c_str(), "w");
    const auto redirect = [](FILE* source, FILE* destination) {
        return dup2(fileno(source), fileno(destination)) >= 0;
    };
#endif
    if (!logFile) {
        return false;
    }

    const bool redirected = redirect(logFile, stdout) && redirect(logFile, stderr);
    std::fclose(logFile);
    return redirected;
}

bool configureSteamSavePaths(
    const std::string& installDirectory,
    bool usesDefaultSramPath,
    bool usesDefaultConfigPath,
    std::string* sramPath,
    std::string* configPath)
{
    const std::string saveDirectory = DewpointPath::join(installDirectory, "save");
    std::string errorMessage;
    if (!DewpointPath::createDirectory(saveDirectory, &errorMessage)) {
        std::cerr << "Failed to create default save directory: " << saveDirectory << ": "
                  << errorMessage << '\n';
        return false;
    }

    if (usesDefaultSramPath) {
        *sramPath = DewpointPath::join(saveDirectory, "save.dat");
    }
    if (usesDefaultConfigPath) {
        *configPath = DewpointPath::join(saveDirectory, "config.dat");
    }
    return true;
}
} // namespace

int main(int argc, char* argv[])
{
    ScopedLogger logger;

    std::string romPath;
    std::string sramPath = "save.dat";
    std::string configPath = "config.dat";
    bool usesDefaultSramPath = true;
    bool usesDefaultConfigPath = true;
    VideoFilter videoFilter = VideoFilter::None;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-s" || argument == "-c" || argument == "-f") {
            if (++i >= argc) {
                printUsage(argv[0]);
                return 1;
            }
            if (argument == "-s") {
                sramPath = argv[i];
                usesDefaultSramPath = false;
            } else {
                if (argument == "-c") {
                    configPath = argv[i];
                    usesDefaultConfigPath = false;
                } else {
                    const std::string filter = argv[i];
                    if (filter == "crt") {
                        videoFilter = VideoFilter::Crt;
                    } else if (filter == "lcd") {
                        videoFilter = VideoFilter::Lcd;
                    } else if (filter == "no") {
                        videoFilter = VideoFilter::None;
                    } else {
                        std::cerr << "Unknown video filter: " << filter << '\n';
                        printUsage(argv[0]);
                        return 1;
                    }
                }
            }
        } else if (!romPath.empty()) {
            printUsage(argv[0]);
            return 1;
        } else {
            romPath = argument;
        }
    }

    std::string logInstallDirectory;
    bool logsRedirected = false;
    if (SteamAPI_IsSteamRunning()) {
        if (!getApplicationInstallDirectory(&logInstallDirectory)) {
            return 1;
        }
        const std::string logPath = DewpointPath::join(logInstallDirectory, "log.txt");
        if (!redirectLogsToFile(logPath)) {
            std::cerr << "Failed to redirect logs to Steam log file: " << logPath << '\n';
            return 1;
        }
        logsRedirected = true;
    }

    ScopedSdl sdl;
    if (!sdl) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    mGBAHelper gba;
    DewpointRuntime dewpoint(gba, [](const char* message) {
        std::cerr << "[Steam] " << message << '\n';
    });
    char* preferencePath = SDL_GetPrefPath("SUZUKI PLAN", APP_NAME);
    if (!preferencePath) {
        std::cerr << "Failed to get the local application data directory: " << SDL_GetError() << '\n';
    } else {
        const std::string highScoreDirectory = DewpointPath::join(preferencePath, "leaderboard-cache");
        SDL_free(preferencePath);
        std::string errorMessage;
        if (!DewpointPath::createDirectory(highScoreDirectory, &errorMessage) ||
            !dewpoint.setHighScoreStorageDirectory(highScoreDirectory)) {
            std::cerr << "Failed to prepare the pending high score directory: "
                      << highScoreDirectory << ": " << errorMessage << '\n';
        }
    }
    const bool steamInitialized = dewpoint.initialize();
    if (steamInitialized) {
        std::string installDirectory;
        if (!getSteamInstallDirectory(&installDirectory)) {
            return 1;
        }
        if (!logsRedirected || !DewpointPath::same(logInstallDirectory, installDirectory)) {
            const std::string logPath = DewpointPath::join(installDirectory, "log.txt");
            if (!redirectLogsToFile(logPath)) {
                std::cerr << "Failed to redirect logs to Steam log file: " << logPath << '\n';
                return 1;
            }
        }
        if ((usesDefaultSramPath || usesDefaultConfigPath) &&
            !configureSteamSavePaths(
                installDirectory,
                usesDefaultSramPath,
                usesDefaultConfigPath,
                &sramPath,
                &configPath)) {
            return 1;
        }
    }

    std::vector<uint8_t> rom;
    const uint8_t* romData = game_rom;
    size_t romSize = game_rom_size;
    if (!romPath.empty() && !readFile(romPath.c_str(), &rom)) {
        std::cerr << "Failed to read GBA ROM: " << romPath << '\n';
        return 1;
    }
    if (!romPath.empty()) {
        romData = rom.data();
        romSize = rom.size();
    }

    gba.setSramPath(sramPath);
    if (!gba.load(romData, romSize)) {
        if (romPath.empty()) {
            std::cerr << "Failed to load embedded GBA ROM\n";
        } else {
            std::cerr << "Failed to load GBA ROM: " << romPath << '\n';
        }
        return 1;
    }

    CSteam steamInput;
    steamInput.setLoggger([](const char* message) {
        std::cerr << "[SteamInput] " << message << '\n';
    });
    if (steamInitialized) {
        steamInput.enableOverlayTracking();
    }
    const bool steamInputInitialized = steamInitialized && steamInput.initializeInput();

    const WindowConfig config = loadWindowConfig(configPath);
    const bool windowModeEnabled = CSteam::isEnabledWindowModo();
    int windowedWidth = config.width;
    int windowedHeight = config.height;
    int windowedX = config.x;
    int windowedY = config.y;

    SDL_Window* window = nullptr;
    std::unique_ptr<VideoRenderer> renderer;
    const char* videoRendererName = nullptr;
#ifdef __APPLE__
    window = SDL_CreateWindow(
        APP_NAME,
        windowedX,
        windowedY,
        windowedWidth,
        windowedHeight,
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE | SDL_WINDOW_METAL);
    if (window) {
        auto metalRenderer = std::make_unique<MetalRenderer>();
        if (metalRenderer->initialize(
                window,
                gba.getVramWidth(),
                gba.getVramHeight(),
                videoFilter)) {
            renderer = std::move(metalRenderer);
            videoRendererName = "Metal";
        } else {
            std::cerr << "Metal renderer unavailable; falling back to OpenGL\n";
            metalRenderer->shutdown();
            SDL_DestroyWindow(window);
            window = nullptr;
        }
    } else {
        std::cerr << "Failed to create a Metal window; falling back to OpenGL: "
                  << SDL_GetError() << '\n';
    }
#endif
#ifdef LINUX
    window = SDL_CreateWindow(
        APP_NAME,
        windowedX,
        windowedY,
        windowedWidth,
        windowedHeight,
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
    if (window) {
        auto vulkanRenderer = std::make_unique<VulkanRenderer>();
        if (vulkanRenderer->initialize(
                window,
                gba.getVramWidth(),
                gba.getVramHeight(),
                videoFilter)) {
            renderer = std::move(vulkanRenderer);
            videoRendererName = "Vulkan";
        } else {
            std::cerr << "Vulkan renderer unavailable; falling back to OpenGL\n";
            vulkanRenderer->shutdown();
            SDL_DestroyWindow(window);
            window = nullptr;
        }
    } else {
        std::cerr << "Failed to create a Vulkan window; falling back to OpenGL: "
                  << SDL_GetError() << '\n';
    }
#endif
    if (!renderer) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(
            SDL_GL_CONTEXT_PROFILE_MASK,
            SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        window = SDL_CreateWindow(
            APP_NAME,
            windowedX,
            windowedY,
            windowedWidth,
            windowedHeight,
            SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
        if (window) {
            auto openGlRenderer = std::make_unique<OpenGlRenderer>();
            if (openGlRenderer->initialize(
                    window,
                    gba.getVramWidth(),
                    gba.getVramHeight(),
                    videoFilter)) {
                renderer = std::move(openGlRenderer);
                videoRendererName = "OpenGL";
            }
        }
    }
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        return 1;
    }
    if (!renderer) {
        std::cerr << "Failed to initialize a video renderer\n";
        SDL_DestroyWindow(window);
        return 1;
    }
    std::cerr << "Video renderer: " << videoRendererName << '\n';
    SDL_SetWindowMinimumSize(window, WINDOW_MIN_WIDTH, WINDOW_MIN_HEIGHT);
    SDL_GetWindowSize(window, &windowedWidth, &windowedHeight);
    SDL_GetWindowPosition(window, &windowedX, &windowedY);
    if ((!windowModeEnabled || config.fullscreen == -1) &&
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
        std::cerr << "SDL_SetWindowFullscreen failed: " << SDL_GetError() << '\n';
    }
    updateCursorVisibility(window);
    dewpoint.setFullscreenCallbacks(
        [window, windowModeEnabled, &windowedWidth, &windowedHeight, &windowedX, &windowedY](bool fullscreen) {
            const bool wasFullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
            if (!fullscreen && !windowModeEnabled) {
                return wasFullscreen;
            }
            if (fullscreen && !wasFullscreen) {
                SDL_GetWindowSize(window, &windowedWidth, &windowedHeight);
                SDL_GetWindowPosition(window, &windowedX, &windowedY);
            }
            const Uint32 flags = fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
            if (SDL_SetWindowFullscreen(window, flags) != 0) {
                std::cerr << "SDL_SetWindowFullscreen failed: " << SDL_GetError() << '\n';
            }
            const bool fullscreenEnabled = isFullscreen(window);
            updateCursorVisibility(window);
            if (!fullscreenEnabled) {
                SDL_GetWindowSize(window, &windowedWidth, &windowedHeight);
                SDL_GetWindowPosition(window, &windowedX, &windowedY);
            }
            return fullscreenEnabled;
        },
        [window]() {
            return isFullscreen(window);
        });
    const auto saveConfig = [&]() {
        if (!saveWindowConfig(configPath, window, windowedWidth, windowedHeight, windowedX, windowedY)) {
            std::cerr << "Failed to save window configuration: " << configPath << '\n';
        }
    };

    const bool rendererUsesVsync = renderer->usesVsync();

    SDL_AudioSpec desired{};
    desired.freq = AUDIO_FREQUENCY;
    desired.format = AUDIO_S16SYS;
    desired.channels = AUDIO_CHANNELS;
    desired.samples = AUDIO_SAMPLES;
    SDL_AudioSpec obtained{};
    SDL_AudioDeviceID audioDevice =
        SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (!audioDevice) {
        std::cerr << "SDL_OpenAudioDevice failed: " << SDL_GetError() << '\n';
        saveConfig();
        renderer->shutdown();
        SDL_DestroyWindow(window);
        return 1;
    }
    if (obtained.freq != AUDIO_FREQUENCY ||
        obtained.format != AUDIO_S16SYS ||
        obtained.channels != AUDIO_CHANNELS) {
        std::cerr << "SDL opened an incompatible audio format\n";
        SDL_CloseAudioDevice(audioDevice);
        saveConfig();
        renderer->shutdown();
        SDL_DestroyWindow(window);
        return 1;
    }
    std::cerr << "SDL audio initialized: frequency=" << obtained.freq
              << ", channels=" << static_cast<int>(obtained.channels)
              << ", samples=" << obtained.samples << '\n';

    bool running = true;
    bool paused = false;
    bool steamOverlayActive = false;
    // Playback starts only after real PCM reaches the target. The audio device
    // then becomes the emulation clock, keeping VSync and rendering stalls from
    // silently growing or draining the queue.
    bool audioPlaybackStarted = false;
    const auto setPaused = [&](bool value) {
        if (paused == value) {
            return;
        }
        paused = value;
        SDL_ClearQueuedAudio(audioDevice);
        audioPlaybackStarted = false;
        SDL_PauseAudioDevice(audioDevice, 1);
    };
    int exitCode = 0;
    mGBAHelper::KeyState keyboardState{};
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_WINDOWEVENT) {
                updateCursorVisibility(window);
                if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    keyboardState = {};
                } else if (
                    (SDL_GetWindowFlags(window) &
                     (SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_MINIMIZED)) == 0) {
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        int constrainedWidth = 0;
                        int constrainedHeight = 0;
                        constrainWindowSize(
                            event.window.data1,
                            event.window.data2,
                            windowedWidth,
                            windowedHeight,
                            &constrainedWidth,
                            &constrainedHeight);
                        windowedWidth = constrainedWidth;
                        windowedHeight = constrainedHeight;
                        if (constrainedWidth != event.window.data1 ||
                            constrainedHeight != event.window.data2) {
                            SDL_SetWindowSize(window, constrainedWidth, constrainedHeight);
                        }
                    } else if (event.window.event == SDL_WINDOWEVENT_MOVED) {
                        windowedX = event.window.data1;
                        windowedY = event.window.data2;
                    }
                }
            } else if (event.type == SDL_KEYDOWN) {
                const bool command = (event.key.keysym.mod & KMOD_GUI) != 0;
                if (command && event.key.keysym.sym == SDLK_q) {
                    running = false;
                } else if (command && event.key.keysym.sym == SDLK_r && !event.key.repeat) {
                    gba.reset();
                    SDL_ClearQueuedAudio(audioDevice);
                    audioPlaybackStarted = false;
                    SDL_PauseAudioDevice(audioDevice, 1);
                } else if (command && event.key.keysym.sym == SDLK_p && !event.key.repeat) {
                    setPaused(!paused);
                } else if (
                    !command &&
                    !event.key.repeat &&
                    !isFullscreen(window) &&
                    event.key.keysym.sym >= SDLK_1 &&
                    event.key.keysym.sym <= SDLK_4) {
                    const int scale = event.key.keysym.sym - SDLK_0;
                    SDL_SetWindowSize(
                        window,
                        WINDOW_MIN_WIDTH * scale,
                        WINDOW_MIN_HEIGHT * scale);
                } else if (!command) {
                    updateKeyState(&keyboardState, event.key.keysym.sym, true);
                }
            } else if (event.type == SDL_KEYUP) {
                updateKeyState(&keyboardState, event.key.keysym.sym, false);
            }
        }
        if (!running) {
            break;
        }

        dewpoint.tick();
        const bool currentSteamOverlayActive =
            steamInitialized && steamInput.isOverlay();
        if (steamOverlayActive != currentSteamOverlayActive) {
            steamOverlayActive = currentSteamOverlayActive;
            setPaused(steamOverlayActive);
        }
        if (steamInputInitialized) {
            steamInput.updateInput();
        }
        switch (steamInput.buttonState.type) {
            case CSteam::ControllerType::XBOX:
                dewpoint.setGamepadType(DewpointRuntime::GamepadType::Xbox);
                break;
            case CSteam::ControllerType::NintendoSwitch:
                dewpoint.setGamepadType(DewpointRuntime::GamepadType::NintendoSwitch);
                break;
            case CSteam::ControllerType::PlayStation:
                dewpoint.setGamepadType(DewpointRuntime::GamepadType::PlayStation);
                break;
            case CSteam::ControllerType::NotConnected:
                dewpoint.setGamepadType(DewpointRuntime::GamepadType::PCKeyboard);
                break;
        }
        updateGbaKeyState(&gba.keyState, keyboardState, steamInput.buttonState);
        if (dewpoint.takeExitRequest(&exitCode)) {
            break;
        }
        if (paused) {
            SDL_Delay(10);
            continue;
        }

        Uint32 queuedAudioSize = SDL_GetQueuedAudioSize(audioDevice);
        if (audioPlaybackStarted && queuedAudioSize == 0) {
            // SDL supplies silence after a queued-audio underrun. Pause and
            // rebuild the target instead of joining new PCM onto that gap.
            std::cerr << "SDL audio underrun; rebuilding prebuffer\n";
            SDL_PauseAudioDevice(audioDevice, 1);
            SDL_ClearQueuedAudio(audioDevice);
            audioPlaybackStarted = false;
        }

        bool emulationAdvanced = false;
        for (int refill = 0;
             refill < MAX_AUDIO_REFILL_FRAMES &&
             queuedAudioSize < TARGET_QUEUED_AUDIO_SIZE;
             ++refill) {
            gba.tick();
            emulationAdvanced = true;

            size_t soundSize = 0;
            uint16_t* sound = gba.dequeSound(&soundSize);
            if (!sound || !soundSize) {
                break;
            }
            if (soundSize > std::numeric_limits<Uint32>::max()) {
                std::cerr << "SDL audio packet is too large: " << soundSize << '\n';
                running = false;
                break;
            }
            if (SDL_QueueAudio(
                    audioDevice,
                    sound,
                    static_cast<Uint32>(soundSize)) != 0) {
                std::cerr << "SDL_QueueAudio failed: " << SDL_GetError() << '\n';
                running = false;
                break;
            }
            queuedAudioSize = SDL_GetQueuedAudioSize(audioDevice);
        }
        if (!running) {
            break;
        }
        if (!audioPlaybackStarted &&
            queuedAudioSize >= TARGET_QUEUED_AUDIO_SIZE) {
            SDL_PauseAudioDevice(audioDevice, 0);
            audioPlaybackStarted = true;
        }

        renderer->present(gba.getVram());

        if (!rendererUsesVsync && !emulationAdvanced) {
            SDL_Delay(1);
        }
    }

    SDL_PauseAudioDevice(audioDevice, 1);
    SDL_CloseAudioDevice(audioDevice);
    saveConfig();
    renderer->shutdown();
    SDL_DestroyWindow(window);

    return exitCode;
}
