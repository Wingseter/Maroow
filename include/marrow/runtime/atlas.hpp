#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/runtime/json.hpp"

namespace marrow::runtime {

struct AtlasInfo {
    std::string name;
    std::string image;
    double width{0.0};
    double height{0.0};
    std::string filter_min;
    std::string filter_mag;
    std::string wrap_x;
    std::string wrap_y;
    bool premultiplied_alpha{false};
};

struct AtlasRegion {
    std::string name;
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};
    double origin_x{0.0};
    double origin_y{0.0};
};

class AtlasData {
public:
    AtlasData(AtlasInfo info, std::vector<AtlasRegion> regions);

    const AtlasInfo& info() const;
    const std::vector<AtlasRegion>& regions() const;

    const AtlasRegion* find_region(std::string_view region_name) const;
    const AtlasRegion* find_region_for_attachment(std::string_view attachment_name) const;

private:
    AtlasInfo info_;
    std::vector<AtlasRegion> regions_;
    std::map<std::string, std::size_t, std::less<>> region_indices_;
};

struct AtlasDataResult {
    std::shared_ptr<const AtlasData> atlas_data;
    std::optional<json::LoadError> error;

    explicit operator bool() const {
        return atlas_data != nullptr;
    }
};

class AtlasLoader {
public:
    static AtlasDataResult load(const json::Document& document);
    static AtlasDataResult load(const std::filesystem::path& path);
};

} // namespace marrow::runtime
