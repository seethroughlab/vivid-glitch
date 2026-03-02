#include "operator_api/wgsl_filter.h"
#include "../glitch_common/glitch_gpu.h"

struct PixelSort : vivid::WgslFilterBase {
    static constexpr const char* kName   = "Pixel Sort";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> threshold_lo {"threshold_lo", 0.2f, 0.0f,   1.0f};
    vivid::Param<float> threshold_hi {"threshold_hi", 0.8f, 0.0f,   1.0f};
    vivid::Param<float> amount       {"amount",       0.5f, 0.0f,   1.0f};
    vivid::Param<int>   direction    {"direction",    0, {"Horizontal", "Vertical", "Diagonal"}};
    vivid::Param<float> reverse      {"reverse",      0.0f, 0.0f,   1.0f};
    vivid::Param<float> smoothing    {"smoothing",    0.5f, 0.0f,   1.0f};
    vivid::Param<float> seed         {"seed",         0.0f, 0.0f, 100.0f};
    vivid::Param<float> mix          {"mix",          1.0f, 0.0f,   1.0f};

    PixelSort() : WgslFilterBase("pixel_sort.wgsl") {
        set_shader_path_override(glitch_gpu::wgsl_path_from_cpp(__FILE__));
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&threshold_lo);
        out.push_back(&threshold_hi);
        out.push_back(&amount);
        out.push_back(&direction);
        out.push_back(&reverse);
        out.push_back(&smoothing);
        out.push_back(&seed);
        out.push_back(&mix);
    }
};

VIVID_REGISTER(PixelSort)
