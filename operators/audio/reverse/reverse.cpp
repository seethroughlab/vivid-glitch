#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "../glitch_common/glitch_dsp.h"

// ---------------------------------------------------------------------------
// Reverse: beat-synced reverse playback
//
// On trigger: captures a slice and plays it backwards with 128-sample
// crossfade at start/end for click-free transitions.
// ---------------------------------------------------------------------------

struct Reverse : vivid::OperatorBase {
    static constexpr const char* kName   = "Reverse";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> phase {"phase",  0.0f,  0.0f, 1.0f};
    vivid::Param<float> chance{"chance", 0.5f,  0.0f, 1.0f};
    vivid::Param<float> size  {"size",   0.25f, 0.05f, 2.0f};
    vivid::Param<int>   sync  {"sync",   0, {"Off","On"}};
    vivid::Param<int>   division {"division", 2, {"1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/4D","1/8D"}};
    vivid::Param<float> transition_ms{"transition_ms", 6.0f, 0.0f, 40.0f};
    vivid::Param<float> mix   {"mix",    1.0f,  0.0f, 1.0f};

    glitch::CircularBuffer buf_;
    glitch::WhiteNoise     rng_;
    glitch::TempoTracker   tempo_;
    float prev_phase_ = 0.0f;

    enum State { Passthrough, Reversing };
    State    state_       = Passthrough;
    uint32_t slice_end_   = 0; // end of captured slice (where we start reading backward)
    uint32_t slice_len_   = 0;
    uint32_t slice_pos_   = 0;

    Reverse() {
        vivid::semantic_tag(phase, "phase_01");
        vivid::semantic_shape(phase, "scalar");

        vivid::semantic_tag(chance, "probability_01");
        vivid::semantic_shape(chance, "scalar");

        vivid::semantic_tag(transition_ms, "time_milliseconds");
        vivid::semantic_shape(transition_ms, "scalar");
        vivid::semantic_unit(transition_ms, "ms");

        vivid::semantic_tag(mix, "amplitude_linear");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_dry_mix");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&chance);
        out.push_back(&size);
        out.push_back(&sync);
        out.push_back(&division);
        out.push_back(&transition_ms);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        auto* audio = vivid_audio(ctx);
        if (!audio) return;

        buf_.init(audio->sample_rate);

        float* in  = audio->input_buffers[0];
        float* out = audio->output_buffers[0];
        uint32_t frames = audio->buffer_size;

        float cur_phase = phase.value;
        float wet = mix.value;
        float dry = 1.0f - wet;
        bool trigger_now = glitch::detect_trigger(cur_phase, prev_phase_);
        tempo_.update_block(frames, trigger_now);
        uint32_t slice_samples = glitch::resolve_tempo_locked_samples(
            sync.int_value() > 0, size.value, division.int_value(), tempo_,
            audio->sample_rate, 1, buf_.size > 0 ? (buf_.size - 1) : 0);

        if (trigger_now && state_ == Passthrough) {
            if (rng_.next_unipolar() < chance.value) {
                state_     = Reversing;
                slice_len_ = slice_samples;
                // slice_end_ is the most recent sample (1 frame before write head)
                slice_end_ = buf_.get_read_pos(1);
                slice_pos_ = 0;
            }
        }

        uint32_t transition_samples = static_cast<uint32_t>(
            transition_ms.value * 0.001f * static_cast<float>(audio->sample_rate));

        for (uint32_t i = 0; i < frames; i++) {
            buf_.write(in[i]);

            if (state_ == Reversing) {
                // Read backwards from slice_end_
                float sample = buf_.read_reverse(slice_end_, static_cast<double>(slice_pos_));

                // 128-sample crossfade at edges
                float cf = glitch::crossfade_coeff(slice_pos_, slice_len_, 128);
                sample *= cf;
                float transition_gain = glitch::crossfade_coeff(slice_pos_, slice_len_, transition_samples);
                float wet_gain = wet * transition_gain;
                out[i] = in[i] * (1.0f - wet_gain) + sample * wet_gain;

                slice_pos_++;
                if (slice_pos_ >= slice_len_) {
                    state_ = Passthrough;
                }
            } else {
                out[i] = in[i];
            }
        }

        prev_phase_ = cur_phase;
    }
};

VIVID_REGISTER(Reverse)
