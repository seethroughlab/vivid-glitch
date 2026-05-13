#include "operator_api/operator.h"
#include "operator_api/metronome_sync.h"
#include "../glitch_common/glitch_dsp.h"

// ---------------------------------------------------------------------------
// BeatRepeat: beat-synced slice repeater with per-repeat volume decay
// ---------------------------------------------------------------------------

/**
 * @brief Repeats captured audio slices with beat-aware timing and repeat decay.
 *
 * @param phase Beat phase used for triggering when clock is External.
 * @param clock Clock source: Free (size in seconds), External (wired phase), Metronome (global).
 * @param chance Probability of triggering a repeat phrase.
 * @param size Slice size in seconds (used in Free mode and as fallback in External mode).
 * @param division Beat division used in External and Metronome modes.
 * @param count Number of repeats.
 * @param decay Per-repeat amplitude decay.
 */
struct BeatRepeat : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "BeatRepeat";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> phase   {"phase",    0.0f,  0.0f,  1.0f};
    vivid::Param<int>   clock   {"clock",    0, {"Free","External","Metronome"}};
    vivid::Param<float> chance  {"chance",   0.5f,  0.0f,  1.0f};
    vivid::Param<float> size    {"size",     0.15f, 0.02f, 1.0f};
    vivid::Param<int>   division{"division", 3, {"1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/4D","1/8D"}};
    vivid::Param<int>   count   {"count",    4, 1, 16};
    vivid::Param<float> decay   {"decay",    0.1f,  0.0f,  1.0f};
    vivid::Param<float> mix     {"mix",      1.0f,  0.0f,  1.0f};

    glitch::CircularBuffer buf_;
    glitch::WhiteNoise     rng_;
    glitch::TempoTracker   tempo_;
    float prev_phase_ = 0.0f;

    enum State { Passthrough, Repeating };
    State    state_        = Passthrough;
    uint32_t slice_start_  = 0;
    uint32_t slice_len_    = 0;
    uint32_t slice_pos_    = 0;
    int      current_rep_  = 0;
    int      total_reps_   = 0;
    float    rep_gain_     = 1.0f;
    float    decay_factor_ = 1.0f;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&clock);
        out.push_back(&chance);
        out.push_back(&size);
        out.push_back(&division);
        out.push_back(&count);
        out.push_back(&decay);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        buf_.init(ctx->sample_rate);

        float*   in     = ctx->input_buffers[0];
        float*   out    = ctx->output_buffers[0];
        uint32_t frames = ctx->buffer_size;
        uint32_t sr     = ctx->sample_rate;

        int  clk = clock.int_value();
        auto metro = vivid::metronome_transport(ctx);
        float cur_phase = (clk == 2) ? metro.beat_phase : phase.value;

        float wet = mix.value;
        float dry = 1.0f - wet;
        bool trigger_now = glitch::detect_trigger(cur_phase, prev_phase_);
        tempo_.update_block(frames, trigger_now);

        uint32_t slice_samples;
        if (clk == 2) {
            slice_samples = glitch::samples_from_bpm(metro.bpm, division.int_value(), sr);
        } else {
            slice_samples = glitch::resolve_tempo_locked_samples(
                clk == 1, size.value, division.int_value(), tempo_,
                sr, 1, buf_.size > 0 ? buf_.size - 1 : 0);
        }

        if (trigger_now && state_ == Passthrough) {
            if (rng_.next_unipolar() < chance.value) {
                state_        = Repeating;
                slice_len_    = slice_samples;
                slice_start_  = buf_.get_read_pos(slice_len_);
                slice_pos_    = 0;
                current_rep_  = 0;
                total_reps_   = count.int_value();
                rep_gain_     = 1.0f;
                decay_factor_ = 1.0f - decay.value;
            }
        }

        for (uint32_t i = 0; i < frames; i++) {
            buf_.write(in[i]);

            if (state_ == Repeating) {
                float sample = buf_.read(static_cast<double>(slice_start_) + slice_pos_);
                sample *= glitch::crossfade_coeff(slice_pos_, slice_len_, 64) * rep_gain_;
                out[i] = in[i] * dry + sample * wet;

                if (++slice_pos_ >= slice_len_) {
                    slice_pos_ = 0;
                    rep_gain_ *= decay_factor_;
                    if (++current_rep_ >= total_reps_) state_ = Passthrough;
                }
            } else {
                out[i] = in[i];
            }
        }

        prev_phase_ = cur_phase;
    }
};

VIVID_REGISTER(BeatRepeat)
