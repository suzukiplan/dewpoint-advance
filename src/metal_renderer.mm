#include "metal_renderer.h"

#include <SDL.h>
#include <SDL_metal.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cstdint>
#include <iostream>

namespace
{
constexpr int WINDOW_ASPECT_WIDTH = 3;
constexpr int WINDOW_ASPECT_HEIGHT = 2;

const char* VIDEO_SHADER = R"METAL(
#include <metal_stdlib>
using namespace metal;

struct RasterizerData {
    float4 position [[position]];
    float2 texCoord;
};

struct ShaderConstants {
    float2 inputSize;
    float2 outputSize;
    int filterMode;
};

constant int VIDEO_FILTER_CRT = 1;
constant int VIDEO_FILTER_LCD = 2;
constant float PI = 3.14159265358979323846;
constant float CRT_GAMMA = 2.1;
constant float DISPLAY_GAMMA = 2.2;
constant float CURVATURE = 0.02;
constant float SCANLINE_WEIGHT = 0.30;
constant float DOT_MASK_STRENGTH = 0.30;
constant float2 CONTENT_SCALE = float2(1.0, 1.04);

vertex RasterizerData videoVertex(uint vertexId [[vertex_id]])
{
    const float2 positions[] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0),
    };
    const float2 texCoords[] = {
        float2(0.0, 1.0),
        float2(2.0, 1.0),
        float2(0.0, -1.0),
    };
    RasterizerData output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.texCoord = texCoords[vertexId];
    return output;
}

float2 warp(float2 coord)
{
    float2 centered = coord * 2.0 - 1.0;
    centered = float2(
        centered.x * (1.0 + CURVATURE * centered.y * centered.y),
        centered.y * (1.0 + CURVATURE * centered.x * centered.x));
    return centered * 0.5 + 0.5;
}

