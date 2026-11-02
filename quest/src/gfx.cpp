#include "gfx.h"

#include <android/log.h>

#include <algorithm>
#include <cstring>

#include "font.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VRLink", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "VRLink", __VA_ARGS__)

namespace vrq {

namespace {
const char* kVS = R"(#version 300 es
out vec2 vUv;
void main() {
    // full-screen triangle
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = vec2(p.x, 1.0 - p.y);   // decoded image row 0 is the top of the picture
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// The swapchain is sRGB, so the GPU encodes our output with the sRGB curve. Decoded video is already
// display-encoded; decoding it with the inverse curve first makes the round trip a pass-through of code values.
const char* kFS = R"(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision highp float;
uniform samplerExternalOES uTex;
in vec2 vUv;
out vec4 oColor;
vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
void main() {
    vec3 c = clamp(texture(uTex, vUv).rgb, 0.0, 1.0);
    oColor = vec4(srgbToLinear(c), 1.0);
}
)";

// Dual circular fisheye -> per-eye 180 equirect, in one pass. The destination keeps the source layout (left
// half = left eye for SBS; whole frame for mono), so the compositor shows it as an ordinary 180 layer.
// Lens model: equidistant (f-theta), r = theta / (fov/2) of the circle radius; the circle is inscribed in its
// eye's square (radius = half the eye height, centred). Outside the circle, or beyond 90 deg, black.
const char* kFSFisheye = R"(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision highp float;
uniform samplerExternalOES uTex;
uniform int uLayout;        // 0 mono, 1 side by side, 2 top/bottom
uniform float uHalfFov;     // radians, half the lens field of view
uniform float uCircle;      // circle radius as a fraction of the eye's half-height (1.0 = inscribed)
uniform vec2 uAspect;       // eye width / eye height, 1.0
in vec2 vUv;
out vec4 oColor;
vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
void main() {
    // which eye, and the uv inside that eye
    vec2 eyeUv = vUv;
    vec2 eyeOrigin = vec2(0.0), eyeSize = vec2(1.0);
    if (uLayout == 1) { eyeSize = vec2(0.5, 1.0); eyeOrigin = vec2(vUv.x < 0.5 ? 0.0 : 0.5, 0.0); }
    else if (uLayout == 2) { eyeSize = vec2(1.0, 0.5); eyeOrigin = vec2(0.0, vUv.y < 0.5 ? 0.0 : 0.5); }
    eyeUv = (vUv - eyeOrigin) / eyeSize;
    // output is equirect 180: lon in [-90, 90], lat in [-90, 90]
    float lon = (eyeUv.x - 0.5) * 3.14159265;
    float lat = (0.5 - eyeUv.y) * 3.14159265;
    vec3 d = vec3(cos(lat) * sin(lon), sin(lat), cos(lat) * cos(lon));
    float theta = acos(clamp(d.z, -1.0, 1.0));
    float phi = atan(d.y, d.x);
    float r = theta / uHalfFov;   // 1.0 at the lens edge
    if (r > 1.0 || theta > 1.5707963) { oColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
    // circle radius in eye uv: half the eye height, scaled; x corrected for the eye's aspect
    float R = 0.5 * uCircle;
    vec2 src = vec2(0.5 + r * R * cos(phi) / uAspect.x, 0.5 - r * R * sin(phi));
    vec2 uv = eyeOrigin + src * eyeSize;
    vec3 c = clamp(texture(uTex, uv).rgb, 0.0, 1.0);
    oColor = vec4(srgbToLinear(c), 1.0);
}
)";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        LOGE("shader compile failed: %s", log);
    }
    return s;
}
}  // namespace

bool Gfx::init() {
    m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(m_display, nullptr, nullptr)) return false;
    const EGLint cfgAttrs[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                               EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
    EGLint n = 0;
    if (!eglChooseConfig(m_display, cfgAttrs, &m_config, 1, &n) || n < 1) return false;
    const EGLint ctxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, ctxAttrs);
    if (m_context == EGL_NO_CONTEXT) return false;
    const EGLint pbAttrs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    m_surface = eglCreatePbufferSurface(m_display, m_config, pbAttrs);
    if (!eglMakeCurrent(m_display, m_surface, m_surface, m_context)) return false;

    m_getClientBuffer = (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)eglGetProcAddress("eglGetNativeClientBufferANDROID");
    m_createImage = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    m_destroyImage = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    m_imageTarget = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!m_getClientBuffer || !m_createImage || !m_destroyImage || !m_imageTarget) {
        LOGE("missing EGL image extensions");
        return false;
    }
    m_program = glCreateProgram();
    glAttachShader(m_program, compile(GL_VERTEX_SHADER, kVS));
    glAttachShader(m_program, compile(GL_FRAGMENT_SHADER, kFS));
    glLinkProgram(m_program);
    GLint ok = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        LOGE("program link failed");
        return false;
    }
    m_texLoc = glGetUniformLocation(m_program, "uTex");
    m_fisheye = glCreateProgram();
    glAttachShader(m_fisheye, compile(GL_VERTEX_SHADER, kVS));
    glAttachShader(m_fisheye, compile(GL_FRAGMENT_SHADER, kFSFisheye));
    glLinkProgram(m_fisheye);
    glGetProgramiv(m_fisheye, GL_LINK_STATUS, &ok);
    if (!ok) {
        LOGE("fisheye program link failed");
        m_fisheye = 0;
    } else {
        m_fiTex = glGetUniformLocation(m_fisheye, "uTex");
        m_fiLayout = glGetUniformLocation(m_fisheye, "uLayout");
        m_fiHalfFov = glGetUniformLocation(m_fisheye, "uHalfFov");
        m_fiCircle = glGetUniformLocation(m_fisheye, "uCircle");
        m_fiAspect = glGetUniformLocation(m_fisheye, "uAspect");
    }
    glGenVertexArrays(1, &m_vao);
    LOGI("GL: %s / %s", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    return true;
}

