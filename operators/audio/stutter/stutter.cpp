#include "operator_api/operator.h"
#include "operator_api/metronome_sync.h"
#include "../glitch_common/glitch_dsp.h"

// ---------------------------------------------------------------------------
// Stutter: beat-synced slice repeater with configurable volume envelopes
//
// Records input to a circular buffer continuously. On phase-wrap trigger:
// captures a slice and replays it `count` times with an envelope shape
// applied across the repetitions (decay, build, flat, triangle).
// ---------------------------------------------------------------------------

/**
 * @brief Repeats short slices of audio in tempo-synced bursts.
 *
 * Stutter captures a window of incoming audio, retriggers it for a configurable count, and shapes
 * the repeated slices with envelope options for rhythmic glitch textures.
 *
 * @param phase Beat phase used for sync and triggering.
 * @param chance Probability of triggering a stutter event.
 * @param size Slice length for each repeated chunk.
 * @param count Number of repeats per trigger.
 * @param envelope Envelope shape applied across the repeats.
 * @param mix Dry/wet blend.
 */
struct Stutter : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Stutter";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> phase     {"phase",      0.0f, 0.0f,  1.0f};
    vivid::Param<int>   clock     {"clock",      0, {"Free","External","Metronome"}};
    vivid::Param<float> chance    {"chance",     0.5f, 0.0f,  1.0f};
    vivid::Param<float> size      {"size",       0.1f, 0.02f, 1.0f};
    vivid::Param<int>   division  {"division",   3, {"1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/4D","1/8D"}};
    vivid::Param<int>   count     {"count",      8,    1,     32};
    vivid::Param<int>   envelope  {"envelope",   0, {"Decay","Build","Flat","Triangle"}};
    vivid::Param<float> env_amount{"env_amount", 0.5f, 0.0f,  1.0f};
    vivid::Param<float> mix       {"mix",        1.0f, 0.0f,  1.0f};

    glitch::CircularBuffer buf_;
    glitch::WhiteNoise     rng_;
    glitch::TempoTracker   tempo_;
    float prev_phase_ = 0.0f;

    enum State { Passthrough, Stuttering };
    State    state_         = Passthrough;
    uint32_t slice_start_   = 0;
    uint32_t slice_len_     = 0;
    uint32_t slice_pos_     = 0;  // position within current repetition
    int      current_rep_   = 0;  // which repetition we're on
    int      total_reps_    = 0;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&clock);
        out.push_back(&chance);
        out.push_back(&size);
        out.push_back(&division);
        out.push_back(&count);
        out.push_back(&envelope);
        out.push_back(&env_amount);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT});
    }

    float envelope_gain(int rep, int total, int shape, float amount) const {
        if (total <= 1) return 1.0f;
        float progress = static_cast<float>(rep) / static_cast<float>(total - 1);
        switch (shape) {
            case 0: // Decay
                return 1.0f - progress * amount;
            case 1: // Build
                return (1.0f - amount) + progress * amount;
            case 2: // Flat
                return 1.0f;
            case 3: { // Triangle
                float tri = (progress < 0.5f) ? (progress * 2.0f) : (2.0f - progress * 2.0f);
                return (1.0f - amount) + tri * amount;
            }
            default:
                return 1.0f;
        }
    }

    void process_audio(const VividAudioContext* ctx) override {
        buf_.init(ctx->sample_rate);

        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        uint32_t frames = ctx->buffer_size;

        int  clk = clock.int_value();
        auto metro = vivid::metronome_transport(ctx);
        float cur_phase = (clk == 2) ? metro.beat_phase : phase.value;

        float wet = mix.value;
        float dry = 1.0f - wet;
        int   cnt = count.int_value();
        int   env_shape = envelope.int_value();
        float env_amt = env_amount.value;
        bool trigger_now = glitch::detect_trigger(cur_phase, prev_phase_);
        tempo_.update_block(frames, trigger_now);

        uint32_t slice_samples;
        if (clk == 2) {
            slice_samples = glitch::samples_from_bpm(metro.bpm, division.int_value(), ctx->sample_rate);
        } else {
            slice_samples = glitch::resolve_tempo_locked_samples(
                clk == 1, size.value, division.int_value(), tempo_,
                ctx->sample_rate, 1, buf_.size > 0 ? buf_.size - 1 : 0);
        }

        // Trigger check at block boundary
        if (trigger_now && state_ == Passthrough) {
            if (rng_.next_unipolar() < chance.value) {
                state_       = Stuttering;
                slice_len_   = slice_samples;
                slice_start_ = buf_.get_read_pos(slice_len_);
                slice_pos_   = 0;
                current_rep_ = 0;
                total_reps_  = cnt;
            }
        }

        for (uint32_t i = 0; i < frames; i++) {
            buf_.write(in[i]);

            if (state_ == Stuttering) {
                // Read from captured slice
                double read_pos = static_cast<double>(slice_start_) + slice_pos_;
                float sample = buf_.read(read_pos);

                // Crossfade at edges
                float cf = glitch::crossfade_coeff(slice_pos_, slice_len_, 64);
                sample *= cf;

                // Envelope across repetitions
                float gain = envelope_gain(current_rep_, total_reps_, env_shape, env_amt);
                sample *= gain;

                out[i] = in[i] * dry + sample * wet;

                slice_pos_++;
                if (slice_pos_ >= slice_len_) {
                    slice_pos_ = 0;
                    current_rep_++;
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

VIVID_REGISTER(Stutter)