float roundedScreenMask(float2 coord, constant ShaderConstants& constants)
{
    float2 edgeDistance =
        abs(coord - float2(0.5)) - float2(0.475, 0.4625);
    float outsideDistance =
        length(max(edgeDistance, float2(0.0))) +
        min(max(edgeDistance.x, edgeDistance.y), 0.0) -
        0.025;
    float antialiasWidth =
        2.0 / min(constants.outputSize.x, constants.outputSize.y);
    return 1.0 - smoothstep(
        -antialiasWidth,
        antialiasWidth,
        outsideDistance);
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

float3 sampleLinear(
    texture2d<float> source,
    sampler textureSampler,
    float2 texel,
    constant ShaderConstants& constants)
{
    float2 coord = (texel + float2(0.5)) / constants.inputSize;
    return pow(
        source.sample(textureSampler, coord).rgb,
        float3(CRT_GAMMA));
}

float3 sampleScanline(
    texture2d<float> source,
    sampler textureSampler,
    float2 baseTexel,
    float rowOffset,
    float4 coefficients,
    constant ShaderConstants& constants)
{
    float3 color =
        sampleLinear(
            source,
            textureSampler,
            baseTexel + float2(-1.0, rowOffset),
            constants) * coefficients.x +
        sampleLinear(
            source,
            textureSampler,
            baseTexel + float2(0.0, rowOffset),
            constants) * coefficients.y +
        sampleLinear(
            source,
            textureSampler,
            baseTexel + float2(1.0, rowOffset),
            constants) * coefficients.z +
        sampleLinear(
            source,
            textureSampler,
            baseTexel + float2(2.0, rowOffset),
            constants) * coefficients.w;
    return clamp(color, float3(0.0), float3(1.0));
}

float3 scanlineBeam(float distance, float3 color)
{
    float3 intensity = sqrt(color);
    float3 beamWidth = mix(
        float3(SCANLINE_WEIGHT * 0.65),
        float3(SCANLINE_WEIGHT * 1.15),
        intensity);
    float3 scaledDistance = float3(distance) / beamWidth;
    float3 peak = mix(float3(0.95), float3(1.15), intensity);
    return peak * exp(-0.5 * scaledDistance * scaledDistance);
}

float3 renderCrt(
    texture2d<float> source,
    sampler textureSampler,
    float2 texCoord,
    float fragmentX,
    constant ShaderConstants& constants)
{
    float2 screenCoord = warp(texCoord);
    float2 coord =
        (screenCoord - float2(0.5)) * CONTENT_SCALE + float2(0.5);
    if (coord.x < 0.0 || coord.x > 1.0 ||
        coord.y < 0.0 || coord.y > 1.0) {
        return float3(0.0);
    }

    float2 texelPosition = coord * constants.inputSize - float2(0.5);
    float2 baseTexel = floor(texelPosition);
    float2 subTexel = fract(texelPosition);

    float4 coefficients = float4(
        lanczos2(1.0 + subTexel.x),
        lanczos2(subTexel.x),
        lanczos2(1.0 - subTexel.x),
        lanczos2(2.0 - subTexel.x));
    coefficients /= dot(coefficients, float4(1.0));

    float3 currentLine = sampleScanline(
        source,
        textureSampler,
        baseTexel,
        0.0,
        coefficients,
        constants);
    float3 nextLine = sampleScanline(
        source,
        textureSampler,
        baseTexel,
        1.0,
        coefficients,
        constants);

    float footprint = constants.inputSize.y / constants.outputSize.y;
    float sampleOffset = footprint / 3.0;
    float3 currentWeight =
        (scanlineBeam(abs(subTexel.y - sampleOffset), currentLine) +
         scanlineBeam(subTexel.y, currentLine) +
         scanlineBeam(subTexel.y + sampleOffset, currentLine)) / 3.0;
    float3 nextWeight =
        (scanlineBeam(abs(1.0 - subTexel.y + sampleOffset), nextLine) +
         scanlineBeam(1.0 - subTexel.y, nextLine) +
         scanlineBeam(abs(1.0 - subTexel.y - sampleOffset), nextLine)) / 3.0;

    float3 color = currentLine * currentWeight + nextLine * nextWeight;
    float maskPhase = fmod(floor(fragmentX), 2.0);
    float3 apertureMask = mix(
        float3(1.0, 1.0 - DOT_MASK_STRENGTH, 1.0),
        float3(
            1.0 - DOT_MASK_STRENGTH,
            1.0,
            1.0 - DOT_MASK_STRENGTH),
        maskPhase);
    color *= apertureMask;
    color = pow(
        clamp(color, float3(0.0), float3(1.0)),
        float3(1.0 / DISPLAY_GAMMA));
    color *= roundedScreenMask(screenCoord, constants);
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

float3 sampleLcdTexel(
    texture2d<float> source,
    float2 texel,
    constant ShaderConstants& constants)
{
    float2 clampedTexel = clamp(
        texel,
        float2(0.0),
        constants.inputSize - float2(1.0));
    uint2 sourceTexel = uint2(clampedTexel);
    return pow(float3(1.5) * source.read(sourceTexel).rgb, float3(2.2));
}

float3 applyGbaColor(float3 color)
{
    color = pow(max(color, float3(0.0)), float3(3.2));
    color = clamp(color * 0.94, float3(0.0), float3(1.0));
    float3x3 colorMatrix = float3x3(
        float3( 0.82, 0.125, 0.195),
        float3( 0.24, 0.665, 0.075),
        float3(-0.06, 0.210, 0.730));
    return pow(
        clamp(colorMatrix * color, float3(0.0), float3(1.0)),
        float3(1.0 / 2.2));
}

float3 renderLcd(
    texture2d<float> source,
    float2 texCoord,
    constant ShaderConstants& constants)
{
    float2 texelPosition =
        texCoord * constants.inputSize - float2(0.4999);
    float2 topLeftTexel = floor(texelPosition);
    float2 subpixelPosition = texelPosition - topLeftTexel;
    float2 sourceFootprint = constants.inputSize / constants.outputSize;

    float horizontalPosition = subpixelPosition.x * 3.0;
    float horizontalFootprint = sourceFootprint.x * 3.0;
    float3 leftMask = float3(
        lcdSmearX(horizontalPosition + 1.0, horizontalFootprint, 1.5),
        lcdSmearX(horizontalPosition,       horizontalFootprint, 1.5),
        lcdSmearX(horizontalPosition - 1.0, horizontalFootprint, 1.5));
    float3 rightMask = float3(
        lcdSmearX(horizontalPosition - 2.0, horizontalFootprint, 1.5),
        lcdSmearX(horizontalPosition - 3.0, horizontalFootprint, 1.5),
        lcdSmearX(horizontalPosition - 4.0, horizontalFootprint, 1.5));
    leftMask = leftMask.bgr;
    rightMask = rightMask.bgr;

    float topMask =
        lcdSmearY(subpixelPosition.y, sourceFootprint.y, 0.63);
    float bottomMask =
        lcdSmearY(subpixelPosition.y - 1.0, sourceFootprint.y, 0.63);

    float3 color =
        sampleLcdTexel(source, topLeftTexel, constants) *
            leftMask * topMask +
        sampleLcdTexel(
            source,
            topLeftTexel + float2(1.0, 0.0),
            constants) * rightMask * topMask +
        sampleLcdTexel(
            source,
            topLeftTexel + float2(0.0, 1.0),
            constants) * leftMask * bottomMask +
        sampleLcdTexel(
            source,
            topLeftTexel + float2(1.0, 1.0),
            constants) * rightMask * bottomMask;
    color *= pow(float3(0.75), float3(2.2));
    color = pow(max(color, float3(0.0)), float3(1.0 / 2.2));
    return applyGbaColor(color);
}

fragment float4 videoFragment(
    RasterizerData input [[stage_in]],
    texture2d<float> source [[texture(0)]],
    sampler textureSampler [[sampler(0)]],
    constant ShaderConstants& constants [[buffer(0)]])
{
    float3 color;
    if (constants.filterMode == VIDEO_FILTER_CRT) {
        color = renderCrt(
            source,
            textureSampler,
            input.texCoord,
            input.position.x,
            constants);
    } else if (constants.filterMode == VIDEO_FILTER_LCD) {
        color = renderLcd(source, input.texCoord, constants);
    } else {
        color = source.sample(textureSampler, input.texCoord).rgb;
    }
    return float4(color, 1.0);
}
)METAL";

struct alignas(8) ShaderConstants {
    float inputSize[2];
    float outputSize[2];
    int32_t filterMode;
};

static_assert(
    sizeof(ShaderConstants) == 24,
    "Metal shader constants must match the Metal shading language layout");
} // namespace

