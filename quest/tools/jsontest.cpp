// Host test for src/json.h, the parser that reads whatever the bridge socket delivers.
//   g++ -std=c++17 -fsanitize=address,undefined -Iquest/src quest/tools/jsontest.cpp -o /tmp/jsontest && /tmp/jsontest
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>

#include "json.h"

using vrq::Json;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* what) {
    if (ok) g_pass++;
    else {
        g_fail++;
        printf("FAIL %s\n", what);
    }
}

int main() {
    Json j;
    const std::string params =
        R"({"type":"params","frame":108350,"groups":[{"clip":"V2:108300","tool":"Velaria_CanvasControl","kind":"card",)"
        R"("params":[{"id":"Yaw","name":"Pan","kind":"number","min":-180.0,"max":180.0,"value":20.5,"animated":false,)"
        R"("invert":true,"section":"Placement"},{"id":"FrameColour","kind":"colour","ids":["FrameColour","FrameColourG",)"
        R"("FrameColourB"],"value":[1.0,0.5,0.25]},{"id":"Comments","kind":"text","value":"a \"quoted\" \u00e9\n line"}]}]})";
    check(Json::parse(params, j), "a real params message parses");
    check(j.get("groups").arr.size() == 1 && j.get("groups").arr[0].get("params").arr[0].get("value").numberOr(0) == 20.5,
          "and reads back");
    Json again;
    check(Json::parse(j.dump(), again) && again.dump() == j.dump(), "dump -> parse -> dump is stable");
    check(Json::parse("-1.5e3", j) && j.num == -1500.0, "exponent number");
    check(Json::parse("  [1, 2 ,3]  ", j) && j.arr.size() == 3, "whitespace");

    check(!Json::parse("nan", j) && !Json::parse("NaN", j) && !Json::parse("inf", j) && !Json::parse("-inf", j) &&
              !Json::parse("Infinity", j), "nan and inf are not numbers");
    check(!Json::parse("0x10", j) && !Json::parse("-0X1p3", j), "hex is not a number");
    check(!Json::parse("1e999", j), "an overflowing number is refused");
    check(!Json::parse("{\"a\":}", j) && !Json::parse("[1,", j) && !Json::parse("\"open", j) && !Json::parse("", j) &&
              !Json::parse("{\"a\" 1}", j) && !Json::parse("[1] x", j) && !Json::parse("\"\\u12", j) &&
              !Json::parse("\"\\q\"", j), "malformed input is refused");

    std::string deepOk(Json::kMaxDepth, '['), deepBad(Json::kMaxDepth + 2, '[');
    deepOk += "1" + std::string(Json::kMaxDepth, ']');
    deepBad += "1" + std::string(Json::kMaxDepth + 2, ']');
    check(Json::parse(deepOk, j), "nesting up to the limit parses");
    check(!Json::parse(deepBad, j), "nesting past the limit is refused");
    check(!Json::parse(std::string(2000000, '['), j), "two million '[' do not run the stack out");
    std::string objs;
    for (int i = 0; i < 100000; i++) objs += "{\"a\":";
    check(!Json::parse(objs, j), "nor do 100 000 nested objects");

    // Mutation fuzz: flip, drop and duplicate bytes of a valid message. The only requirement is to come back.
    std::mt19937 rng(20261115);
    int parsed = 0;
    for (int n = 0; n < 200000; n++) {
        std::string s = params;
        int edits = 1 + (int)(rng() % 4);
        for (int e = 0; e < edits && !s.empty(); e++) {
            size_t at = rng() % s.size();
            switch (rng() % 4) {
                case 0: s[at] = (char)(rng() & 0xff); break;
                case 1: s.erase(at, 1 + rng() % 8); break;
                case 2: s.insert(at, s.substr(at, 1 + rng() % 16)); break;
                case 3: s[at] = "[]{}\",:\\0-eE.u"[rng() % 15]; break;
            }
        }
        Json f;
        if (Json::parse(s, f)) {
            parsed++;
            (void)f.dump();
            (void)f.get("groups").get("x").intOr(0);
        }
    }
    check(true, "200 000 mutated messages survived");
    printf("%d passed, %d failed (%d of the mutated messages still parsed)\n", g_pass, g_fail, parsed);
    return g_fail ? 1 : 0;
}
