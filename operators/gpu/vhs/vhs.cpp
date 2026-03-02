#include "operator_api/wgsl_filter.h"
#include "../glitch_common/glitch_gpu.h"

struct VHS : vivid::WgslFilterBase {
    static constexpr const char* kName   = "VHS";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> tracking     {"tracking",      0.3f, 0.0f, 1.0f};
    vivid::Param<float> color_bleed  {"color_bleed",   0.5f, 0.0f, 1.0f};
    vivid::Param<float> noise        {"noise",         0.2f, 0.0f, 1.0f};
    vivid::Param<float> head_switch  {"head_switch",   0.3f, 0.0f, 1.0f};
    vivid::Param<float> luma_sharpen {"luma_sharpen",  0.3f, 0.0f, 1.0f};
    vivid::Param<float> wobble       {"wobble",        0.2f, 0.0f, 1.0f};
    vivid::Param<float> speed        {"speed",         1.0f, 0.1f, 5.0f};
    vivid::Param<float> mix          {"mix",           1.0f, 0.0f, 1.0f};

    VHS() : WgslFilterBase("vhs.wgsl") {
        set_shader_path_override(glitch_gpu::wgsl_path_from_cpp(__FILE__));
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&tracking);
        out.push_back(&color_bleed);
        out.push_back(&noise);
        out.push_back(&head_switch);
        out.push_back(&luma_sharpen);
        out.push_back(&wobble);
        out.push_back(&speed);
        out.push_back(&mix);
    }
};

VIVID_REGISTER(VHS)
