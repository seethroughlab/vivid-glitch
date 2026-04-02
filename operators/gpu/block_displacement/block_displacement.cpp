#include "operator_api/wgsl_filter.h"
#include "../glitch_common/glitch_gpu.h"

/**
 * @brief Offsets and duplicates rectangular regions of the image.
 *
 * Block Displacement breaks the input into a grid, shifts selected blocks, and can duplicate them
 * to create digital tearing and collage-like distortions.
 *
 * @param amount Overall displacement amount.
 * @param grid_x Number of blocks across the X axis.
 * @param grid_y Number of blocks across the Y axis.
 * @param direction Direction of displacement.
 * @param duplicate Amount of block duplication.
 * @param mix Dry/wet blend.
 */
struct BlockDisplacement : vivid::WgslFilterBase {
    static constexpr const char* kName   = "Block Displacement";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> amount    {"amount",    0.5f, 0.0f, 1.0f};
    vivid::Param<float> grid_x   {"grid_x",    8.0f, 2.0f, 64.0f};
    vivid::Param<float> grid_y   {"grid_y",    8.0f, 2.0f, 64.0f};
    vivid::Param<float> chance   {"chance",    0.3f, 0.0f, 1.0f};
    vivid::Param<int>   direction{"direction", 0, {"Both", "Horizontal", "Vertical"}};
    vivid::Param<float> duplicate{"duplicate", 0.3f, 0.0f, 1.0f};
    vivid::Param<float> seed     {"seed",      0.0f, 0.0f, 100.0f};
    vivid::Param<float> mix      {"mix",       1.0f, 0.0f, 1.0f};

    BlockDisplacement() : WgslFilterBase("block_displacement.wgsl") {
        set_shader_path_override(glitch_gpu::wgsl_path_from_cpp(__FILE__));
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&amount);
        out.push_back(&grid_x);
        out.push_back(&grid_y);
        out.push_back(&chance);
        out.push_back(&direction);
        out.push_back(&duplicate);
        out.push_back(&seed);
        out.push_back(&mix);
    }
};

VIVID_REGISTER(BlockDisplacement)
