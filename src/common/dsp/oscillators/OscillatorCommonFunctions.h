/*
 * Surge XT - a free and open source hybrid synthesizer,
 * built by Surge Synth Team
 *
 * Learn more at https://surge-synthesizer.github.io/
 *
 * Copyright 2018-2023, various authors, as described in the GitHub
 * transaction log.
 *
 * Surge XT is released under the GNU General Public Licence v3
 * or later (GPL-3.0-or-later). The license is found in the "LICENSE"
 * file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 *
 * Surge was a commercial product from 2004-2018, copyright and ownership
 * held by Claes Johanson at Vember Audio during that period.
 * Claes made Surge open source in September 2018.
 *
 * All source for Surge XT is available at
 * https://github.com/surge-synthesizer/surge
 */

#ifndef SURGE_SRC_COMMON_DSP_OSCILLATORS_OSCILLATORCOMMONFUNCTIONS_H
#define SURGE_SRC_COMMON_DSP_OSCILLATORS_OSCILLATORCOMMONFUNCTIONS_H

#include "DSPUtils.h"
#include "SurgeStorage.h"

namespace Surge
{
namespace Oscillator
{
struct DriftLFO
{
    DriftLFO() noexcept : d(0), d2(0) {}

    inline void init(bool nzi)
    {
        d = 0;
        d2 = 0;
        if (nzi)
            d2 = 0.0005 * ((float)rand() / (float)(RAND_MAX));
    }

    inline float drift_noise(float &lastval)
    {
        constexpr float filter = 0.00001f;
        constexpr float m = 316.227766017f; // 1.f / sqrt(filter);
        float rand11 = (((float)rand() / (float)RAND_MAX) * 2.f - 1.f);

        lastval = lastval * (1.f - filter) + rand11 * filter;

        return lastval * m;
    }

    inline float next()
    {
        d = drift_noise(d2);
        return d;
    }

    inline float val() const { return d; }

    float d, d2;
};

/*
 * Generate coefficients and, in non-sse cases, operate the character filter
 */
template <typename valtype> struct CharacterFilter
{
    SurgeStorage *storage;
    CharacterFilter(SurgeStorage *s) noexcept : storage(s) {}

    void init(int itype)
    {
        type = itype;
        switch (type)
        {
        case 0:
        {
            valtype filt = 1.0 - 2.0 * 5000.0 * storage->dsamplerate_inv;
            filt *= filt;
            CoefB0 = 1.0 - filt;
            CoefB1 = 0.0;
            CoefA1 = filt;
            doFilter = true;
        }
        break;
        case 2:
        {
            valtype filt = 1.0 - 2.0 * 5000.0 * storage->dsamplerate_inv;
            filt *= filt;
            valtype A0 = 1.0 / (1.0 - filt);
            CoefB0 = 1.0 * A0;
            CoefB1 = -filt * A0;
            CoefA1 = 0.0;
            doFilter = true;
        }
        break;
        default:
        case 1:
        {
            CoefB0 = 1.0;
            CoefB1 = 0.0;
            CoefA1 = 0.0;
            doFilter = false;
        }
        break;
        }
    }

    int type = 1;
    bool doFilter = false;
    valtype CoefB0 = 1.0, CoefB1 = 0.0, CoefA1 = 0.0;

    inline void process_block(float *output, size_t size)
    {
        if (!doFilter)
            return;
        if (starting)
        {
            priorY_L = output[0];
            priorX_L = output[0];
        }
        starting = false;
        for (auto i = 0; i < size; ++i)
        {
            auto pfL = CoefA1 * priorY_L + CoefB0 * output[i] + CoefB1 * priorX_L;
            priorY_L = pfL;
            priorX_L = output[i];
            output[i] = pfL;
        }
    }

    inline void process_block_stereo(float *output, float *outputR, size_t size)
    {
        if (!doFilter)
            return;

        if (starting)
        {
            priorY_L = output[0];
            priorX_L = output[0];
            priorY_R = outputR[0];
            priorX_R = outputR[0];
        }
        starting = false;
        for (auto i = 0; i < size; ++i)
        {
            auto pfL = CoefA1 * priorY_L + CoefB0 * output[i] + CoefB1 * priorX_L;
            priorY_L = pfL;
            priorX_L = output[i];
            output[i] = pfL;

            auto pfR = CoefA1 * priorY_R + CoefB0 * outputR[i] + CoefB1 * priorX_R;
            priorY_R = pfR;
            priorX_R = outputR[i];
            outputR[i] = pfR;
        }
    }

    bool starting = false;
    valtype priorY_L = 0.0, priorX_L = 0.0, priorY_R = 0.0, priorX_R = 0.0;
};

template <typename valtype> struct UnisonSetup
{
    UnisonSetup(int nv) : n_unison(nv)
    {
        auto voices = n_unison;
        sqrt_uni = sqrt(1.0 * n_unison);
        sqrt_uni_inv = 1.0 / sqrt_uni;

        odd = voices & 1;
        mid = voices * 0.5 - 0.5;
        half = voices >> 1;
    }

    inline valtype detuneBias() const
    {
        if (n_unison == 1)
            return 1.0;
        return 2.0 / (n_unison - 1);
    }
    inline valtype detuneOffset() const
    {
        if (n_unison == 1)
            return 0;
        return -1;
    }
    inline valtype detune(int voice) const { return detuneBias() * voice + detuneOffset(); }

    inline void panLaw(int voice, valtype &panL, valtype &panR) const
    {
        if (n_unison == 1)
        {
            panL = 1.0;
            panR = 1.0;
            return;
        }
        float d = fabs((valtype)voice - mid) / mid;
        if (odd && (voice >= half))
            d = -d;
        if (voice & 1)
            d = -d;

        panL = (1.f - d);
        panR = (1.f + d);
    }

    inline valtype attenuation() const { return sqrt_uni_inv; }

    inline valtype attenuation_inv() const { return sqrt_uni; }

    inline void attenuatedPanLaw(int voice, valtype &panL, valtype &panR) const
    {
        panLaw(voice, panL, panR);
        panL *= attenuation();
        panR *= attenuation();
    }

    bool odd;
    valtype mid;
    int half;
    int n_unison = 1;
    double sqrt_uni, sqrt_uni_inv;
};

} // namespace Oscillator
} // namespace Surge

#endif // SURGE_OSCILLATORCOMMONFUNCTIONS_H
