// Host check of the sound-map maths in src/main.cpp (+83): builds the covariance of synthetic first-order fields and
// prints where the map peaks.  g++ -O2 -std=c++17 quest/tools/soundmaptest.cpp -o /tmp/smt && /tmp/smt
#include <cmath>
#include <cstdio>
int main() {
    const int W = 512, H = 256;
    struct { const char* name; float az, el; } src[] = {{"front", 0, 0}, {"left", 90, 0}, {"right", -90, 0}, {"back", 180, 0}, {"up", 0, 70}, {"front-left 45", 45, 0}};
    for (auto& s : src) {
        float az = s.az * (float)M_PI / 180, el = s.el * (float)M_PI / 180;
        float g[4] = {1, std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az)};   // W Y Z X, SN3D
        float C[4][4];
        for (int a = 0; a < 4; a++) for (int b = 0; b < 4; b++) C[a][b] = g[a] * g[b];   // one coherent source
        float best = -1; int bi = 0, bj = 0;
        for (int j = 0; j < H; j++) {
            float e = ((float)M_PI / 2) - ((float)j + 0.5f) / H * (float)M_PI;
            for (int i = 0; i < W; i++) {
                float yawRight = (((float)i + 0.5f) / W - 0.5f) * 2 * (float)M_PI, a2 = -yawRight;
                float gv[4] = {1, std::cos(e) * std::sin(a2), std::sin(e), std::cos(e) * std::cos(a2)};
                float acc = 0;
                for (int a = 0; a < 4; a++) { float r = 0; for (int b = 0; b < 4; b++) r += C[a][b] * gv[b]; acc += gv[a] * r; }
                if (acc > best) { best = acc; bi = i; bj = j; }
            }
        }
        float yawDeg = ((bi + 0.5f) / W - 0.5f) * 360, pitchDeg = 90 - (bj + 0.5f) / H * 180;
        printf("%-14s peak at yaw %+6.1f (right+)  pitch %+5.1f\n", s.name, yawDeg, pitchDeg);
    }
}
