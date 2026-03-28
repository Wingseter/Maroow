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
    double rotate_degrees{0.0};
};

class AtlasData {
public:
    /**
     * @brief Constructs immutable atlas data from parsed atlas info and regions.
     * @param info Atlas metadata such as image path and sampler hints.
     * @param regions Named atlas regions available for lookup.
     */
    AtlasData(AtlasInfo info, std::vector<AtlasRegion> regions);

    /// @brief Returns atlas-wide metadata.
    /// @return Immutable atlas info.
    const AtlasInfo& info() const;
    /// @brief Returns every region stored in the atlas.
    /// @return Immutable atlas region list.
    const std::vector<AtlasRegion>& regions() const;

    /**
     * @brief Finds a region by exact region name.
     * @param region_name Region identifier stored in the atlas.
     * @return The matching region, or `nullptr` when no region exists.
     */
    const AtlasRegion* find_region(std::string_view region_name) const;
    /**
     * @brief Finds the atlas region used by an attachment name.
     * @param attachment_name Attachment or region identifier to resolve.
     * @return The matching region, or `nullptr` when no region exists.
     */
    const AtlasRegion* find_region_for_attachment(std::string_view attachment_name) const;

private:
    AtlasInfo info_;
    std::vector<AtlasRegion> regions_;
    std::map<std::string, std::size_t, std::less<>> region_indices_;
};

struct AtlasDataResult {
    std::shared_ptr<const AtlasData> atlas_data;
    std::optional<json::LoadError> error;

    /// @brief Reports whether atlas loading succeeded.
    /// @return `true` when atlas data is available; otherwise `false`.
    explicit operator bool() const {
        return atlas_data != nullptr;
    }
};

class AtlasLoader {
public:
    /**
     * @brief Loads atlas data from an already parsed JSON document.
     * @param document Parsed `.matl` document.
     * @return Immutable atlas data or a load error.
     */
    static AtlasDataResult load(const json::Document& document);
    /**
     * @brief Loads atlas data from a `.matl` file on disk.
     * @param path Path to the atlas metadata file.
     * @return Immutable atlas data or a load error.
     */
    static AtlasDataResult load(const std::filesystem::path& path);
};

} // namespace marrow::runtime
