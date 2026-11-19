// EGL/GLES plumbing: zero-copy import of decoded frames (AHardwareBuffer -> EGLImage -> external texture),
// a colour-preserving blit into the OpenXR swapchain, and the text overlay.
#pragma once
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>

#include <map>
#include <string>
#include <vector>

namespace vrq {

class Gfx {
public:
    bool init();
    void shutdown();
    EGLDisplay display() const { return m_display; }
    EGLConfig config() const { return m_config; }
    EGLContext context() const { return m_context; }

    // Fisheye unwrap parameters (nullptr = plain copy). Dual fisheye uses the stream layout; a domemaster is the
    // same lens model with one circle (layout 0), its axis pointed at the dome zenith by the layer pose.
    struct Fisheye {
        int layout = 1;        // 0 mono, 1 side by side, 2 top/bottom (which half holds which eye)
        float fovDeg = 190;    // lens field of view (Canon RF 5.2 dual fisheye: 190)
        float circle = 1.0f;   // image circle radius / (eye height / 2)
    };
    // Dome preview (+69): the domemaster seen from a seat, drawn as a 360 equirect in the level room frame
    // (lon -180..180 across, lat latTop..latBottom down) at infinity, like the theatre background behind it.
    // Outside the dome the output is transparent (premultiplied), so the theatre's wall shows through.
    struct Dome {
        float eye[3] = {0, 0, 0};   // the eye from the dome's centre, (right, up, back) as fractions of the radius
        float tiltDeg = 0;          // the dome's front edge below level
        float fovDeg = 180;         // field of view the master's circle covers; the dome shows its inner 180
        float circle = 1.0f;        // image circle radius / (frame height / 2)
        float latTopDeg = 90, latBottomDeg = -40;
        int srcW = 0, srcH = 0;     // the stream's frame size (the destination is 2:1, so it can't be inferred)
    };
    // Draws the decoded frame into dstTex (w x h), unwrapping dual fisheye to per-eye 180 equirect when asked,
    // or a domemaster to the seat's view of the dome. Returns false if the buffer couldn't be imported.
    bool blitVideo(AHardwareBuffer* ahb, GLuint dstTex, int w, int h, const Fisheye* fisheye = nullptr, const Dome* dome = nullptr);
    void dropImageCache();   // call when the image reader is replaced

    // Uploads RGBA pixels into dstTex. `rgba` is top-row-first unless bottomUp (already in GL row order).
    void upload(GLuint dstTex, const uint8_t* rgba, int w, int h, bool bottomUp = false);

private:
    struct Imported {
        EGLImageKHR image;
        GLuint tex;
        uint64_t lastUse;
    };
    EGLDisplay m_display = EGL_NO_DISPLAY;
    EGLConfig m_config = nullptr;
    EGLContext m_context = EGL_NO_CONTEXT;
    EGLSurface m_surface = EGL_NO_SURFACE;
    GLuint m_program = 0;
    GLint m_texLoc = -1;
    GLuint m_fisheye = 0;
    GLint m_fiTex = -1, m_fiLayout = -1, m_fiHalfFov = -1, m_fiCircle = -1, m_fiAspect = -1;
    GLuint m_dome = 0;
    GLint m_doTex = -1, m_doEye = -1, m_doTilt = -1, m_doHalfFov = -1, m_doCircle = -1, m_doAspect = -1, m_doLat = -1, m_doRadiusPx = -1;
    GLuint m_vao = 0;
    // Keyed by AHardwareBuffer_getId (never reused), not by pointer: the decoder reallocates buffers and a recycled
    // pointer would sample a stale image. Entries unused for a while are released so freed buffers don't pile up.
    std::map<uint64_t, Imported> m_images;
    uint64_t m_blits = 0;
    // One FBO, re-pointed at the destination every blit and detached afterwards. Caching FBOs per texture name is
    // unsafe: swapchain textures are deleted on reconfigure and their names recycled, leaving a cached FBO drawing
    // into the orphaned old texture while the compositor shows the new, never-written one (garbage).
    GLuint m_fbo = 0;
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC m_getClientBuffer = nullptr;
    PFNEGLCREATEIMAGEKHRPROC m_createImage = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC m_destroyImage = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC m_imageTarget = nullptr;
};

// Renders lines of text into an RGBA buffer (white on a translucent dark panel).
void rasterizeText(const std::vector<std::string>& lines, std::vector<uint8_t>& rgba, int w, int h);

}  // namespace vrq
