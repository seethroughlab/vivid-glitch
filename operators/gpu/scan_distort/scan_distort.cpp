#include "operator_api/wgsl_filter.h"
#include "../glitch_common/glitch_gpu.h"

struct ScanDistort : vivid::WgslFilterBase {
    static constexpr const char* kName   = "Scan Distort";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> amount      {"amount",      0.05f, 0.0f,   0.5f};
    vivid::Param<float> frequency   {"frequency",  10.0f,  1.0f, 100.0f};
    vivid::Param<float> speed       {"speed",       1.0f,  0.0f,  10.0f};
    vivid::Param<int>   mode        {"mode",        0, {"Noise", "Sine", "Square", "Sawtooth"}};
    vivid::Param<float> band_height {"band_height", 0.1f,  0.0f,   1.0f};
    vivid::Param<float> band_count  {"band_count",  3.0f,  1.0f,  20.0f};
    vivid::Param<float> jitter      {"jitter",      0.0f,  0.0f,   1.0f};
    vivid::Param<float> mix         {"mix",         1.0f,  0.0f,   1.0f};

    ScanDistort() : WgslFilterBase("scan_distort.wgsl") {
        set_shader_path_override(glitch_gpu::wgsl_path_from_cpp(__FILE__));
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&amount);
        out.push_back(&frequency);
        out.push_back(&speed);
        out.push_back(&mode);
        out.push_back(&band_height);
        out.push_back(&band_count);
        out.push_back(&jitter);
        out.push_back(&mix);
    }
};

VIVID_REGISTER(ScanDistort)
