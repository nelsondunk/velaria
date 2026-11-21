// Host render of the sound orbs shader (src/orbs_shader.h, +91) with Mesa's surfaceless EGL + GLES 3, so the orbs can
// be looked at, and a shader compile error caught, before an APK goes on the headset. Writes a side-by-side stereo
// pair over graphite as a PPM (PIL turns it into a PNG).
//   g++ -O2 -std=c++17 -Iquest/src quest/tools/orbsview.cpp quest/src/orbs.cpp -lEGL -lGLESv2 -o /tmp/orbsview
//   EGL_PLATFORM=surfaceless /tmp/orbsview /tmp/orbs.ppm [timeS]
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "orbs.h"
#include "orbs_shader.h"

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        fprintf(stderr, "shader compile failed:\n%s\n", log);
        exit(2);
    }
    return s;
}

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "/tmp/orbs.ppm";
    float timeS = argc > 2 ? (float)atof(argv[2]) : 3.0f;
    const int W = 768, H = 768;

    PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = getPlatformDisplay ? getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr) : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, nullptr, nullptr)) { fprintf(stderr, "no EGL display\n"); return 1; }
    const EGLint cfgAttrs[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
    EGLConfig cfg; EGLint n = 0;
    if (!eglChooseConfig(dpy, cfgAttrs, &cfg, 1, &n) || n < 1) { fprintf(stderr, "no EGL config\n"); return 1; }
    const EGLint ctxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttrs);
    const EGLint pb[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pb);
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) { fprintf(stderr, "eglMakeCurrent failed\n"); return 1; }
    printf("GL %s / %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    GLuint prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, vrq::shaders::kVS));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, vrq::shaders::kFSOrbs));
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { fprintf(stderr, "link failed\n"); return 2; }

    GLuint tex, fbo, vao;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2 * W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glGenVertexArrays(1, &vao);

    // three orbs as the tracker would place them: front-left at level 1, right a little up at 0.6, one low and back-right
    struct O { float az, el, level, alpha, phase; } os[] = {{25, 5, 1.0f, 1.0f, 0.4f}, {-12, 14, 0.6f, 1.0f, 2.9f}, {5, -16, 0.8f, 0.6f, 5.1f}, {-30, -4, 0.3f, 1.0f, 1.3f}};
    const float dist = 2.5f;
    float orb[8 * 4] = {}, orbP[8 * 4] = {};
    int cnt = 0;
    for (auto& o : os) {
        float az = o.az * (float)M_PI / 180, el = o.el * (float)M_PI / 180;
        // ambisonic (x fwd, y left, z up) -> OpenXR (x right, y up, z back)
        float ax = std::cos(el) * std::cos(az), ay = std::cos(el) * std::sin(az), az_ = std::sin(el);
        orb[cnt * 4] = -ay * dist; orb[cnt * 4 + 1] = az_ * dist + 1.6f; orb[cnt * 4 + 2] = -ax * dist;
        orb[cnt * 4 + 3] = dist * (0.085f + 0.055f * o.level);
        orbP[cnt * 4] = o.level; orbP[cnt * 4 + 1] = o.alpha; orbP[cnt * 4 + 2] = o.phase;
        cnt++;
    }
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "uCount"), cnt);
    glUniform4fv(glGetUniformLocation(prog, "uOrb"), 8, orb);
    glUniform4fv(glGetUniformLocation(prog, "uOrbP"), 8, orbP);
    glUniform1f(glGetUniformLocation(prog, "uTime"), timeS);
    // head at 1.6 m looking a little left (toward the first orb) so it sits near the centre; IPD 64 mm
    float yaw = 15 * (float)M_PI / 180;
    float rot[9] = {std::cos(yaw), 0, -std::sin(yaw), 0, 1, 0, std::sin(yaw), 0, std::cos(yaw)};   // columns: right, up, back
    glUniformMatrix3fv(glGetUniformLocation(prog, "uRot"), 1, GL_FALSE, rot);
    float half = std::tan(48 * (float)M_PI / 180);   // Quest 3 is ~96 degrees across
    glUniform4f(glGetUniformLocation(prog, "uTan"), -half, half, half, -half);
    glBindVertexArray(vao);
    for (int e = 0; e < 2; e++) {
        float ipd = (e == 0 ? -0.032f : 0.032f);
        float eye[3] = {rot[0] * ipd, 1.6f, rot[2] * ipd};
        glUniform3fv(glGetUniformLocation(prog, "uEye"), 1, eye);
        glViewport(e * W, 0, W, H);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glFinish();
    std::vector<unsigned char> px((size_t)2 * W * H * 4);
    glReadPixels(0, 0, 2 * W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

    // composite premultiplied over graphite, flip to top-row-first, report coverage per eye
    FILE* f = fopen(out, "wb");
    fprintf(f, "P6\n%d %d\n255\n", 2 * W, H);
    long covered[2] = {0, 0};
    for (int y = H - 1; y >= 0; y--)
        for (int x = 0; x < 2 * W; x++) {
            const unsigned char* p = &px[((size_t)y * 2 * W + x) * 4];
            float a = p[3] / 255.0f;
            if (p[3] > 8) covered[x / W]++;
            unsigned char o[3];
            const float bg[3] = {0.11f, 0.11f, 0.12f};
            for (int c = 0; c < 3; c++) o[c] = (unsigned char)std::min(255.0f, p[c] + bg[c] * 255 * (1 - a));
            fwrite(o, 1, 3, f);
        }
    fclose(f);
    printf("wrote %s; orb pixels left %ld right %ld of %d each\n", out, covered[0], covered[1], W * H);
    return (covered[0] > 1000 && covered[1] > 1000) ? 0 : 3;
}
