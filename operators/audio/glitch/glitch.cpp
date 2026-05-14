#include "operator_api/operator.h"
#include "operator_api/metronome_sync.h"
#include "../glitch_common/glitch_dsp.h"

#include <cmath>

// ---------------------------------------------------------------------------
// Glitch: meta-effect — six independently-triggered parallel effects
//
// On phase wrap: each sub-effect independently rolls its own chance parameter.
// Any subset may trigger simultaneously; their outputs are additively mixed.
// All size/duration parameters are expressed as beat divisions and resolved
// against the measured tempo (TempoTracker fed by the phase input).
// ---------------------------------------------------------------------------

// bpm > 0: metronome mode (use BPM directly); bpm == 0: external (use TempoTracker)
static inline uint32_t tempo_samples(int div_index,
                                     const glitch::TempoTracker& tempo,
                                     float bpm, uint32_t sr) {
    if (bpm > 0.0f) return glitch::samples_from_bpm(bpm, div_index, sr);
    float mul      = glitch::division_multiplier(glitch::division_from_index(div_index));
    float fallback = mul * 0.5f;
    return glitch::resolve_tempo_locked_samples(true, fallback, div_index, tempo, sr, 1);
}

// ---------------------------------------------------------------------------
// GlitchRepeat: capture a beat-sized slice, loop it N times with decay
// read_one() reads current position; advance() steps state once per sample.
// ---------------------------------------------------------------------------
struct GlitchRepeat {
    vivid::Param<float> chance  {"repeat_chance",   0.2f,  0.0f, 1.0f};
    vivid::Param<int>   division{"repeat_division",  3, {"1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/4D","1/8D"}};
    vivid::Param<int>   count   {"repeat_count",     4, 2, 16};
    vivid::Param<float> decay   {"repeat_decay",   0.15f,  0.0f, 1.0f};

    uint32_t slice_start_ = 0, slice_len_ = 0, slice_pos_ = 0;
    int      rep_current_ = 0, total_reps_ = 0;
    float    rep_gain_    = 1.0f;
    bool     active_      = false;

    bool is_active() const { return active_; }

    void try_trigger(glitch::WhiteNoise& rng, uint32_t sr,
                     const glitch::CircularBuffer& buf, const glitch::TempoTracker& tempo,
                     float bpm) {
        if (active_ || rng.next_unipolar() >= chance.value) return;
        slice_len_   = tempo_samples(division.int_value(), tempo, bpm, sr);
        slice_start_ = buf.get_read_pos(slice_len_);
        slice_pos_   = 0;
        total_reps_  = count.int_value();
        rep_current_ = 0;
        rep_gain_    = 1.0f;
        active_      = true;
    }

    float read_one(const glitch::CircularBuffer& buf) const {
        double rp = static_cast<double>(slice_start_) + slice_pos_;
        float  s  = buf.read(rp);
        return s * glitch::crossfade_coeff(slice_pos_, slice_len_, 64) * rep_gain_;
    }

    void advance() {
        if (!active_) return;
        if (++slice_pos_ >= slice_len_) {
            slice_pos_ = 0;
            rep_gain_ *= (1.0f - decay.value);
            if (++rep_current_ >= total_reps_) active_ = false;
        }
    }
};

// ---------------------------------------------------------------------------
// GlitchReverse: capture a slice, play it backwards
// ---------------------------------------------------------------------------
struct GlitchReverse {
    vivid::Param<float> chance       {"reverse_chance",       0.15f, 0.0f, 1.0f};
    vivid::Param<int>   division     {"reverse_division",      2, {"1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/4D","1/8D"}};
    vivid::Param<float> transition_ms{"reverse_transition_ms", 6.0f, 0.0f, 40.0f};

    uint32_t slice_end_ = 0, slice_len_ = 0, slice_pos_ = 0;
    bool     active_    = false;

    bool is_active() const { return active_; }

