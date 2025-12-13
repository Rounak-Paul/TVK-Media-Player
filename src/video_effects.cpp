#include "video_effects.h"
#include <tinyvk/renderer/shader_compiler.h>
#include <tinyvk/renderer/renderer.h>
#include <tinyvk/core/log.h>

namespace tvk_media {

static const char* g_effectsComputeShader = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0, rgba8) readonly uniform image2D sourceImage;
layout(binding = 1, rgba8) writeonly uniform image2D outputImage;

layout(push_constant) uniform PushConstants {
    float brightness;
    float contrast;
    float gamma;
    float exposure;
    
    float hue;
    float saturation;
    float temperature;
    float tint;
    
    float shadows;
    float highlights;
    int filterType;
    float filterStrength;
    
    float filterThreshold;
    int filterLevels;
    float vignette;
    float vignetteSize;
    
    float filmGrain;
    float chromaticAberration;
    float scanlines;
    float vintageStrength;
    
    int vintageEnabled;
    int width;
    int height;
    int frameCounter;
    
    float bloom;
    float bloomThreshold;
    float bloomRadius;
    int pad0;
} pc;

vec3 rgb_to_hsl(vec3 rgb) {
    float maxC = max(rgb.r, max(rgb.g, rgb.b));
    float minC = min(rgb.r, min(rgb.g, rgb.b));
    float l = (maxC + minC) * 0.5;
    
    if (maxC == minC) {
        return vec3(0.0, 0.0, l);
    }
    
    float d = maxC - minC;
    float s = l > 0.5 ? d / (2.0 - maxC - minC) : d / (maxC + minC);
    float h;
    
    if (maxC == rgb.r) {
        h = (rgb.g - rgb.b) / d + (rgb.g < rgb.b ? 6.0 : 0.0);
    } else if (maxC == rgb.g) {
        h = (rgb.b - rgb.r) / d + 2.0;
    } else {
        h = (rgb.r - rgb.g) / d + 4.0;
    }
    h /= 6.0;
    
    return vec3(h, s, l);
}

float hue_to_rgb(float p, float q, float t) {
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0/6.0) return p + (q - p) * 6.0 * t;
    if (t < 0.5) return q;
    if (t < 2.0/3.0) return p + (q - p) * (2.0/3.0 - t) * 6.0;
    return p;
}

vec3 hsl_to_rgb(vec3 hsl) {
    if (hsl.y == 0.0) {
        return vec3(hsl.z);
    }
    
    float q = hsl.z < 0.5 ? hsl.z * (1.0 + hsl.y) : hsl.z + hsl.y - hsl.z * hsl.y;
    float p = 2.0 * hsl.z - q;
    
    return vec3(
        hue_to_rgb(p, q, hsl.x + 1.0/3.0),
        hue_to_rgb(p, q, hsl.x),
        hue_to_rgb(p, q, hsl.x - 1.0/3.0)
    );
}

float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 apply_color_adjustments(vec3 color) {
    float exposureMult = pow(2.0, pc.exposure);
    color *= exposureMult;
    
    color = (color - 0.5) * pc.contrast + 0.5 + pc.brightness;
    
    color = pow(max(color, vec3(0.0)), vec3(1.0 / max(pc.gamma, 0.01)));
    
    color.r += pc.temperature * 0.1;
    color.b -= pc.temperature * 0.1;
    color.g += pc.tint * 0.1;
    
    if (pc.hue != 0.0 || pc.saturation != 1.0) {
        vec3 hsl = rgb_to_hsl(color);
        hsl.x = fract(hsl.x + pc.hue);
        hsl.y = clamp(hsl.y * pc.saturation, 0.0, 1.0);
        color = hsl_to_rgb(hsl);
    }
    
    if (pc.shadows != 0.0 || pc.highlights != 0.0) {
        float lum = dot(color, vec3(0.299, 0.587, 0.114));
        float shadowWeight = 1.0 - lum;
        float highlightWeight = lum;
        float adj = pc.shadows * shadowWeight * 0.5 + pc.highlights * highlightWeight * 0.5;
        color += adj;
    }
    
    return clamp(color, 0.0, 1.0);
}

