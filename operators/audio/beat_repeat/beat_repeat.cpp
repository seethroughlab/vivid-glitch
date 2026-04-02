#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "../glitch_common/glitch_dsp.h"

// ---------------------------------------------------------------------------
// BeatRepeat: beat-synced slice repeater with per-repeat volume decay
//
// Simpler than Stutter — captures a slice on trigger, replays it `count`
// times with each repetition multiplied by (1-decay)^n.
// ---------------------------------------------------------------------------

/**
 * @brief Repeats captured audio slices with beat-aware timing and repeat decay.
 *
 * BeatRepeat grabs a segment of audio, loops it in sync with the beat grid, and can fade each
 * repeat so the result stays rhythmic instead of washing out the source.
 *
 * @param phase Beat phase used for sync and triggering.
 * @param chance Probability of triggering a repeat phrase.
 * @param size Slice size relative to the beat grid.
 * @param division Beat division used when sync is enabled.
 * @param count Number of repeats.
 * @param decay Per-repeat amplitude decay.
 */
struct BeatRepeat : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "BeatRepeat";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> phase {"phase",  0.0f,  0.0f,  1.0f};
    vivid::Param<float> chance{"chance", 0.5f,  0.0f,  1.0f};
    vivid::Param<float> size  {"size",   0.15f, 0.02f, 1.0f};
    vivid::Param<int>   sync  {"sync",   0, {"Off","On"}};
    vivid::Param<int>   division {"division", 3, {"1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/4D","1/8D"}};
    vivid::Param<int>   count {"count",  4,     1,     16};
    vivid::Param<float> decay {"decay",  0.1f,  0.0f,  1.0f};
    vivid::Param<float> mix   {"mix",    1.0f,  0.0f,  1.0f};

    glitch::CircularBuffer buf_;
    glitch::WhiteNoise     rng_;
    glitch::TempoTracker   tempo_;
    float prev_phase_ = 0.0f;

    enum State { Passthrough, Repeating };
    State    state_       = Passthrough;
    uint32_t slice_start_ = 0;
    uint32_t slice_len_   = 0;
    uint32_t slice_pos_   = 0;
    int      current_rep_ = 0;
    int      total_reps_  = 0;
    float    rep_gain_    = 1.0f;
    float    decay_factor_= 1.0f;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&chance);
        out.push_back(&size);
        out.push_back(&sync);
        out.push_back(&division);
        out.push_back(&count);
        out.push_back(&decay);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        buf_.init(ctx->sample_rate);

        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        uint32_t frames = ctx->buffer_size;

        float cur_phase = phase.value;
        float wet = mix.value;
        float dry = 1.0f - wet;
        int   cnt = count.int_value();
        bool trigger_now = glitch::detect_trigger(cur_phase, prev_phase_);
        tempo_.update_block(frames, trigger_now);
        uint32_t slice_samples = glitch::resolve_tempo_locked_samples(
            sync.int_value() > 0, size.value, division.int_value(), tempo_,
            ctx->sample_rate, 1, buf_.size > 0 ? (buf_.size - 1) : 0);

        if (trigger_now && state_ == Passthrough) {
            if (rng_.next_unipolar() < chance.value) {
                state_        = Repeating;
                slice_len_    = slice_samples;
                slice_start_  = buf_.get_read_pos(slice_len_);
                slice_pos_    = 0;
                current_rep_  = 0;
                total_reps_   = cnt;
                rep_gain_     = 1.0f;
                decay_factor_ = 1.0f - decay.value;
            }
        }

        for (uint32_t i = 0; i < frames; i++) {
            buf_.write(in[i]);

            if (state_ == Repeating) {
                double read_pos = static_cast<double>(slice_start_) + slice_pos_;
                float sample = buf_.read(read_pos);

                float cf = glitch::crossfade_coeff(slice_pos_, slice_len_, 64);
                sample *= cf * rep_gain_;

                out[i] = in[i] * dry + sample * wet;

                slice_pos_++;
                if (slice_pos_ >= slice_len_) {
                    slice_pos_ = 0;
                    current_rep_++;
                    rep_gain_ *= decay_factor_;
                    if (current_rep_ >= total_reps_) {
                        state_ = Passthrough;
                    }
                }
            } else {
                out[i] = in[i];
            }
        }

        prev_phase_ = cur_phase;
    }
};

VIVID_REGISTER(BeatRepeat)