class MetalRenderer::Implementation
{
  private:
    SDL_Window* window = nullptr;
    SDL_MetalView view = nullptr;
    CAMetalLayer* layer = nil;
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> commandQueue = nil;
    id<MTLRenderPipelineState> pipeline = nil;
    id<MTLTexture> texture = nil;
    id<MTLSamplerState> textureSampler = nil;
    id<MTLCommandBuffer> inFlightCommandBuffer = nil;
    int inputWidth = 0;
    int inputHeight = 0;
    VideoFilter filter = VideoFilter::None;

    bool createPipeline()
    {
        NSError* error = nil;
        NSString* shaderSource = [NSString stringWithUTF8String:VIDEO_SHADER];
        id<MTLLibrary> library =
            [device newLibraryWithSource:shaderSource options:nil error:&error];
        if (!library) {
            std::cerr << "Failed to compile Metal video shader: "
                      << (error ? error.localizedDescription.UTF8String : "unknown error")
                      << '\n';
            return false;
        }

        id<MTLFunction> vertexFunction =
            [library newFunctionWithName:@"videoVertex"];
        id<MTLFunction> fragmentFunction =
            [library newFunctionWithName:@"videoFragment"];
        if (!vertexFunction || !fragmentFunction) {
            std::cerr << "Failed to load Metal video shader functions\n";
            return false;
        }

        MTLRenderPipelineDescriptor* descriptor =
            [[MTLRenderPipelineDescriptor alloc] init];
        descriptor.label = @"Dewpoint video pipeline";
        descriptor.vertexFunction = vertexFunction;
        descriptor.fragmentFunction = fragmentFunction;
        descriptor.colorAttachments[0].pixelFormat = layer.pixelFormat;
        pipeline = [device newRenderPipelineStateWithDescriptor:descriptor
                                                          error:&error];
        if (!pipeline) {
            std::cerr << "Failed to create Metal video pipeline: "
                      << (error ? error.localizedDescription.UTF8String : "unknown error")
                      << '\n';
            return false;
        }
        return true;
    }

