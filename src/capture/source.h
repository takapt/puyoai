#ifndef CAPTURE_SOURCE_H_
#define CAPTURE_SOURCE_H_

#include <SDL.h>
#include "gui/unique_sdl_surface.h"

class Screen;

class Source {
public:
    virtual ~Source() {}

    UniqueSDLSurface nextFrame();

    virtual void handleEvent(const SDL_Event&) {}
    virtual void handleKeys() {}

    bool ok() const { return ok_; }

    virtual bool start() { return true; }
    bool done() const { return done_; }
    void end() { done_ = true; }

    int width() const { return width_; }
    int height() const { return height_; }

    void setSavesScreenShot(bool b) { savesScreenShot_ = b; }

protected:
    Source();

    virtual UniqueSDLSurface getNextFrame() = 0;

    bool ok_;
    bool done_;
    bool savesScreenShot_ = false;
    int width_;
    int height_;
};

#endif  // CAPTURE_SOURCE_H_
