#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "core/Scene.h"

namespace radiary {

// Base interface for all post-processing effects.
// Each effect is self-contained: it knows when it should run (IsEnabled),
// what resources it needs (NeedsDepthBuffer), and how to apply itself.
class Effect {
public:
    virtual ~Effect() = default;

    // Returns true if this effect should run given the current scene settings.
    // This is the single source of truth for effect enablement.
    virtual bool IsEnabled(const Scene& scene) const = 0;

    // Returns true if this effect requires a populated depth buffer.
    virtual bool NeedsDepthBuffer() const { return false; }

    // Apply the effect to the pixel buffer.
    // Called ONLY when IsEnabled() returned true.
    // depthBuffer may be empty if NeedsDepthBuffer() returned false.
    virtual void Apply(
        const Scene& scene,
        std::vector<std::uint32_t>& pixels,
        int width,
        int height,
        const std::vector<float>& depthBuffer,
        const std::function<bool()>& shouldAbort) const = 0;
};

// Factory that maps EffectStackStage enum values to concrete Effect instances.
struct EffectFactory {
    static std::unique_ptr<Effect> CreateEffect(EffectStackStage stage);
};

}  // namespace radiary
