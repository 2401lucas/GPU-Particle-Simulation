//
// Created by 2401Lucas on 2025-10-31.
//

#ifndef GPU_PARTICLE_SIM_PIPELINE_H
#define GPU_PARTICLE_SIM_PIPELINE_H

#include <string>
#include <vector>
#include "Texture.h"

enum class PipelineStage {
    Top, // Beginning of pipeline
    VertexShader,
    PixelShader,
    ComputeShader,
    RenderTarget,
    DepthStencil,
    Transfer,
    Bottom, // End of pipeline
};

enum class BlendMode {
    None,
    Alpha,
    Additive,
    Multiply
};

enum class CullMode {
    None,
    Front,
    Back
};

enum class CompareFunc {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
};

enum class PrimitiveTopology {
    TriangleList,
    TriangleStrip,
    LineList,
    LineStrip,
    PointList
};

enum class ShaderStage {
    Vertex,
    Pixel,
    Compute,
    Geometry,
    Hull,
    Domain
};

enum class InputRate {
    PerVertex,
    PerInstance
};

struct Shader {
    std::string filepath = "";
    std::string entry = "main";
    ShaderStage stage;

    bool IsValid() {
        return !filepath.empty();
    }
};

struct PipelineCreateInfo {
    Shader vertexShader;
    Shader pixelShader;
    Shader computeShader;

    // Input layout
    struct VertexBufferLayout {
        uint32_t binding;
        uint32_t stride;
        InputRate rate; // per-vertex or per-instance
    };

    struct VertexAttribute {
        const char *semantic;
        uint32_t index;
        TextureFormat format;
        uint32_t binding;
        uint32_t offset;
    };

    std::vector<VertexAttribute> vertexAttributes;
    std::vector<VertexBufferLayout> vertexBuffers;

    // Rasterizer state
    CullMode cullMode = CullMode::Back;
    bool wireframe = false;
    uint32_t sampleCount = 1;

    PrimitiveTopology topology = PrimitiveTopology::TriangleList;

    // Depth/Stencil state
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    CompareFunc depthFunc = CompareFunc::Less;

    // Blend state
    BlendMode blendMode = BlendMode::None;

    // Render target formats
    TextureFormat renderTargetFormats[8] = {};
    uint32_t renderTargetCount = 1;
    TextureFormat depthStencilFormat = TextureFormat::Depth32;

    bool dynamicViewport = true;
    bool dynamicScissor = true;

    const char *debugName = nullptr;
};

class Pipeline {
public:
    virtual ~Pipeline() = default;
};

#endif //GPU_PARTICLE_SIM_PIPELINE_H