vec3 apply_filter(vec3 color, ivec2 coord) {
    if (pc.filterType == 0) return color;
    
    if (pc.filterType == 1) {
        float gray = dot(color, vec3(0.299, 0.587, 0.114));
        return mix(color, vec3(gray), pc.filterStrength);
    }
    
    if (pc.filterType == 2) {
        vec3 sepia = vec3(
            dot(color, vec3(0.393, 0.769, 0.189)),
            dot(color, vec3(0.349, 0.686, 0.168)),
            dot(color, vec3(0.272, 0.534, 0.131))
        );
        return mix(color, sepia, pc.filterStrength);
    }
    
    if (pc.filterType == 3) {
        return mix(color, 1.0 - color, pc.filterStrength);
    }
    
    if (pc.filterType == 4) {
        float levels = float(pc.filterLevels);
        vec3 posterized = floor(color * levels) / (levels - 1.0);
        return posterized;
    }
    
    if (pc.filterType == 5) {
        vec3 result = color;
        if (color.r > pc.filterThreshold) result.r = 1.0 - color.r;
        if (color.g > pc.filterThreshold) result.g = 1.0 - color.g;
        if (color.b > pc.filterThreshold) result.b = 1.0 - color.b;
        return result;
    }
    
    if (pc.filterType == 6) {
        float gray = dot(color, vec3(0.299, 0.587, 0.114));
        return vec3(gray >= pc.filterThreshold ? 1.0 : 0.0);
    }
    
    if (pc.filterType == 7) {
        vec3 sum = vec3(0.0);
        sum += imageLoad(sourceImage, coord + ivec2(-1, -1)).rgb * 0.0;
        sum += imageLoad(sourceImage, coord + ivec2( 0, -1)).rgb * -1.0;
        sum += imageLoad(sourceImage, coord + ivec2( 1, -1)).rgb * 0.0;
        sum += imageLoad(sourceImage, coord + ivec2(-1,  0)).rgb * -1.0;
        sum += imageLoad(sourceImage, coord + ivec2( 0,  0)).rgb * 5.0;
        sum += imageLoad(sourceImage, coord + ivec2( 1,  0)).rgb * -1.0;
        sum += imageLoad(sourceImage, coord + ivec2(-1,  1)).rgb * 0.0;
        sum += imageLoad(sourceImage, coord + ivec2( 0,  1)).rgb * -1.0;
        sum += imageLoad(sourceImage, coord + ivec2( 1,  1)).rgb * 0.0;
        return clamp(sum, 0.0, 1.0);
    }
    
    if (pc.filterType == 8) {
        vec3 sum = vec3(0.0);
        sum += imageLoad(sourceImage, coord + ivec2(-1, -1)).rgb * -1.0;
        sum += imageLoad(sourceImage, coord + ivec2( 0, -1)).rgb * -1.0;
        sum += imageLoad(sourceImage, coord + ivec2( 1, -1)).rgb * -1.0;
        sum += imageLoad(sourceImage, coord + ivec2(-1,  0)).rgb * -1.0;
        sum += imageLoad(sourceImage, coord + ivec2( 0,  0)).rgb * 8.0;
        sum += imageLoad(sourceImage, coord + ivec2( 1,  0)).rgb * -1.0;
        sum += imageLoad(sourceImage, coord + ivec2(-1,  1)).rgb * -1.0;
        sum += imageLoad(sourceImage, coord + ivec2( 0,  1)).rgb * -1.0;
        sum += imageLoad(sourceImage, coord + ivec2( 1,  1)).rgb * -1.0;
        return clamp(sum, 0.0, 1.0);
    }
    
    return color;
}

vec3 sample_bloom(ivec2 coord, int scale) {
    ivec2 sampleCoord = clamp(coord, ivec2(0), ivec2(pc.width - 1, pc.height - 1));
    vec3 col = imageLoad(sourceImage, sampleCoord).rgb;
    float lum = dot(col, vec3(0.299, 0.587, 0.114));
    if (lum > pc.bloomThreshold) {
        return max(col - pc.bloomThreshold, vec3(0.0));
    }
    return vec3(0.0);
}

vec3 blur_at_scale(ivec2 coord, int scale) {
    vec3 accum = vec3(0.0);
    float totalWeight = 0.0;
    float sigma = float(scale) * 1.5;
    float sigma2 = 2.0 * sigma * sigma;
    
    int kernelSize = scale * 2;
    
    for (int y = -kernelSize; y <= kernelSize; y += scale) {
        for (int x = -kernelSize; x <= kernelSize; x += scale) {
            float dist2 = float(x * x + y * y);
            float weight = exp(-dist2 / sigma2);
            accum += sample_bloom(coord + ivec2(x, y), scale) * weight;
            totalWeight += weight;
        }
    }
    
    return accum / max(totalWeight, 0.001);
}

