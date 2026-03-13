#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "core/Scene.h"

namespace radiary {

class SceneSerializer {
public:
    bool Save(const Scene& scene, const std::filesystem::path& path, std::string& error) const;
    std::optional<Scene> Load(const std::filesystem::path& path, std::string& error) const;
};

}  // namespace radiary
