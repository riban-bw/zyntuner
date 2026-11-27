/*
 * zyntuner - low CPU spectrum analyser: log spaced bands from a Hann windowed FFT, spread over the audio blocks
 *
 * Copyright (C) 2026 riban
 * License: GPL-3.0-or-later
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

// RT-safe: fixed buffers, no allocation. Per sample: one store in a ring buffer. Every hop (half the FFT size,
// ~43 ms) a frame is analysed in small steps (window, FFT stages, bands), finished within half a hop.
// Real input FFT: an N/2 point complex FFT of the even / odd samples, split into the N/2 bins the bands use.
class SpectrumAnalyser
{
public:
    static constexpr int kMaxBands = 64;
    static constexpr float kFallRate = 40.0f;  // dB/s: peaks fall slowly, rises are shown at once

    // FFT size: 4096 at 44.1 / 48 kHz (10.8 / 11.7 Hz per bin), 8192 at 88.2 / 96 kHz and above
    void setSampleRate(const double sampleRate, const int bands, const float minFreq, const float maxFreq,
                       const float floorDb)
    {
        fSize = 4096;
        while (fSize < kMaxSize && sampleRate / fSize > 16.0)
            fSize *= 2;
        fHalf = fSize / 2;
        fBits = 0;  // stages of the N/2 point FFT
        while ((1 << fBits) < fHalf)
            ++fBits;
        fHop = fSize / 2;
        fFloor = floorDb;
        fFallStep = static_cast<float>(kFallRate * fHop / sampleRate);
        // Power of a full scale sine (summed over the bins around it) = 1: Hann sum(w^2) = 3N/8
        fScale = 32.0f / (3.0f * static_cast<float>(fSize) * static_cast<float>(fSize));

        for (int n = 0; n < fSize; ++n)
            fWindow[n] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * M_PI * n / fSize));
        for (int n = 0; n < fHalf; ++n)
        {
            int r = 0;
            for (int b = 0; b < fBits; ++b)
                r |= ((n >> b) & 1) << (fBits - 1 - b);
            fBitRev[n] = static_cast<uint16_t>(r);
            fCos[n] = static_cast<float>(std::cos(2.0 * M_PI * n / fSize));
            fSin[n] = static_cast<float>(-std::sin(2.0 * M_PI * n / fSize));
        }

        // Bins of each band: those with their centre inside it, else the bin nearest to its centre
        fBands = std::min(bands, kMaxBands);
        const double df = sampleRate / fSize, ratio = std::pow(maxFreq / minFreq, 1.0 / fBands);
        const int nyquist = fHalf;
        for (int b = 0; b < fBands; ++b)
        {
            const double lo = minFreq * std::pow(ratio, b), hi = lo * ratio;
            int k0 = std::max(1, static_cast<int>(std::ceil(lo / df)));
            int k1 = std::min(nyquist, static_cast<int>(std::ceil(hi / df)));
            if (k1 <= k0)
            {
                k0 = std::max(1, static_cast<int>(std::sqrt(lo * hi) / df + 0.5));
                k1 = k0 + 1;
            }
            if (lo >= sampleRate / 2 || k0 >= nyquist)
                k0 = k1 = 0;  // above Nyquist: stays at the floor
            fBinLo[b] = k0;
            fBinHi[b] = k1;
        }
        // The bands are computed in two steps with about the same number of bins each
        int total = 0, sum = 0;
        for (int b = 0; b < fBands; ++b)
            total += fBinHi[b] - fBinLo[b];
        fBandSplit = 0;
        while (fBandSplit < fBands && 2 * (sum + fBinHi[fBandSplit] - fBinLo[fBandSplit]) <= total)
        {
            sum += fBinHi[fBandSplit] - fBinLo[fBandSplit];
            ++fBandSplit;
        }
        reset();
    }

    void reset()
    {
        std::memset(fRing, 0, sizeof(fRing));
        fWrite = fSinceHop = fFill = 0;
        fStep = -1;
        for (int b = 0; b < kMaxBands; ++b)
            fLevel[b] = fFloor;
        fUpdated = true;
    }

    inline void push(const float x)
    {
        fRing[fWrite] = x;
        fWrite = (fWrite + 1) & (fSize - 1);
        if (fFill < fSize)
            ++fFill;
        ++fSinceHop;
    }

    // Once per block, after pushing its samples: does only this block's share of the analysis
    void process(const uint32_t frames)
    {
        if (fStep < 0)
        {
            if (fSinceHop < fHop || fFill < fSize)
                return;
            fSinceHop -= fHop;
            fStep = 0;
        }
        // Steps: window + bit reversal, one per FFT stage, bands in two. Finish within half a hop.
        const int steps = fBits + 3;
        int budget = std::max(1, static_cast<int>(std::ceil(2.0 * steps * frames / fHop)));
        while (fStep >= 0 && budget-- > 0)
        {
            if (fStep == 0)
                loadFrame();
            else if (fStep <= fBits)
                fftStage(fStep - 1);
            else if (fStep == fBits + 1)
                computeBands(0, fBandSplit);
            else
                computeBands(fBandSplit, fBands);
            if (++fStep >= steps)
                fStep = -1;
        }
    }

    // True once after each new analysis (or reset): the band levels changed
    bool takeUpdate()
    {
        const bool u = fUpdated;
        fUpdated = false;
        return u;
    }

    // Level of a band in dB (fFloor .. 0 dBFS: 0 = full scale sine)
    float level(const int band) const { return fLevel[band]; }

private:
    static constexpr int kMaxSize = 8192;

    // Windowed frame, packed as N/2 complex values (even samples real, odd imaginary), bit reversed
    void loadFrame()
    {
        int r = fWrite;  // oldest sample of the last fSize
        const int mask = fSize - 1;
        for (int n = 0; n < fHalf; ++n)
        {
            const int i = fBitRev[n];
            fRe[i] = fRing[r] * fWindow[2 * n];
            fIm[i] = fRing[(r + 1) & mask] * fWindow[2 * n + 1];
            r = (r + 2) & mask;
        }
    }

    // N/2 point radix-2 FFT, decimation in time, in place: one stage
    void fftStage(const int s)
    {
        const int half = 1 << s, span = half << 1, twStep = fSize / span;
        for (int j = 0; j < half; ++j)
        {
            const float wr = fCos[j * twStep], wi = fSin[j * twStep];
            for (int k = j; k < fHalf; k += span)
            {
                const int m = k + half;
                const float tr = wr * fRe[m] - wi * fIm[m];
                const float ti = wr * fIm[m] + wi * fRe[m];
                fRe[m] = fRe[k] - tr;
                fIm[m] = fIm[k] - ti;
                fRe[k] += tr;
                fIm[k] += ti;
            }
        }
    }

    void computeBands(const int first, const int end)
    {
        for (int b = first; b < end; ++b)
        {
            float power = 0.0f;
            for (int k = fBinLo[b]; k < fBinHi[b]; ++k)
            {
                // Real FFT bin k (1 .. N/2-1) from Z[k] and Z[N/2-k]: X = E + W^k O
                const float ar = fRe[k], ai = fIm[k], br = fRe[fHalf - k], bi = -fIm[fHalf - k];
                const float er = 0.5f * (ar + br), ei = 0.5f * (ai + bi);
                const float orr = 0.5f * (ai - bi), oi = -0.5f * (ar - br);
                const float xr = er + fCos[k] * orr - fSin[k] * oi;
                const float xi = ei + fCos[k] * oi + fSin[k] * orr;
                power += xr * xr + xi * xi;
            }
            const float db = fBinHi[b] > fBinLo[b]
                ? std::max(fFloor, std::min(0.0f, 10.0f * std::log10(power * fScale + 1e-20f))) : fFloor;
            fLevel[b] = std::max(db, fLevel[b] - fFallStep);
        }
        if (end == fBands)
            fUpdated = true;
    }

    int fSize = 4096, fHalf = 2048, fBits = 11, fHop = 2048, fBands = 0, fBandSplit = 0;
    int fWrite = 0, fSinceHop = 0, fFill = 0, fStep = -1;
    float fFloor = -80.0f, fFallStep = 1.0f, fScale = 1.0f;
    bool fUpdated = false;
    int fBinLo[kMaxBands] = {}, fBinHi[kMaxBands] = {};
    float fLevel[kMaxBands] = {};
    float fRing[kMaxSize] = {}, fWindow[kMaxSize] = {};
    float fRe[kMaxSize / 2] = {}, fIm[kMaxSize / 2] = {}, fCos[kMaxSize / 2] = {}, fSin[kMaxSize / 2] = {};
    uint16_t fBitRev[kMaxSize / 2] = {};
};