vec3 apply_post_process(vec3 color, ivec2 coord) {
    vec2 uv = vec2(coord) / vec2(pc.width, pc.height);
    
    if (pc.bloom > 0.0) {
        vec3 bloomAccum = vec3(0.0);
        
        float mipWeights[6] = float[](0.5, 0.3, 0.15, 0.1, 0.05, 0.025);
        int scales[6] = int[](1, 2, 4, 8, 16, 32);
        int numMips = int(pc.bloomRadius);
        numMips = clamp(numMips, 1, 6);
        
        float totalWeight = 0.0;
        for (int m = 0; m < numMips; m++) {
            bloomAccum += blur_at_scale(coord, scales[m]) * mipWeights[m];
            totalWeight += mipWeights[m];
        }
        
        bloomAccum /= totalWeight;
        color += bloomAccum * pc.bloom;
    }
    
    if (pc.chromaticAberration > 0.0) {
        vec2 center = vec2(pc.width, pc.height) * 0.5;
        vec2 dir = vec2(coord) - center;
        float dist = length(dir) / length(center);
        dir = normalize(dir);
        
        float offset = pc.chromaticAberration * 20.0 * dist;
        
        vec2 rOffset = dir * offset;
        vec2 bOffset = -dir * offset;
        
        ivec2 rCoord = ivec2(vec2(coord) - rOffset);
        ivec2 bCoord = ivec2(vec2(coord) + bOffset);
        rCoord = clamp(rCoord, ivec2(0), ivec2(pc.width - 1, pc.height - 1));
        bCoord = clamp(bCoord, ivec2(0), ivec2(pc.width - 1, pc.height - 1));
        color.r = imageLoad(sourceImage, rCoord).r;
        color.b = imageLoad(sourceImage, bCoord).b;
    }
    
    if (pc.vintageEnabled != 0) {
        vec3 vintage = vec3(
            0.9 * color.r + 0.05 * color.g + 0.05 * color.b + 0.05,
            0.05 * color.r + 0.85 * color.g + 0.05 * color.b + 0.02,
            0.1 * color.r + 0.1 * color.g + 0.7 * color.b - 0.02
        );
        color = mix(color, vintage, pc.vintageStrength);
        color = (color - 0.5) * (1.0 - pc.vintageStrength * 0.2) + 0.5;
    }
    
    if (pc.filmGrain > 0.0) {
        float noise = rand(uv + float(pc.frameCounter) * 0.01) - 0.5;
        color += noise * pc.filmGrain * 0.2;
    }
    
    if (pc.scanlines > 0.0 && (coord.y % 2) == 1) {
        color *= 1.0 - pc.scanlines * 0.5;
    }
    
    if (pc.vignette > 0.0) {
        vec2 center = vec2(0.5);
        float dist = distance(uv, center);
        float maxDist = 0.707;
        float innerRadius = maxDist * pc.vignetteSize;
        if (dist > innerRadius) {
            float t = (dist - innerRadius) / (maxDist - innerRadius);
            float v = 1.0 - t * pc.vignette;
            color *= max(v, 0.0);
        }
    }
    
    return clamp(color, 0.0, 1.0);
}

void main() {
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    
    if (coord.x >= pc.width || coord.y >= pc.height) {
        return;
    }
    
    vec4 pixel = imageLoad(sourceImage, coord);
    vec3 color = pixel.rgb;
    
    color = apply_color_adjustments(color);
    color = apply_filter(color, coord);
    color = apply_post_process(color, coord);
    
    imageStore(outputImage, coord, vec4(color, pixel.a));
}
)";

VideoEffects::VideoEffects()
    : _renderer(nullptr)
    , _context(nullptr)
    , _computePipeline(VK_NULL_HANDLE)
    , _pipelineLayout(VK_NULL_HANDLE)
    , _descriptorSetLayout(VK_NULL_HANDLE)
    , _descriptorSet(VK_NULL_HANDLE)
    , _shaderModule(VK_NULL_HANDLE)
    , _stagingImage(VK_NULL_HANDLE)
    , _stagingMemory(VK_NULL_HANDLE)
    , _stagingImageView(VK_NULL_HANDLE)
    , _stagingWidth(0)
    , _stagingHeight(0)
    , _lastSrcView(VK_NULL_HANDLE)
    , _lastDstView(VK_NULL_HANDLE)
    , _frameCounter(0)
    , _initialized(false)
{
}

VideoEffects::~VideoEffects() {
    Cleanup();
}

bool VideoEffects::Init(tvk::Renderer* renderer) {
    if (_initialized) return true;
    
    _renderer = renderer;
    _context = &renderer->GetContext();
    
    if (!CreateDescriptorSetLayout()) {
        TVK_LOG_ERROR("Failed to create descriptor set layout for video effects");
        return false;
    }
    
    if (!CreateComputePipeline()) {
        TVK_LOG_ERROR("Failed to create compute pipeline for video effects");
        return false;
    }
    
    if (!AllocateDescriptorSet()) {
        TVK_LOG_ERROR("Failed to allocate descriptor set for video effects");
        return false;
    }
    
    _initialized = true;
    TVK_LOG_INFO("Video effects GPU pipeline initialized");
    return true;
}