    void try_trigger(glitch::WhiteNoise& rng, uint32_t sr,
                     const glitch::CircularBuffer& buf, const glitch::TempoTracker& tempo,
                     float bpm) {
        if (active_ || rng.next_unipolar() >= chance.value) return;
        slice_len_ = tempo_samples(division.int_value(), tempo, bpm, sr);
        slice_end_ = buf.get_read_pos(1);
        slice_pos_ = 0;
        active_    = true;
    }

    float read_one(uint32_t sr, const glitch::CircularBuffer& buf) const {
        uint32_t fade = static_cast<uint32_t>(transition_ms.value * 0.001f * sr);
        if (fade < 1) fade = 1;
        float s = buf.read_reverse(slice_end_, static_cast<double>(slice_pos_));
        return s * glitch::crossfade_coeff(slice_pos_, slice_len_, fade);
    }

    void advance() {
        if (!active_) return;
        if (++slice_pos_ >= slice_len_) active_ = false;
    }
};

// ---------------------------------------------------------------------------
// GlitchStutter: capture a beat-division slice, repeat it N times
// ---------------------------------------------------------------------------
struct GlitchStutter {
    vivid::Param<float> chance    {"stutter_chance",    0.15f, 0.0f, 1.0f};
    vivid::Param<int>   division  {"stutter_division",   4, {"1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/4D","1/8D"}};
    vivid::Param<int>   count     {"stutter_count",      8, 2, 32};
    vivid::Param<int>   envelope  {"stutter_envelope",   0, {"Decay","Build","Flat","Triangle"}};
    vivid::Param<float> env_amount{"stutter_env_amount", 0.5f, 0.0f, 1.0f};

    uint32_t slice_start_ = 0, slice_len_ = 0, slice_pos_ = 0;
    int      rep_current_ = 0, total_reps_ = 0;
    bool     active_      = false;

    bool is_active() const { return active_; }

    float envelope_gain(int rep, int total) const {
        if (total <= 1) return 1.0f;
        float progress = static_cast<float>(rep) / static_cast<float>(total - 1);
        float amt = env_amount.value;
        switch (envelope.int_value()) {
            case 0: return 1.0f - progress * amt;
            case 1: return (1.0f - amt) + progress * amt;
            case 2: return 1.0f;
            case 3: {
                float tri = (progress < 0.5f) ? progress * 2.0f : 2.0f - progress * 2.0f;
                return (1.0f - amt) + tri * amt;
            }
            default: return 1.0f;
        }
    }

    void try_trigger(glitch::WhiteNoise& rng, uint32_t sr,
                     const glitch::CircularBuffer& buf, const glitch::TempoTracker& tempo,
                     float bpm) {
        if (active_ || rng.next_unipolar() >= chance.value) return;
        slice_len_   = tempo_samples(division.int_value(), tempo, bpm, sr);
        slice_start_ = buf.get_read_pos(slice_len_);
        slice_pos_   = 0;
        total_reps_  = count.int_value();
        rep_current_ = 0;
        active_      = true;
    }

    float read_one(const glitch::CircularBuffer& buf) const {
        double rp = static_cast<double>(slice_start_) + slice_pos_;
        float  s  = buf.read(rp);
        return s * glitch::crossfade_coeff(slice_pos_, slice_len_, 64)
                 * envelope_gain(rep_current_, total_reps_);
    }

    void advance() {
        if (!active_) return;
        if (++slice_pos_ >= slice_len_) {
            slice_pos_ = 0;
            if (++rep_current_ >= total_reps_) active_ = false;
        }
    }
};

// ---------------------------------------------------------------------------
// GlitchScratch: varispeed DJ-style scratch gesture
// ---------------------------------------------------------------------------
struct GlitchScratch {
    vivid::Param<float> chance    {"scratch_chance",     0.1f, 0.0f, 1.0f};
    vivid::Param<int>   division  {"scratch_division",    2, {"1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/4D","1/8D"}};
    vivid::Param<float> speed     {"scratch_speed",       1.0f, 0.125f, 4.0f};
    vivid::Param<float> speed_rand{"scratch_speed_rand",  0.3f, 0.0f,   1.0f};
    vivid::Param<int>   motion    {"scratch_motion",      0, {"BackForth","Forward","Backward","Random"}};

