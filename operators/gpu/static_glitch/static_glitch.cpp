#include "operator_api/wgsl_filter.h"
#include "../glitch_common/glitch_gpu.h"

struct StaticGlitch : vivid::WgslFilterBase {
    static constexpr const char* kName   = "Static Glitch";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> amount       {"amount",        0.5f, 0.0f,   1.0f};
    vivid::Param<float> density      {"density",       1.0f, 0.1f,   4.0f};
    vivid::Param<float> speed        {"speed",        30.0f, 1.0f, 120.0f};
    vivid::Param<int>   color_mode   {"color_mode",    0, {"Mono", "Color", "Channel"}};
    vivid::Param<float> scanline     {"scanline",      0.0f, 0.0f,   1.0f};
    vivid::Param<float> vertical_hold{"vertical_hold", 0.0f, 0.0f,   1.0f};
    vivid::Param<float> interference {"interference",  0.0f, 0.0f,   1.0f};
    vivid::Param<float> mix          {"mix",           1.0f, 0.0f,   1.0f};

    StaticGlitch() : WgslFilterBase("static_glitch.wgsl") {
        set_shader_path_override(glitch_gpu::wgsl_path_from_cpp(__FILE__));
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&amount);
        out.push_back(&density);
        out.push_back(&speed);
        out.push_back(&color_mode);
        out.push_back(&scanline);
        out.push_back(&vertical_hold);
        out.push_back(&interference);
        out.push_back(&mix);
    }
};

VIVID_REGISTER(StaticGlitch)
