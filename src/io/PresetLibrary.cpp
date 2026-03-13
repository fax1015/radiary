#include "io/PresetLibrary.h"

#include <algorithm>

namespace radiary {

void PresetLibrary::LoadFromDirectory(const std::filesystem::path& directory) {
    presets_.clear();
    if (std::filesystem::exists(directory)) {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".radiary") {
                continue;
            }
            std::string error;
            if (auto scene = serializer_.Load(entry.path(), error)) {
                presets_.push_back(*scene);
            }
        }
    }

    std::sort(presets_.begin(), presets_.end(), [](const Scene& left, const Scene& right) {
        return left.name < right.name;
    });
}

std::vector<std::string> PresetLibrary::Names() const {
    std::vector<std::string> names;
    names.reserve(presets_.size());
    for (const Scene& scene : presets_) {
        names.push_back(scene.name);
    }
    return names;
}

std::string PresetLibrary::NameAt(const std::size_t index) const {
    if (presets_.empty()) {
        return "Default";
    }
    return presets_[index % presets_.size()].name;
}

Scene PresetLibrary::SceneAt(const std::size_t index) const {
    if (presets_.empty()) {
        return CreateDefaultScene();
    }
    return presets_[index % presets_.size()];
}

std::size_t PresetLibrary::Count() const {
    return presets_.size();
}

}  // namespace radiary