    static constexpr float kSpeedPool[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
    static constexpr int   kSpeedPoolSize = 6;

    glitch::WhiteNoise rng_;
    uint32_t region_start_ = 0, region_len_ = 0;
    uint32_t total_len_ = 0, total_pos_ = 0;
    double   read_pos_  = 0.0;
    float    direction_ = 1.0f, cur_speed_ = 1.0f;
    bool     active_    = false;

    bool is_active() const { return active_; }

    float pick_speed() {
        float rand_amt = speed_rand.value;
        if (rand_amt > 0.0f) {
            int idx = static_cast<int>(rng_.next_unipolar() * kSpeedPoolSize);
            if (idx >= kSpeedPoolSize) idx = kSpeedPoolSize - 1;
            return speed.value * (1.0f - rand_amt) + kSpeedPool[idx] * rand_amt;
        }
        return speed.value;
    }

    void try_trigger(glitch::WhiteNoise& /*rng*/, uint32_t sr,
                     const glitch::CircularBuffer& buf, const glitch::TempoTracker& tempo,
                     float bpm) {
        if (active_ || rng_.next_unipolar() >= chance.value) return;
        uint32_t dur  = tempo_samples(division.int_value(), tempo, bpm, sr);
        region_len_   = dur * 2;
        if (region_len_ >= buf.size) region_len_ = buf.size - 1;
        region_start_ = buf.get_read_pos(region_len_);
        total_len_    = dur;
        total_pos_    = 0;
        read_pos_     = 0.0;
        cur_speed_    = pick_speed();
        direction_    = (motion.int_value() == 2) ? -1.0f : 1.0f;
        active_       = true;
    }

    float read_one(const glitch::CircularBuffer& buf) const {
        float s = buf.read(static_cast<double>(region_start_) + read_pos_);
        return s * glitch::crossfade_coeff(total_pos_, total_len_, 128);
    }

    void advance() {
        if (!active_) return;
        int cur_motion = motion.int_value();
        if (cur_motion == 0) {
            float half = static_cast<float>(total_len_) * 0.5f;
            direction_ = (total_pos_ < static_cast<uint32_t>(half)) ? 1.0f : -1.0f;
        } else if (cur_motion == 3 && rng_.next_unipolar() < 0.02f) {
            direction_ = -direction_;
            cur_speed_ = pick_speed();
        }
        read_pos_ += direction_ * cur_speed_;
        if (read_pos_ < 0.0) read_pos_ = 0.0;
        if (read_pos_ >= static_cast<double>(region_len_))
            read_pos_ = static_cast<double>(region_len_) - 1.0;
        if (++total_pos_ >= total_len_) active_ = false;
    }
};
constexpr float GlitchScratch::kSpeedPool[];

// ---------------------------------------------------------------------------
// GlitchTapeStop: cubic deceleration / quadratic acceleration with silence gap
// compute_abs_pos() advances shared state once and returns the read position.
// ---------------------------------------------------------------------------
struct GlitchTapeStop {
    vivid::Param<float> chance         {"tape_chance",        0.08f, 0.0f, 1.0f};
    vivid::Param<int>   stop_division  {"tape_stop_division",  1, {"1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/4D","1/8D"}};
    vivid::Param<int>   start_division {"tape_start_division", 1, {"1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/4D","1/8D"}};
    vivid::Param<int>   mode           {"tape_mode",           0, {"StopStart","StopOnly","StartOnly"}};

