#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/Scene.h"
#include "io/SceneSerializer.h"

namespace radiary {

class PresetLibrary {
public:
    void LoadFromDirectory(const std::filesystem::path& directory);
    std::vector<std::string> Names() const;
    std::string NameAt(std::size_t index) const;
    Scene SceneAt(std::size_t index) const;
    std::size_t Count() const;

private:
    std::vector<Scene> presets_;
    SceneSerializer serializer_;
};

}  // namespace radiary