void VideoEffects::Cleanup() {
    if (!_context) return;
    
    VkDevice device = _context->GetDevice();
    if (!device) return;
    
    vkDeviceWaitIdle(device);
    
    DestroyStagingImage();
    
    if (_computePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, _computePipeline, nullptr);
        _computePipeline = VK_NULL_HANDLE;
    }
    
    if (_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, _pipelineLayout, nullptr);
        _pipelineLayout = VK_NULL_HANDLE;
    }
    
    if (_shaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, _shaderModule, nullptr);
        _shaderModule = VK_NULL_HANDLE;
    }
    
    if (_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, _descriptorSetLayout, nullptr);
        _descriptorSetLayout = VK_NULL_HANDLE;
    }
    
    _initialized = false;
}

bool VideoEffects::CreateDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding bindings[2]{};
    
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[0].pImmutableSamplers = nullptr;
    
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].pImmutableSamplers = nullptr;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    
    if (vkCreateDescriptorSetLayout(_context->GetDevice(), &layoutInfo, nullptr, &_descriptorSetLayout) != VK_SUCCESS) {
        return false;
    }
    
    return true;
}

bool VideoEffects::CreateComputePipeline() {
    _shaderModule = tvk::ShaderCompiler::CreateShaderModuleFromGLSL(
        _renderer, g_effectsComputeShader, tvk::ShaderStage::Compute, "video_effects"
    );
    
    if (_shaderModule == VK_NULL_HANDLE) {
        TVK_LOG_ERROR("Failed to compile video effects compute shader");
        return false;
    }
    
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(EffectsPushConstants);
    
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &_descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(_context->GetDevice(), &layoutInfo, nullptr, &_pipelineLayout) != VK_SUCCESS) {
        return false;
    }
    
    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = _shaderModule;
    stageInfo.pName = "main";
    
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = _pipelineLayout;
    
    if (vkCreateComputePipelines(_context->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_computePipeline) != VK_SUCCESS) {
        return false;
    }
    
    return true;
}

bool VideoEffects::AllocateDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = _context->GetDescriptorPool();
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &_descriptorSetLayout;
    
    if (vkAllocateDescriptorSets(_context->GetDevice(), &allocInfo, &_descriptorSet) != VK_SUCCESS) {
        return false;
    }
    
    return true;
}

void VideoEffects::UpdateDescriptorSet(VkImageView srcView, VkImageView dstView) {
    if (srcView == _lastSrcView && dstView == _lastDstView) return;
    _lastSrcView = srcView;
    _lastDstView = dstView;
    
    VkDescriptorImageInfo imageInfos[2]{};
    imageInfos[0].imageView = srcView;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[1].imageView = dstView;
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = _descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].dstArrayElement = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &imageInfos[0];
    
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = _descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].dstArrayElement = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &imageInfos[1];
    
    vkUpdateDescriptorSets(_context->GetDevice(), 2, writes, 0, nullptr);
}

bool VideoEffects::CreateStagingImage(uint32_t width, uint32_t height) {
    if (_stagingImage != VK_NULL_HANDLE && _stagingWidth == width && _stagingHeight == height) {
        return true;
    }
    
    DestroyStagingImage();
    
    VkDevice device = _context->GetDevice();
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &_stagingImage) != VK_SUCCESS) {
        return false;
    }
    
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, _stagingImage, &memReqs);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = _context->FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &_stagingMemory) != VK_SUCCESS) {
        vkDestroyImage(device, _stagingImage, nullptr);
        _stagingImage = VK_NULL_HANDLE;
        return false;
    }
    
    vkBindImageMemory(device, _stagingImage, _stagingMemory, 0);
    
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = _stagingImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &_stagingImageView) != VK_SUCCESS) {
        vkFreeMemory(device, _stagingMemory, nullptr);
        vkDestroyImage(device, _stagingImage, nullptr);
        _stagingImage = VK_NULL_HANDLE;
        _stagingMemory = VK_NULL_HANDLE;
        return false;
    }
    
    _stagingWidth = width;
    _stagingHeight = height;
    _lastSrcView = VK_NULL_HANDLE;
    _lastDstView = VK_NULL_HANDLE;
    
    return true;
}

