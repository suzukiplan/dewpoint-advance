#ifndef METAL_RENDERER_H
#define METAL_RENDERER_H

#include "video_renderer.h"

#include <memory>

class MetalRenderer final : public VideoRenderer
{
  private:
    class Implementation;
    std::unique_ptr<Implementation> implementation;

  public:
    MetalRenderer();
    ~MetalRenderer() override;

    bool initialize(
        struct SDL_Window* window,
        int width,
        int height,
        VideoFilter filter) override;
    bool usesVsync() const override;
    void present(const void* pixels) override;
    void shutdown() override;
};

#endif
