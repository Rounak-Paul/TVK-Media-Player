#pragma once

#include <tinyvk/tinyvk.h>
#include <vulkan/vulkan.h>
#include <cstdint>

namespace tvk_media {

struct ColorAdjustments {
    float brightness = 0.0f;
    float contrast = 1.0f;
    float gamma = 1.0f;
    float hue = 0.0f;
    float saturation = 1.0f;
    float temperature = 0.0f;
    float tint = 0.0f;
    float exposure = 0.0f;
    float shadows = 0.0f;
    float highlights = 0.0f;
    
    bool IsDefault() const {
        return brightness == 0.0f && contrast == 1.0f && gamma == 1.0f &&
               hue == 0.0f && saturation == 1.0f && temperature == 0.0f &&
               tint == 0.0f && exposure == 0.0f && shadows == 0.0f && highlights == 0.0f;
    }
    
    void Reset() {
        brightness = 0.0f;
        contrast = 1.0f;
        gamma = 1.0f;
        hue = 0.0f;
        saturation = 1.0f;
        temperature = 0.0f;
        tint = 0.0f;
        exposure = 0.0f;
        shadows = 0.0f;
        highlights = 0.0f;
    }
};

enum class FilterType {
    None = 0,
    Grayscale,
    Sepia,
    Invert,
    Posterize,
    Solarize,
    Threshold,
    Sharpen,
    EdgeDetect
};

struct FilterSettings {
    FilterType type = FilterType::None;
    float strength = 1.0f;
    float threshold = 0.5f;
    int levels = 4;
    
    bool IsDefault() const {
        return type == FilterType::None;
    }
    
    void Reset() {
        type = FilterType::None;
        strength = 1.0f;
        threshold = 0.5f;
        levels = 4;
    }
};

struct PostProcessSettings {
    float vignette = 0.0f;
    float vignetteSize = 0.5f;
    float filmGrain = 0.0f;
    float chromaticAberration = 0.0f;
    float scanlines = 0.0f;
    bool vintageEnabled = false;
    float vintageStrength = 0.5f;
    float bloom = 0.0f;
    float bloomThreshold = 0.8f;
    float bloomRadius = 4.0f;
    
    bool IsDefault() const {
        return vignette == 0.0f && filmGrain == 0.0f &&
               chromaticAberration == 0.0f && scanlines == 0.0f && !vintageEnabled && bloom == 0.0f;
    }
    
    void Reset() {
        vignette = 0.0f;
        vignetteSize = 0.5f;
        filmGrain = 0.0f;
        chromaticAberration = 0.0f;
        scanlines = 0.0f;
        vintageEnabled = false;
        vintageStrength = 0.5f;
        bloom = 0.0f;
        bloomThreshold = 0.8f;
        bloomRadius = 4.0f;
    }
};

struct EffectsPushConstants {
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
};

class VideoEffects {
public:
    VideoEffects();
    ~VideoEffects();
    
    bool Init(tvk::Renderer* renderer);
    void Cleanup();
    
    void ProcessFrame(tvk::Texture* texture);
    
    ColorAdjustments& GetColorAdjustments() { return _colorAdjust; }
    FilterSettings& GetFilterSettings() { return _filter; }
    PostProcessSettings& GetPostProcess() { return _postProcess; }
    
    const ColorAdjustments& GetColorAdjustments() const { return _colorAdjust; }
    const FilterSettings& GetFilterSettings() const { return _filter; }
    const PostProcessSettings& GetPostProcess() const { return _postProcess; }
    
    bool HasActiveEffects() const;
    void ResetAll();
    
private:
    bool CreateComputePipeline();
    bool CreateDescriptorSetLayout();
    bool AllocateDescriptorSet();
    void UpdateDescriptorSet(VkImageView srcView, VkImageView dstView);
    bool CreateStagingImage(uint32_t width, uint32_t height);
    void DestroyStagingImage();
    
    tvk::Renderer* _renderer;
    tvk::VulkanContext* _context;
    
    VkPipeline _computePipeline;
    VkPipelineLayout _pipelineLayout;
    VkDescriptorSetLayout _descriptorSetLayout;
    VkDescriptorSet _descriptorSet;
    VkShaderModule _shaderModule;
    
    VkImage _stagingImage;
    VkDeviceMemory _stagingMemory;
    VkImageView _stagingImageView;
    uint32_t _stagingWidth;
    uint32_t _stagingHeight;
    
    VkImageView _lastSrcView;
    VkImageView _lastDstView;
    
    ColorAdjustments _colorAdjust;
    FilterSettings _filter;
    PostProcessSettings _postProcess;
    uint32_t _frameCounter;
    
    bool _initialized;
};

}