    bool createTexture()
    {
        MTLTextureDescriptor* descriptor =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
                                      MTLPixelFormatRGBA8Unorm
                                                                  width:
                                      static_cast<NSUInteger>(inputWidth)
                                                                 height:
                                      static_cast<NSUInteger>(inputHeight)
                                                              mipmapped:NO];
        descriptor.usage = MTLTextureUsageShaderRead;
#if TARGET_OS_OSX
        descriptor.storageMode = MTLStorageModeManaged;
#else
        descriptor.storageMode = MTLStorageModeShared;
#endif
        texture = [device newTextureWithDescriptor:descriptor];
        if (!texture) {
            std::cerr << "Failed to create Metal video texture\n";
            return false;
        }
        texture.label = @"Dewpoint video texture";

        MTLSamplerDescriptor* samplerDescriptor =
            [[MTLSamplerDescriptor alloc] init];
        samplerDescriptor.minFilter = MTLSamplerMinMagFilterNearest;
        samplerDescriptor.magFilter = MTLSamplerMinMagFilterNearest;
        samplerDescriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
        samplerDescriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
        textureSampler = [device newSamplerStateWithDescriptor:samplerDescriptor];
        if (!textureSampler) {
            std::cerr << "Failed to create Metal video sampler\n";
            return false;
        }
        return true;
    }

  public:
    ~Implementation()
    {
        shutdown();
    }

    bool initialize(
        SDL_Window* targetWindow,
        int width,
        int height,
        VideoFilter selectedFilter)
    {
        if (!targetWindow || width <= 0 || height <= 0) {
            std::cerr << "Invalid Metal renderer dimensions\n";
            return false;
        }

        window = targetWindow;
        inputWidth = width;
        inputHeight = height;
        filter = selectedFilter;
        device = MTLCreateSystemDefaultDevice();
        if (!device) {
            std::cerr << "No Metal device is available\n";
            return false;
        }

        view = SDL_Metal_CreateView(window);
        if (!view) {
            std::cerr << "SDL_Metal_CreateView failed: " << SDL_GetError() << '\n';
            return false;
        }
        layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(view);
        if (!layer) {
            std::cerr << "SDL_Metal_GetLayer failed\n";
            return false;
        }
        layer.device = device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        layer.displaySyncEnabled = YES;

        commandQueue = [device newCommandQueue];
        if (!commandQueue) {
            std::cerr << "Failed to create Metal command queue\n";
            return false;
        }
        commandQueue.label = @"Dewpoint video command queue";
        return createPipeline() && createTexture();
    }

    bool usesVsync() const
    {
        return true;
    }

    void present(const void* pixels)
    {
        if (!pixels || !window || !layer || !commandQueue ||
            !pipeline || !texture) {
            return;
        }

        if (inFlightCommandBuffer) {
            [inFlightCommandBuffer waitUntilCompleted];
            inFlightCommandBuffer = nil;
        }

        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_Metal_GetDrawableSize(window, &drawableWidth, &drawableHeight);
        if (drawableWidth <= 0 || drawableHeight <= 0) {
            return;
        }
        layer.drawableSize =
            CGSizeMake(static_cast<CGFloat>(drawableWidth),
                       static_cast<CGFloat>(drawableHeight));

        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (!drawable) {
            return;
        }

        const MTLRegion textureRegion = MTLRegionMake2D(
            0,
            0,
            static_cast<NSUInteger>(inputWidth),
            static_cast<NSUInteger>(inputHeight));
        [texture replaceRegion:textureRegion
                  mipmapLevel:0
                    withBytes:pixels
                  bytesPerRow:static_cast<NSUInteger>(inputWidth) * 4];

        MTLRenderPassDescriptor* renderPass =
            [MTLRenderPassDescriptor renderPassDescriptor];
        renderPass.colorAttachments[0].texture = drawable.texture;
        renderPass.colorAttachments[0].loadAction = MTLLoadActionClear;
        renderPass.colorAttachments[0].storeAction = MTLStoreActionStore;
        renderPass.colorAttachments[0].clearColor =
            MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
        id<MTLRenderCommandEncoder> encoder =
            [commandBuffer renderCommandEncoderWithDescriptor:renderPass];
        if (!commandBuffer || !encoder) {
            std::cerr << "Failed to create Metal rendering commands\n";
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
        const MTLViewport viewport{
            static_cast<double>(viewportX),
            static_cast<double>(viewportY),
            static_cast<double>(viewportWidth),
            static_cast<double>(viewportHeight),
            0.0,
            1.0,
        };
        const MTLScissorRect scissor{
            static_cast<NSUInteger>(viewportX),
            static_cast<NSUInteger>(viewportY),
            static_cast<NSUInteger>(viewportWidth),
            static_cast<NSUInteger>(viewportHeight),
        };
        const ShaderConstants constants{
            {
                static_cast<float>(inputWidth),
                static_cast<float>(inputHeight),
            },
            {
                static_cast<float>(viewportWidth),
                static_cast<float>(viewportHeight),
            },
            static_cast<int32_t>(filter),
        };

        [encoder setViewport:viewport];
        [encoder setScissorRect:scissor];
        [encoder setRenderPipelineState:pipeline];
        [encoder setFragmentTexture:texture atIndex:0];
        [encoder setFragmentSamplerState:textureSampler atIndex:0];
        [encoder setFragmentBytes:&constants
                           length:sizeof(constants)
                          atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                   vertexStart:0
                   vertexCount:3];
        [encoder endEncoding];
        [commandBuffer presentDrawable:drawable];
        [commandBuffer commit];
        inFlightCommandBuffer = commandBuffer;
    }

    void shutdown()
    {
        if (inFlightCommandBuffer) {
            [inFlightCommandBuffer waitUntilCompleted];
            inFlightCommandBuffer = nil;
        }
        textureSampler = nil;
        texture = nil;
        pipeline = nil;
        commandQueue = nil;
        layer = nil;
        device = nil;
        if (view) {
            SDL_Metal_DestroyView(view);
            view = nullptr;
        }
        window = nullptr;
    }
};

MetalRenderer::MetalRenderer()
    : implementation(std::make_unique<Implementation>())
{
}

MetalRenderer::~MetalRenderer() = default;

bool MetalRenderer::initialize(
    SDL_Window* window,
    int width,
    int height,
    VideoFilter filter)
{
    return implementation->initialize(window, width, height, filter);
}

bool MetalRenderer::usesVsync() const
{
    return implementation->usesVsync();
}

void MetalRenderer::present(const void* pixels)
{
    implementation->present(pixels);
}

void MetalRenderer::shutdown()
{
    implementation->shutdown();
}
