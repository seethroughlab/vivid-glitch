#include "operator_api/wgsl_filter.h"
#include "../glitch_common/glitch_gpu.h"

/**
 * @brief Simulates JPEG-style block corruption and color breakup.
 *
 * JPEG Glitch introduces block quantization artifacts, channel offsets, and corruption patterns to
 * create compressed-image aesthetics without leaving the GPU chain.
 *
 * @param amount Overall strength of the effect.
 * @param block_size Size of the simulated compression blocks.
 * @param corruption Amount of corruption introduced per block.
 * @param quantize Strength of color quantization.
 * @param mix Dry/wet blend.
 */
struct JpegGlitch : vivid::WgslFilterBase {
    static constexpr const char* kName   = "JPEG Glitch";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> amount         {"amount",         0.5f, 0.0f,  1.0f};
    vivid::Param<float> block_size     {"block_size",     8.0f, 4.0f, 32.0f};
    vivid::Param<float> corruption     {"corruption",     0.3f, 0.0f,  1.0f};
    vivid::Param<float> quantize       {"quantize",       0.5f, 0.0f,  1.0f};
    vivid::Param<float> channel_offset {"channel_offset", 0.3f, 0.0f,  1.0f};
    vivid::Param<float> seed           {"seed",           0.0f, 0.0f, 100.0f};
    vivid::Param<float> mix            {"mix",            1.0f, 0.0f,  1.0f};

    JpegGlitch() : WgslFilterBase("jpeg_glitch.wgsl") {
        set_shader_path_override(glitch_gpu::wgsl_path_from_cpp(__FILE__));
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&amount);
        out.push_back(&block_size);
        out.push_back(&corruption);
        out.push_back(&quantize);
        out.push_back(&channel_offset);
        out.push_back(&seed);
        out.push_back(&mix);
    }
};

VIVID_REGISTER(JpegGlitch)