void Gfx::shutdown() {
    dropImageCache();
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
    m_fbo = 0;
    if (m_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (m_surface != EGL_NO_SURFACE) eglDestroySurface(m_display, m_surface);
        if (m_context != EGL_NO_CONTEXT) eglDestroyContext(m_display, m_context);
        eglTerminate(m_display);
    }
    m_display = EGL_NO_DISPLAY;
}

void Gfx::dropImageCache() {
    for (auto& kv : m_images) {
        glDeleteTextures(1, &kv.second.tex);
        m_destroyImage(m_display, kv.second.image);
    }
    m_images.clear();
}

bool Gfx::blitVideo(AHardwareBuffer* ahb, GLuint dstTex, int w, int h, const Fisheye* fisheye) {
    uint64_t id = 0;
    if (AHardwareBuffer_getId(ahb, &id) != 0) id = (uint64_t)(uintptr_t)ahb;
    m_blits++;
    auto it = m_images.find(id);
    if (it == m_images.end()) {
        EGLClientBuffer cb = m_getClientBuffer(ahb);
        const EGLint attrs[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
        EGLImageKHR img = m_createImage(m_display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, cb, attrs);
        if (img == EGL_NO_IMAGE_KHR) {
            LOGE("eglCreateImageKHR failed 0x%x", eglGetError());
            return false;
        }
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_imageTarget(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)img);
        it = m_images.emplace(id, Imported{img, tex, m_blits}).first;
    }
    it->second.lastUse = m_blits;
    // release imports of buffers the decoder no longer cycles through
    for (auto e = m_images.begin(); e != m_images.end();) {
        if (m_blits - e->second.lastUse > 120) {
            glDeleteTextures(1, &e->second.tex);
            m_destroyImage(m_display, e->second.image);
            e = m_images.erase(e);
        } else
            ++e;
    }
    if (!m_fbo) glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dstTex, 0);
    GLenum fbst = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fbst != GL_FRAMEBUFFER_COMPLETE) {
        LOGE("framebuffer incomplete 0x%x", fbst);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    glViewport(0, 0, w, h);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, it->second.tex);
    if (fisheye && m_fisheye) {
        glUseProgram(m_fisheye);
        glUniform1i(m_fiTex, 0);
        glUniform1i(m_fiLayout, fisheye->layout);
        glUniform1f(m_fiHalfFov, fisheye->fovDeg * 0.5f * 3.14159265f / 180.0f);
        glUniform1f(m_fiCircle, fisheye->circle);
        float ew = fisheye->layout == 1 ? w * 0.5f : (float)w, eh = fisheye->layout == 2 ? h * 0.5f : (float)h;
        glUniform2f(m_fiAspect, ew / eh, 1.0f);
    } else {
        glUseProgram(m_program);
        glUniform1i(m_texLoc, 0);
    }
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);   // never hold a swapchain texture
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void Gfx::upload(GLuint dstTex, const uint8_t* rgba, int w, int h) {
    // `rgba` is top-row-first; OpenXR GL swapchains are bottom-row-first, so upload flipped.
    std::vector<uint8_t> flipped((size_t)w * h * 4);
    for (int y = 0; y < h; y++)
        memcpy(&flipped[(size_t)(h - 1 - y) * w * 4], rgba + (size_t)y * w * 4, (size_t)w * 4);
    glBindTexture(GL_TEXTURE_2D, dstTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void rasterizeText(const std::vector<std::string>& lines, std::vector<uint8_t>& rgba, int w, int h) {
    rgba.assign((size_t)w * h * 4, 0);
    // translucent rounded-ish panel
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t* p = &rgba[((size_t)y * w + x) * 4];
            bool corner = (x < 6 || x >= w - 6) && (y < 6 || y >= h - 6);
            p[0] = 10;
            p[1] = 12;
            p[2] = 16;
            p[3] = corner ? 0 : 200;
        }
    const int lineH = font::kH + 4;
    int y0 = 12;
    for (const auto& line : lines) {
        int x0 = 16;
        for (char ch : line) {
            int gi = (unsigned char)ch - font::kFirst;
            if (gi < 0 || gi >= font::kCount) gi = 0;
            const uint8_t* g = font::kGlyphs + (size_t)gi * font::kW * font::kH;
            for (int gy = 0; gy < font::kH; gy++)
                for (int gx = 0; gx < font::kW; gx++) {
                    int px = x0 + gx, py = y0 + gy;
                    if (px >= w || py >= h) continue;
                    uint8_t a = g[gy * font::kW + gx];
                    if (!a) continue;
                    uint8_t* p = &rgba[((size_t)py * w + px) * 4];
                    // white text over the panel
                    p[0] = (uint8_t)((p[0] * (255 - a) + 245 * a) / 255);
                    p[1] = (uint8_t)((p[1] * (255 - a) + 245 * a) / 255);
                    p[2] = (uint8_t)((p[2] * (255 - a) + 245 * a) / 255);
                    p[3] = std::max<uint8_t>(p[3], a);
                }
            x0 += font::kW;
            if (x0 + font::kW > w) break;
        }
        y0 += lineH;
        if (y0 + font::kH > h) break;
    }
}

}  // namespace vrq
