// Drives the built LV2 binary through its ports. See tests/run.sh
#include <lv2/core/lv2.h>
#include <lv2/urid/urid.h>
#include <lv2/options/options.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/atom/atom.h>
#include <lv2/parameters/parameters.h>
#include <dlfcn.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
static std::map<std::string, LV2_URID> ids;
static LV2_URID mapUri(LV2_URID_Map_Handle, const char* u) { auto& i = ids[u]; if (!i) i = ids.size(); return i; }
static int fails = 0;
static void check(bool ok, const char* what) { if (!ok) ++fails; printf("%s %s\n", ok ? "ok  " : "FAIL", what); }
struct Host {
    const LV2_Descriptor* d; LV2_Handle h; double sr; int bs;
    float ref = 440, mode = 0, en = 1, scale = 0, note = 0, cents = 0, valid = 0, degree = 0, octave = 0;
    float bands[32] = {};
    std::vector<float> l, r, ol, orr; double worst = 0, total = 0;
    Host(const LV2_Descriptor* d, double sr, int bs) : d(d), sr(sr), bs(bs), l(bs), r(bs), ol(bs), orr(bs) {
        static LV2_URID_Map m{ nullptr, mapUri };
        static int32_t len; len = bs; static float srf; srf = sr;
        LV2_URID ti = mapUri(0, LV2_ATOM__Int), tf = mapUri(0, LV2_ATOM__Float);
        LV2_Options_Option o[] = {
            { LV2_OPTIONS_INSTANCE, 0, mapUri(0, LV2_BUF_SIZE__maxBlockLength), 4, ti, &len },
            { LV2_OPTIONS_INSTANCE, 0, mapUri(0, LV2_BUF_SIZE__nominalBlockLength), 4, ti, &len },
            { LV2_OPTIONS_INSTANCE, 0, mapUri(0, LV2_PARAMETERS__sampleRate), 4, tf, &srf },
            { LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr } };
        LV2_Feature fm{ LV2_URID__map, &m }, fo{ LV2_OPTIONS__options, o };
        const LV2_Feature* f[] = { &fm, &fo, nullptr };
        h = d->instantiate(d, sr, "", f);
        if (!h) { puts("instantiate failed"); exit(2); }
        void* p[] = { l.data(), r.data(), ol.data(), orr.data(), &ref, &mode, &en, &scale, &note, &cents, &valid, &degree, &octave };
        for (int i = 0; i < 13; ++i) d->connect_port(h, i, p[i]);
        for (int i = 0; i < 32; ++i) d->connect_port(h, 13 + i, &bands[i]);
        d->activate(h);
    }
    ~Host() { d->deactivate(h); d->cleanup(h); }
    long t0 = 0; bool same = true; float maxOut = 0, maxStep = 0, lastOut = 0;
    int loudest() const { int m = 0; for (int i = 1; i < 32; ++i) if (bands[i] > bands[m]) m = i; return m; }
    float maxBand() const { return bands[loudest()]; }
    template<class F> void run(double secs, F gen) {
        same = true; maxOut = 0; maxStep = 0;
        for (long n = 0; n < secs * sr; n += bs, t0 += bs) {
            for (int i = 0; i < bs; ++i) { l[i] = gen((t0 + i) / sr); r[i] = -0.3f * l[i] + 0.0f; }
            auto t = std::chrono::steady_clock::now(); d->run(h, bs);
            double e = std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
            worst = std::max(worst, e); total += e;
            for (int i = 0; i < bs; ++i) { same &= ol[i] == l[i] && orr[i] == r[i]; maxOut = std::max(maxOut, std::max(fabsf(ol[i]), fabsf(orr[i])));
                maxStep = std::max(maxStep, fabsf(ol[i] - lastOut)); lastOut = ol[i]; }
        }
    }
};
static double hz(int n, double c) { return 440 * pow(2, (n - 69 + c / 100) / 12); }
static float noise() { return rand() / (float)RAND_MAX * 2 - 1; }
int main(int, char** a) {
    void* so = dlopen(a[1], RTLD_NOW); if (!so) { puts(dlerror()); return 2; }
    auto d = ((LV2_Descriptor_Function)dlsym(so, "lv2_descriptor"))(0);
    char b[200];
    for (double sr : { 44100., 48000., 96000. }) for (int bs : { 64, 256, 1024 }) {
        double maxErr = 0; int wrong = 0;
        for (int n : { 23, 28, 33, 38, 40, 45, 50, 55, 59, 64, 69, 76, 82, 88 }) for (double c : { -37., -8., 0., 3., 21. }) {
            Host h(d, sr, bs); double f = hz(n, c);
            // Plucked string: 10 partials, faster decay higher up, weak fundamental, some noise
            h.run(1.0, [&](double t) { double s = 0; for (int k = 1; k <= 10 && k * f < sr / 2; ++k)
                s += (k == 1 ? .5 : 1. / k) * exp(-t * (.6 + .5 * k)) * sin(2 * M_PI * k * f * t + k); return float(.2 * s + .001 * noise()); });
            if (h.valid < .5 || h.note != n) { ++wrong; if (sr == 48000) printf("  wrong n=%d c=%g got %g %g v%g\n", n, c, h.note, h.cents, h.valid); }
            else maxErr = std::max(maxErr, fabs(h.cents - c));
            if (h.maxOut != 0) check(false, "muted while enabled");
        }
        snprintf(b, 200, "%g Hz / %d: wrong %d of 70, max error %.2f cents", sr, bs, wrong, maxErr); check(!wrong && maxErr < 2, b);
    }
    auto sine = [](double t) { return float(.2 * sin(2 * M_PI * 110 * t)); };
    { Host h(d, 48000, 256); h.run(1, sine); check(h.valid > .5 && h.note == 45, "A2 sine");
      h.run(1, [](double) { return 0.f; }); check(h.valid < .5, "silence clears valid");
      h.run(1, sine); check(h.maxOut == 0, "enabled: audio muted");
      h.en = 0; h.run(.1, sine); check(h.valid < .5 && h.maxStep < .01, "bypass: no pitch, fade in without click");
      h.run(.1, sine); check(h.same, "bypass: audio passes unchanged");
      h.en = 1; h.run(.1, sine); check(h.maxStep < .01, "enable: fade out without click");
      h.run(1, sine); check(h.valid > .5 && h.maxOut == 0, "un-bypass recovers, muted");
      h.ref = 432; h.run(.5, sine); check(fabs(h.cents - 31.77) < 1, "reference 432"); }
    // Default (lv2_enabled = 0 in the TTL): bypassed from the start, audio passes without a fade in
    { Host h(d, 48000, 256); h.en = 0; h.run(.1, sine);
      check(h.same && h.valid < .5, "default bypassed: audio passes from the start"); }
    // Scales: {scale, frequency, degree, octave, cents}
    { const double c4 = 440 * pow(2, -0.75);
      struct T { int scale; double f; int degree, octave; double cents; const char* what; } ts[] = {
        { 0, hz(64, 0), 4, 4, 0, "12-TET E4" },
        { 0, 330, 4, 4, 1.96, "12-TET 330 Hz = E4 +2" },
        { 0, hz(60, -10), 0, 4, -10, "12-TET C4 -10 (period wrap)" },
        { 0, hz(59, 40), 11, 3, 40, "12-TET B3 +40" },
        { 1, 330, 4, 4, 0, "Just E4 = 3/4 of A4" },
        { 2, 440 * 81. / 64 * 16 / 27, 4, 4, 0, "Pythagorean E4" },
        { 8, 440 * pow(2, 50 / 1200.), 19, 4, 0, "24-TET A+" },
        { 9, 440 * pow(2, 1 / 31.), 24, 4, 0, "31-TET A+" },
        { 10, 440 * pow(2, -1 / 53.), 39, 4, 0, "53-TET A-1" },
        { 12, c4 * pow(2, 100 / 1200.), 0, 4, 100, "Slendro root +100" },
        { 15, c4 * 11 / 8, 3, 4, 0, "Harmonic 11" },
        { 16, c4 * 81 / 80 * 2, 1, 5, 0, "Partch 81/80" },
        { 17, c4 * 3, 0, 5, 0, "Bohlen-Pierce tritave" },
        { 17, c4 * pow(3, 1 / 13.) * pow(2, 5 / 1200.), 1, 4, 5, "Bohlen-Pierce step 1 +5" },
      };
      for (auto& t : ts) {
        Host h(d, 48000, 256); h.scale = t.scale;
        h.run(1, [&](double x) { return float(.2 * sin(2 * M_PI * t.f * x)); });
        snprintf(b, 200, "%s: degree %g octave %g cents %.2f", t.what, h.degree, h.octave, h.cents);
        check(h.valid > .5 && h.degree == t.degree && h.octave == t.octave && fabs(h.cents - t.cents) < 1, b);
      } }
    { Host h(d, 48000, 256); int v = 0; for (int i = 0; i < 50; ++i) { h.run(.1, [](double) { return .2f * noise(); }); v += h.valid > .5; }
      snprintf(b, 200, "white noise: valid %d of 50", v); check(v < 5, b); }
    // Spectrum: band b spans 20 * 1000^(b/32) .. 20 * 1000^((b+1)/32) Hz
    auto bandCentre = [](int b) { return 20 * pow(1000., (b + .5) / 32); };
    for (double sr : { 44100., 48000., 96000. }) for (int bs : { 64, 1024 }) {
        int wrong = 0; double maxErr = 0;
        for (int b : { 0, 3, 8, 16, 24, 31 }) {
            Host h(d, sr, bs); h.mode = 1; double f = bandCentre(b);
            h.run(.5, [&](double t) { return float(.5 * sin(2 * M_PI * f * t)); });
            // Full scale sine = 0 dB. Analysed: 0.5 * (L + R) = 0.35 * 0.5 -> -15.14 dB. Bands below ~100 Hz have
            // only a bin or two: up to ~3 dB low between bins
            if (h.loudest() != b) ++wrong;
            else maxErr = std::max(maxErr, fabs(h.bands[b] + 15.14) - (b < 8 ? 2.0 : 0.0));
            if (!h.same) check(false, "spectrum: audio passes unchanged");
        }
        snprintf(b, 200, "spectrum %g Hz / %d: wrong band %d of 6, max level error %.2f dB", sr, bs, wrong, maxErr);
        check(!wrong && maxErr < 1.0, b);
    }
    { Host h(d, 48000, 256); h.mode = 1; double f = 1000;
      h.run(.5, [&](double t) { return float(.5 * sin(2 * M_PI * f * t)); });
      int far = 0; for (int i = 0; i < 32; ++i) if (abs(i - h.loudest()) > 2 && h.bands[i] > -60) ++far;
      snprintf(b, 200, "spectrum 1 kHz sine: %d bands more than 2 away above -60 dB", far); check(!far, b);
      check(h.valid < .5, "spectrum mode: tuner not running");
      h.run(1, [](double) { return 0.f; }); check(h.maxBand() < -50 && h.maxBand() > -75, "spectrum: silence, bands fall at 40 dB/s");
      h.run(1, [](double) { return 0.f; }); check(h.maxBand() <= -79.9f, "spectrum: silence falls to the floor");
      h.run(.5, [&](double t) { return float(.5 * sin(2 * M_PI * f * t)); });
      h.en = 0; h.run(.1, [&](double t) { return float(.5 * sin(2 * M_PI * f * t)); });
      check(h.maxBand() <= -79.9f && h.same, "spectrum bypassed: bands at the floor, audio passes");
      h.en = 1; h.mode = 0; h.run(.1, sine); h.run(1, sine); check(h.maxBand() <= -79.9f && h.valid > .5 && h.maxOut == 0, "tuner mode: bands at the floor, muted");
      h.mode = 1; h.run(.1, sine); check(h.valid < .5 && h.maxStep < .01, "tuner -> spectrum: valid cleared, fade in without click"); }
    { Host h(d, 48000, 64); h.mode = 1; h.run(20, [](double t) { return float(.2 * sin(2 * M_PI * 1000 * t)); });
      printf("CPU spectrum %.3f %% of realtime, worst 64-frame block %.0f us\n", h.total / 20 * 100, h.worst * 1e6); }
    { Host h(d, 48000, 64); h.run(20, sine); printf("CPU active %.3f %% of realtime, worst 64-frame block %.0f us\n", h.total / 20 * 100, h.worst * 1e6); }
    { Host h(d, 48000, 64); h.run(20, [](double) { return 0.f; }); printf("CPU silent %.3f %%\n", h.total / 20 * 100); }
    { Host h(d, 48000, 64); h.en = 0; h.run(20, sine); printf("CPU bypassed %.3f %%\n", h.total / 20 * 100); }
    return fails != 0;
}
