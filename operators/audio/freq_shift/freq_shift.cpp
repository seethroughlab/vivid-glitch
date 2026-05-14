#include "operator_api/operator.h"
#include "operator_api/audio_dsp.h"

#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// FreqShift: Bode frequency shifter via Hilbert transform
//
// 31-tap FIR Hilbert transform with Blackman window produces an analytic
// signal (I/Q pair). Multiply by a complex oscillator at the shift frequency
// to move all frequencies up or down. LFO modulates shift amount.
//
// No circular buffer needed — purely algebraic with a short FIR delay line.
// ---------------------------------------------------------------------------

static constexpr int kHilbertTaps = 31;
static constexpr int kHilbertHalf = kHilbertTaps / 2; // 15

/**
 * @brief Applies frequency shifting with optional modulation.
 *
 * FreqShift moves every spectral component by a fixed amount rather than preserving harmonic
 * spacing, making it useful for metallic, alien, and sideband-heavy textures.
 *
 * @param phase Beat phase input for modulation timing.
 * @param shift Amount of frequency shift in hertz.
 * @param mod_depth Depth of shift modulation.
 * @param mod_rate Modulation rate.
 * @param mix Dry/wet blend.
 */
struct FreqShift : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "FreqShift";
    static constexpr bool kTimeDependent = false;
    static constexpr uint32_t kMaxChannels = 2;

    vivid::Param<float> phase    {"phase",     0.0f,    0.0f,    1.0f};
    vivid::Param<float> shift    {"shift",     20.0f, -500.0f, 500.0f};
    vivid::Param<float> mod_depth{"mod_depth",  0.0f,   0.0f, 200.0f};
    vivid::Param<float> mod_rate {"mod_rate",   2.0f,   0.1f,  20.0f};
    vivid::Param<float> mix      {"mix",        1.0f,   0.0f,   1.0f};

    // Hilbert FIR coefficients — shared (constant)
    float hilbert_coeff_[kHilbertTaps] = {};
    bool  coeffs_ready_ = false;

    // Per-channel FIR delay line and oscillator state
    float  delay_line_[kMaxChannels][kHilbertTaps] = {};
    int    delay_idx_[kMaxChannels]                 = {};
    double osc_phase_[kMaxChannels]                 = {};
    double lfo_phase_[kMaxChannels]                 = {};

    void compute_coefficients() {
        if (coeffs_ready_) return;
        coeffs_ready_ = true;

        for (int i = 0; i < kHilbertTaps; i++) {
            int n = i - kHilbertHalf;
            if (n == 0 || (n % 2) == 0) {
                hilbert_coeff_[i] = 0.0f;
            } else {
                hilbert_coeff_[i] = 2.0f / (static_cast<float>(M_PI) * n);
            }
            float w = 0.42f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * i / (kHilbertTaps - 1))
                            + 0.08f * std::cos(4.0f * static_cast<float>(M_PI) * i / (kHilbertTaps - 1));
            hilbert_coeff_[i] *= w;
        }
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&shift);
        out.push_back(&mod_depth);
        out.push_back(&mod_rate);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        compute_coefficients();

        uint32_t nch = ctx->input_channel_counts ? ctx->input_channel_counts[0] : 1u;
        if (nch > kMaxChannels) nch = kMaxChannels;

        uint32_t frames = ctx->buffer_size;
        double sr = ctx->sample_rate;
        double inv_sr = 1.0 / sr;

        float base_shift = shift.value;
        float depth = mod_depth.value;
        float rate  = mod_rate.value;
        float wet   = mix.value;
        float dry   = 1.0f - wet;

        for (uint32_t c = 0; c < nch; c++) {
            const float* in_c  = ctx->input_buffers[0]  + c * frames;
            float*       out_c = ctx->output_buffers[0] + c * frames;
            int&         didx  = delay_idx_[c];
            double&      osc   = osc_phase_[c];
            double&      lfo   = lfo_phase_[c];
            float*       dline = delay_line_[c];

            for (uint32_t i = 0; i < frames; i++) {
                dline[didx] = in_c[i];

                float q_signal = 0.0f;
                for (int t = 0; t < kHilbertTaps; t++) {
                    int idx = didx - t;
                    if (idx < 0) idx += kHilbertTaps;
                    q_signal += dline[idx] * hilbert_coeff_[t];
                }

                int i_idx = didx - kHilbertHalf;
                if (i_idx < 0) i_idx += kHilbertTaps;
                float i_signal = dline[i_idx];

                float lfo_val = static_cast<float>(std::sin(lfo * 2.0 * M_PI));
                float effective_shift = base_shift + lfo_val * depth;

                float cos_osc = static_cast<float>(std::cos(osc * 2.0 * M_PI));
                float sin_osc = static_cast<float>(std::sin(osc * 2.0 * M_PI));

                float shifted = i_signal * cos_osc - q_signal * sin_osc;
                out_c[i] = in_c[i] * dry + shifted * wet;

                osc += effective_shift * inv_sr;
                osc -= std::floor(osc);

                lfo += rate * inv_sr;
                lfo -= std::floor(lfo);

                if (++didx >= kHilbertTaps) didx = 0;
            }
        }
    }
};

VIVID_REGISTER(FreqShift)
