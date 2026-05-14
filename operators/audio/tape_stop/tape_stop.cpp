#include "operator_api/operator.h"
#include "operator_api/metronome_sync.h"
#include "../glitch_common/glitch_dsp.h"

// ---------------------------------------------------------------------------
// TapeStop: tape deck slowdown/speedup simulation
//
// State machine: Passthrough -> Stopping -> Stopped -> Starting -> Passthrough
// Stopping: cubic deceleration (rate = (1-progress)^3), pitch drops naturally.
// Starting: quadratic acceleration (rate = progress^2).
// Stopped: 100ms silence gap.
// ---------------------------------------------------------------------------

/**
 * @brief Emulates tape-style slowdowns and restarts on incoming audio.
 *
 * TapeStop ramps playback speed down or back up over a timed window, creating classic DJ stop
 * and restart gestures without leaving the graph.
 *
 * @param phase Beat phase used for sync and triggering.
 * @param chance Probability of triggering the effect.
 * @param stop_time Time spent decelerating.
 * @param start_time Time spent accelerating back up.
 * @param mode Chooses stop-start, stop-only, or start-only behavior.
 * @param mix Dry/wet blend.
 */
struct TapeStop : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "TapeStop";
    static constexpr bool kTimeDependent = false;
    static constexpr uint32_t kMaxChannels = 2;

    vivid::Param<float> phase     {"phase",      0.0f,  0.0f,  1.0f};
    vivid::Param<int>   clock     {"clock",      0, {"External","Metronome"}};
    vivid::Param<float> chance    {"chance",     0.3f,  0.0f,  1.0f};
    vivid::Param<float> stop_time {"stop_time",  0.5f,  0.05f, 2.0f};
    vivid::Param<float> start_time{"start_time", 0.2f,  0.05f, 1.0f};
    vivid::Param<int>   mode      {"mode",       0, {"StopStart","Stop","Start"}};
    vivid::Param<float> mix       {"mix",        1.0f,  0.0f,  1.0f};

    glitch::CircularBuffer buf_[kMaxChannels];
    glitch::WhiteNoise     rng_;
    float prev_phase_ = 0.0f;

    enum State { Pass, Stopping, Stopped, Starting };
    State    state_         = Pass;
    uint32_t state_pos_     = 0;
    uint32_t state_len_     = 0;
    double   read_offset_   = 0.0; // fractional offset into buffer for varispeed
    uint32_t stopped_count_ = 0;
    uint32_t stopped_len_   = 0;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&clock);
        out.push_back(&chance);
        out.push_back(&stop_time);
        out.push_back(&start_time);
        out.push_back(&mode);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        uint32_t nch = ctx->input_channel_counts ? ctx->input_channel_counts[0] : 1u;
        if (nch > kMaxChannels) nch = kMaxChannels;
        for (uint32_t c = 0; c < nch; c++) buf_[c].init(ctx->sample_rate);

        uint32_t frames = ctx->buffer_size;
        uint32_t sr = ctx->sample_rate;
        auto  metro = vivid::metronome_transport(ctx);
        float cur_phase = (clock.int_value() == 1) ? metro.beat_phase : phase.value;
        float wet = mix.value;
        float dry = 1.0f - wet;
        int   cur_mode = mode.int_value();

        if (glitch::detect_trigger(cur_phase, prev_phase_) && state_ == Pass) {
            if (rng_.next_unipolar() < chance.value) {
                if (cur_mode == 2) {
                    state_     = Starting;
                    state_pos_ = 0;
                    state_len_ = static_cast<uint32_t>(start_time.value * sr);
                    if (state_len_ < 1) state_len_ = 1;
                    read_offset_ = 0.0;
                } else {
                    state_     = Stopping;
                    state_pos_ = 0;
                    state_len_ = static_cast<uint32_t>(stop_time.value * sr);
                    if (state_len_ < 1) state_len_ = 1;
                    read_offset_ = 0.0;
                }
            }
        }

        for (uint32_t i = 0; i < frames; i++) {
            // Write all channels to their buffers
            for (uint32_t c = 0; c < nch; c++) {
                const float* in_c = ctx->input_buffers[0] + c * frames;
                buf_[c].write(in_c[i]);
            }

            if (state_ == Stopping) {
                float progress = static_cast<float>(state_pos_) / static_cast<float>(state_len_);
                float rate = (1.0f - progress) * (1.0f - progress) * (1.0f - progress);

                read_offset_ += rate;
                uint32_t frames_back = static_cast<uint32_t>(state_len_ - read_offset_);
                if (frames_back >= buf_[0].size) frames_back = buf_[0].size - 1;
                uint32_t read_abs = buf_[0].get_read_pos(frames_back);

                for (uint32_t c = 0; c < nch; c++) {
                    const float* in_c  = ctx->input_buffers[0]  + c * frames;
                    float*       out_c = ctx->output_buffers[0] + c * frames;
                    float sample = buf_[c].read(static_cast<double>(read_abs));
                    out_c[i] = in_c[i] * dry + sample * wet;
                }
                state_pos_++;
                if (state_pos_ >= state_len_) {
                    if (cur_mode == 1) {
                        state_ = Pass;
                    } else {
                        state_         = Stopped;
                        stopped_count_ = 0;
                        stopped_len_   = sr / 10;
                    }
                }
            } else if (state_ == Stopped) {
                for (uint32_t c = 0; c < nch; c++) {
                    const float* in_c  = ctx->input_buffers[0]  + c * frames;
                    float*       out_c = ctx->output_buffers[0] + c * frames;
                    out_c[i] = in_c[i] * dry;
                }
                if (++stopped_count_ >= stopped_len_) {
                    state_     = Starting;
                    state_pos_ = 0;
                    state_len_ = static_cast<uint32_t>(start_time.value * sr);
                    if (state_len_ < 1) state_len_ = 1;
                    read_offset_ = 0.0;
                }
            } else if (state_ == Starting) {
                float progress = static_cast<float>(state_pos_) / static_cast<float>(state_len_);
                float rate = progress * progress;

                read_offset_ += rate;
                uint32_t frames_back = static_cast<uint32_t>(state_len_ - read_offset_);
                if (frames_back >= buf_[0].size) frames_back = buf_[0].size - 1;
                uint32_t read_abs = buf_[0].get_read_pos(frames_back);

                for (uint32_t c = 0; c < nch; c++) {
                    const float* in_c  = ctx->input_buffers[0]  + c * frames;
                    float*       out_c = ctx->output_buffers[0] + c * frames;
                    float sample = buf_[c].read(static_cast<double>(read_abs));
                    out_c[i] = in_c[i] * dry + sample * wet;
                }
                state_pos_++;
                if (state_pos_ >= state_len_) state_ = Pass;
            } else {
                for (uint32_t c = 0; c < nch; c++) {
                    const float* in_c  = ctx->input_buffers[0]  + c * frames;
                    float*       out_c = ctx->output_buffers[0] + c * frames;
                    out_c[i] = in_c[i];
                }
            }
        }

        prev_phase_ = cur_phase;
    }
};

VIVID_REGISTER(TapeStop)