void VideoEffects::DestroyStagingImage() {
    VkDevice device = _context->GetDevice();
    
    if (_stagingImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, _stagingImageView, nullptr);
        _stagingImageView = VK_NULL_HANDLE;
    }
    
    if (_stagingImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, _stagingImage, nullptr);
        _stagingImage = VK_NULL_HANDLE;
    }
    
    if (_stagingMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, _stagingMemory, nullptr);
        _stagingMemory = VK_NULL_HANDLE;
    }
    
    _stagingWidth = 0;
    _stagingHeight = 0;
}

bool VideoEffects::HasActiveEffects() const {
    return !_colorAdjust.IsDefault() || !_filter.IsDefault() || !_postProcess.IsDefault();
}

void VideoEffects::ResetAll() {
    _colorAdjust.Reset();
    _filter.Reset();
    _postProcess.Reset();
}

void VideoEffects::ProcessFrame(tvk::Texture* texture) {
    if (!_initialized || !texture || !HasActiveEffects()) return;
    
    _frameCounter++;
    
    uint32_t width = texture->GetWidth();
    uint32_t height = texture->GetHeight();
    
    if (!CreateStagingImage(width, height)) {
        TVK_LOG_ERROR("Failed to create staging image for video effects");
        return;
    }
    
    UpdateDescriptorSet(_stagingImageView, texture->GetImageView());
    
    VkCommandBuffer cmd = _context->BeginSingleTimeCommands();
    
    VkImageMemoryBarrier barriers[2]{};
    
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = texture->GetImage();
    barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[0].subresourceRange.baseMipLevel = 0;
    barriers[0].subresourceRange.levelCount = 1;
    barriers[0].subresourceRange.baseArrayLayer = 0;
    barriers[0].subresourceRange.layerCount = 1;
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    
    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].image = _stagingImage;
    barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[1].subresourceRange.baseMipLevel = 0;
    barriers[1].subresourceRange.levelCount = 1;
    barriers[1].subresourceRange.baseArrayLayer = 0;
    barriers[1].subresourceRange.layerCount = 1;
    barriers[1].srcAccessMask = 0;
    barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 2, barriers
    );
    
    VkImageCopy copyRegion{};
    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.srcSubresource.mipLevel = 0;
    copyRegion.srcSubresource.baseArrayLayer = 0;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.srcOffset = {0, 0, 0};
    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.dstSubresource.mipLevel = 0;
    copyRegion.dstSubresource.baseArrayLayer = 0;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.dstOffset = {0, 0, 0};
    copyRegion.extent = {width, height, 1};
    
    vkCmdCopyImage(
        cmd,
        texture->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        _stagingImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &copyRegion
    );
    
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 2, barriers
    );
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _computePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1, &_descriptorSet, 0, nullptr);
    
    EffectsPushConstants pc{};
    pc.brightness = _colorAdjust.brightness;
    pc.contrast = _colorAdjust.contrast;
    pc.gamma = _colorAdjust.gamma;
    pc.exposure = _colorAdjust.exposure;
    pc.hue = _colorAdjust.hue;
    pc.saturation = _colorAdjust.saturation;
    pc.temperature = _colorAdjust.temperature;
    pc.tint = _colorAdjust.tint;
    pc.shadows = _colorAdjust.shadows;
    pc.highlights = _colorAdjust.highlights;
    pc.filterType = static_cast<int>(_filter.type);
    pc.filterStrength = _filter.strength;
    pc.filterThreshold = _filter.threshold;
    pc.filterLevels = _filter.levels;
    pc.vignette = _postProcess.vignette;
    pc.vignetteSize = _postProcess.vignetteSize;
    pc.filmGrain = _postProcess.filmGrain;
    pc.chromaticAberration = _postProcess.chromaticAberration;
    pc.scanlines = _postProcess.scanlines;
    pc.vintageStrength = _postProcess.vintageStrength;
    pc.vintageEnabled = _postProcess.vintageEnabled ? 1 : 0;
    pc.width = static_cast<int>(width);
    pc.height = static_cast<int>(height);
    pc.frameCounter = static_cast<int>(_frameCounter);
    pc.bloom = _postProcess.bloom;
    pc.bloomThreshold = _postProcess.bloomThreshold;
    pc.bloomRadius = _postProcess.bloomRadius;
    pc.pad0 = 0;
    
    vkCmdPushConstants(cmd, _pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(EffectsPushConstants), &pc);
    
    uint32_t groupCountX = (width + 15) / 16;
    uint32_t groupCountY = (height + 15) / 16;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);
    
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].image = texture->GetImage();
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, barriers
    );
    
    _context->EndSingleTimeCommands(cmd);
}

}