    enum TapeState { Pass, Stopping, Stopped, Starting };
    TapeState state_         = Pass;
    uint32_t  state_pos_     = 0, state_len_ = 0;
    uint32_t  start_len_     = 0;
    uint32_t  stopped_count_ = 0, stopped_len_ = 0;
    double    read_offset_   = 0.0;
    bool      active_        = false;

    // Kind of output for this sample
    enum OutputKind { Passthrough, FromBuf, Silent };
    struct FrameResult { OutputKind kind; uint32_t buf_abs; };

    bool is_active() const { return active_; }

    void try_trigger(glitch::WhiteNoise& rng, uint32_t sr,
                     const glitch::CircularBuffer& /*buf*/, const glitch::TempoTracker& tempo,
                     float bpm) {
        if (active_ || rng.next_unipolar() >= chance.value) return;
        int m = mode.int_value();
        start_len_   = tempo_samples(start_division.int_value(), tempo, bpm, sr);
        if (m == 2) {
            state_     = Starting;
            state_len_ = start_len_;
        } else {
            state_     = Stopping;
            state_len_ = tempo_samples(stop_division.int_value(), tempo, bpm, sr);
        }
        state_pos_   = 0;
        read_offset_ = 0.0;
        active_      = true;
    }

    // Advances shared state once per sample, returns the read descriptor.
    // Call this once outside the channel loop.
    FrameResult compute_frame(uint32_t sr, const glitch::CircularBuffer& buf) {
        if (state_ == Stopping) {
            float progress = static_cast<float>(state_pos_) / static_cast<float>(state_len_);
            float rate = (1.0f - progress) * (1.0f - progress) * (1.0f - progress);
            read_offset_ += rate;
            uint32_t fb = static_cast<uint32_t>(state_len_ - read_offset_);
            if (fb >= buf.size) fb = buf.size - 1;
            uint32_t abs = buf.get_read_pos(fb);
            if (++state_pos_ >= state_len_) {
                if (mode.int_value() == 1) { state_ = Pass; active_ = false; }
                else { state_ = Stopped; stopped_count_ = 0; stopped_len_ = sr / 10; }
            }
            return {FromBuf, abs};
        }
        if (state_ == Stopped) {
            if (++stopped_count_ >= stopped_len_) {
                state_ = Starting; state_pos_ = 0; state_len_ = start_len_; read_offset_ = 0.0;
            }
            return {Silent, 0};
        }
        if (state_ == Starting) {
            float progress = static_cast<float>(state_pos_) / static_cast<float>(state_len_);
            float rate = progress * progress;
            read_offset_ += rate;
            uint32_t fb = static_cast<uint32_t>(state_len_ - read_offset_);
            if (fb >= buf.size) fb = buf.size - 1;
            uint32_t abs = buf.get_read_pos(fb);
            if (++state_pos_ >= state_len_) { state_ = Pass; active_ = false; }
            return {FromBuf, abs};
        }
        return {Passthrough, 0};
    }
};

// ---------------------------------------------------------------------------
// GlitchFreqShift: 4-stage allpass Hilbert burst — per-channel filter state
// ---------------------------------------------------------------------------
static constexpr uint32_t kGlitchMaxChannels = 2;

struct GlitchFreqShift {
    vivid::Param<float> chance   {"shift_chance",    0.1f,    0.0f,    1.0f};
    vivid::Param<int>   division {"shift_division",   2, {"1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/4D","1/8D"}};
    vivid::Param<float> shift_hz {"shift_hz",        30.0f, -500.0f, 500.0f};
    vivid::Param<float> mod_depth{"shift_mod_depth",  0.0f,    0.0f,  200.0f};

    float  allpass_x_[kGlitchMaxChannels][4] = {};
    float  allpass_y_[kGlitchMaxChannels][4] = {};
    double osc_phase_[kGlitchMaxChannels]    = {};
    float  active_shift_ = 0.0f;
    uint32_t pos_ = 0, len_ = 0;
    bool   active_ = false;

    bool is_active() const { return active_; }

