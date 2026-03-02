#include "operator_api/wgsl_filter.h"
#include "../glitch_common/glitch_gpu.h"

struct ChannelShift : vivid::WgslFilterBase {
    static constexpr const char* kName   = "Channel Shift";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> r_offset_x    {"r_offset_x",     0.02f, -0.5f, 0.5f};
    vivid::Param<float> r_offset_y    {"r_offset_y",     0.0f,  -0.5f, 0.5f};
    vivid::Param<float> g_offset_x    {"g_offset_x",     0.0f,  -0.5f, 0.5f};
    vivid::Param<float> g_offset_y    {"g_offset_y",     0.0f,  -0.5f, 0.5f};
    vivid::Param<float> b_offset_x    {"b_offset_x",    -0.02f, -0.5f, 0.5f};
    vivid::Param<float> b_offset_y    {"b_offset_y",     0.0f,  -0.5f, 0.5f};
    vivid::Param<float> animate       {"animate",        0.0f,   0.0f, 1.0f};
    vivid::Param<float> animate_speed {"animate_speed",  1.0f,   0.1f, 10.0f};
    vivid::Param<int>   wrap          {"wrap",           0, {"Clamp", "Repeat", "Mirror"}};
    vivid::Param<float> mix           {"mix",            1.0f,   0.0f, 1.0f};

    ChannelShift() : WgslFilterBase("channel_shift.wgsl") {
        set_shader_path_override(glitch_gpu::wgsl_path_from_cpp(__FILE__));
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        display_hint(r_offset_x, VIVID_DISPLAY_XY_PAD);
        display_hint(r_offset_y, VIVID_DISPLAY_XY_PAD);
        display_hint(g_offset_x, VIVID_DISPLAY_XY_PAD);
        display_hint(g_offset_y, VIVID_DISPLAY_XY_PAD);
        display_hint(b_offset_x, VIVID_DISPLAY_XY_PAD);
        display_hint(b_offset_y, VIVID_DISPLAY_XY_PAD);

        out.push_back(&r_offset_x);
        out.push_back(&r_offset_y);
        out.push_back(&g_offset_x);
        out.push_back(&g_offset_y);
        out.push_back(&b_offset_x);
        out.push_back(&b_offset_y);
        out.push_back(&animate);
        out.push_back(&animate_speed);
        out.push_back(&wrap);
        out.push_back(&mix);
    }
};

VIVID_REGISTER(ChannelShift)
