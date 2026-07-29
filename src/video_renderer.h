#ifndef VIDEO_RENDERER_H
#define VIDEO_RENDERER_H

class VideoRenderer
{
  public:
    virtual ~VideoRenderer() = default;
    virtual bool initialize(
        struct SDL_Window* window,
        int width,
        int height,
        bool crtEnabled) = 0;
    virtual bool usesVsync() const = 0;
    virtual void present(const void* pixels) = 0;
    virtual void shutdown() = 0;
};

#endif