    void try_trigger(glitch::WhiteNoise& rng, uint32_t sr,
                     const glitch::CircularBuffer& /*buf*/, const glitch::TempoTracker& tempo,
                     float bpm) {
        if (active_ || rng.next_unipolar() >= chance.value) return;
        len_          = tempo_samples(division.int_value(), tempo, bpm, sr);
        pos_          = 0;
        active_shift_ = shift_hz.value;
        for (uint32_t c = 0; c < kGlitchMaxChannels; c++) {
            osc_phase_[c] = 0.0;
            for (int j = 0; j < 4; j++) allpass_x_[c][j] = allpass_y_[c][j] = 0.0f;
        }
        active_ = true;
    }

    // Process one sample for a specific channel. Caller must call advance() once per sample
    // after all channels are processed.
    float read_one(float in, uint32_t sr, uint32_t ch) {
        static const float a_coeffs[4] = {
            0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f
        };
        float x = in, q = x;
        for (int j = 0; j < 4; j++) {
            float y = a_coeffs[j] * (x - allpass_y_[ch][j]) + allpass_x_[ch][j];
            allpass_x_[ch][j] = x;
            allpass_y_[ch][j] = y;
            if (j == 1) q = y;
            x = y;
        }
        float progress = static_cast<float>(pos_) / static_cast<float>(len_);
        float mod = mod_depth.value * std::sin(progress * static_cast<float>(M_PI) * 4.0f);
        float shifted = x * static_cast<float>(std::cos(osc_phase_[ch] * 2.0 * M_PI))
                      - q * static_cast<float>(std::sin(osc_phase_[ch] * 2.0 * M_PI));
        osc_phase_[ch] += (active_shift_ + mod) / static_cast<double>(sr);
        osc_phase_[ch] -= std::floor(osc_phase_[ch]);
        return shifted;
    }

    void advance() {
        if (!active_) return;
        if (++pos_ >= len_) active_ = false;
    }
};

// ---------------------------------------------------------------------------
// Glitch: orchestrates the six sub-effects
// ---------------------------------------------------------------------------
struct Glitch : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Glitch";
    static constexpr bool kTimeDependent = false;
    static constexpr uint32_t kMaxChannels = kGlitchMaxChannels;

    vivid::Param<float> phase{"phase", 0.0f, 0.0f, 1.0f};
    vivid::Param<int>   clock{"clock", 0, {"External","Metronome"}};
    vivid::Param<float> mix  {"mix",   1.0f, 0.0f, 1.0f};

    GlitchRepeat    repeat_;
    GlitchReverse   reverse_;
    GlitchStutter   stutter_;
    GlitchScratch   scratch_;
    GlitchTapeStop  tape_;
    GlitchFreqShift shift_;

