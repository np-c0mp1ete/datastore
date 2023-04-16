#pragma once

#include <string_view>
#include <filesystem>
#include <optional>

#include "datastore/node.hpp"


namespace datastore
{
//TODO: move to detail namespace
class serializer
{
public:
    std::optional<node> deserialize_node(volume& vol, node* parent, std::vector<uint8_t>& buffer, size_t& pos);
    bool serialize_node(node& n, std::vector<uint8_t>& buffer);

    std::optional<volume> deserialize_volume(std::vector<uint8_t>& buffer);
    bool serialize_volume(volume& vol, std::vector<uint8_t>& buffer);
};

class volume
{
    friend class serializer;

  public:
    // Higher number -> higher priority
    using priority_t = uint8_t;

    // Predefined values can be used for simplicity
    enum priority_class : priority_t
    {
        lowest = 0,
        low = 25,
        below_medium = 50,
        medium = 100,
        above_medium = 150,
        high = 200,
        highest = 255
    };

    static const inline std::vector<uint8_t> signature = {'=', 'V', 'O', 'L'};

    constexpr static size_t max_tree_depth = 512;

    volume(priority_t priority);

    [[nodiscard]] volume(const volume& other) noexcept;

    [[nodiscard]] volume(volume&& other) noexcept;

    volume& operator=(const volume& rhs) noexcept;

    volume& operator=(volume&& rhs) noexcept;

    bool unload(const std::filesystem::path& filepath);
    static std::optional<volume> load(const std::filesystem::path& filepath);

    node* root()
    {
        return &root_;
    }

    [[nodiscard]] priority_t priority() const
    {
        return priority_;
    }

private:
    uint8_t format_version_[2];
    priority_t priority_;
    node root_ = node("root", this, nullptr);
};
} // namespace datastore