    glitch::CircularBuffer buf_[kMaxChannels];
    glitch::WhiteNoise     rng_;
    glitch::TempoTracker   tempo_;
    float prev_phase_ = 0.0f;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&clock);
        out.push_back(&mix);
        out.push_back(&repeat_.chance);
        out.push_back(&repeat_.division);
        out.push_back(&repeat_.count);
        out.push_back(&repeat_.decay);
        out.push_back(&reverse_.chance);
        out.push_back(&reverse_.division);
        out.push_back(&reverse_.transition_ms);
        out.push_back(&stutter_.chance);
        out.push_back(&stutter_.division);
        out.push_back(&stutter_.count);
        out.push_back(&stutter_.envelope);
        out.push_back(&stutter_.env_amount);
        out.push_back(&scratch_.chance);
        out.push_back(&scratch_.division);
        out.push_back(&scratch_.speed);
        out.push_back(&scratch_.speed_rand);
        out.push_back(&scratch_.motion);
        out.push_back(&tape_.chance);
        out.push_back(&tape_.stop_division);
        out.push_back(&tape_.start_division);
        out.push_back(&tape_.mode);
        out.push_back(&shift_.chance);
        out.push_back(&shift_.division);
        out.push_back(&shift_.shift_hz);
        out.push_back(&shift_.mod_depth);
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
        uint32_t sr     = ctx->sample_rate;

        auto  metro     = vivid::metronome_transport(ctx);
        float cur_phase = (clock.int_value() == vivid::kClockSourceMetronome)
                          ? metro.beat_phase : phase.value;
        float bpm       = (clock.int_value() == vivid::kClockSourceMetronome)
                          ? metro.bpm : 0.0f;

        float wet = mix.value;
        float dry = 1.0f - wet;

        bool trigger = glitch::detect_trigger(cur_phase, prev_phase_);
        tempo_.update_block(frames, trigger);

        if (trigger) {
            repeat_ .try_trigger(rng_, sr, buf_[0], tempo_, bpm);
            reverse_.try_trigger(rng_, sr, buf_[0], tempo_, bpm);
            stutter_.try_trigger(rng_, sr, buf_[0], tempo_, bpm);
            scratch_.try_trigger(rng_, sr, buf_[0], tempo_, bpm);
            tape_   .try_trigger(rng_, sr, buf_[0], tempo_, bpm);
            shift_  .try_trigger(rng_, sr, buf_[0], tempo_, bpm);
        }

        for (uint32_t i = 0; i < frames; i++) {
            // Write all channels to their buffers
            for (uint32_t c = 0; c < nch; c++) {
                const float* in_c = ctx->input_buffers[0] + c * frames;
                buf_[c].write(in_c[i]);
            }

            // Compute TapeStop's read descriptor once (advances shared state)
            GlitchTapeStop::FrameResult tape_frame{GlitchTapeStop::Passthrough, 0};
            if (tape_.is_active()) tape_frame = tape_.compute_frame(sr, buf_[0]);

            int n_active = (repeat_.is_active()  ? 1 : 0)
                         + (reverse_.is_active() ? 1 : 0)
                         + (stutter_.is_active() ? 1 : 0)
                         + (scratch_.is_active() ? 1 : 0)
                         + (tape_.is_active()    ? 1 : 0)
                         + (shift_.is_active()   ? 1 : 0);

            // Process each channel using current (un-advanced) shared positions
            for (uint32_t c = 0; c < nch; c++) {
                const float* in_c  = ctx->input_buffers[0]  + c * frames;
                float*       out_c = ctx->output_buffers[0] + c * frames;

                if (n_active > 0) {
                    float wet_sum = 0.0f;
                    if (repeat_ .is_active()) wet_sum += repeat_ .read_one(buf_[c]);
                    if (reverse_.is_active()) wet_sum += reverse_.read_one(sr, buf_[c]);
                    if (stutter_.is_active()) wet_sum += stutter_.read_one(buf_[c]);
                    if (scratch_.is_active()) wet_sum += scratch_.read_one(buf_[c]);
                    if (tape_.is_active()) {
                        switch (tape_frame.kind) {
                            case GlitchTapeStop::FromBuf:
                                wet_sum += buf_[c].read(static_cast<double>(tape_frame.buf_abs));
                                break;
                            case GlitchTapeStop::Silent:
                                break;
                            case GlitchTapeStop::Passthrough:
                                wet_sum += in_c[i];
                                break;
                        }
                    }
                    if (shift_.is_active()) wet_sum += shift_.read_one(in_c[i], sr, c);
                    out_c[i] = in_c[i] * dry + (wet_sum / static_cast<float>(n_active)) * wet;
                } else {
                    out_c[i] = in_c[i];
                }
            }

            // Advance shared state once per sample (after all channels read)
            repeat_ .advance();
            reverse_.advance();
            stutter_.advance();
            scratch_.advance();
            shift_  .advance();
            // tape_ state was already advanced by compute_frame() above
        }

        prev_phase_ = cur_phase;
    }
};

VIVID_REGISTER(Glitch)
