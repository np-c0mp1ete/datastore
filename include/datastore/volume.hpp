#pragma once

#include <string_view>
#include <filesystem>
#include <optional>

#include "datastore/node.hpp"


namespace datastore
{
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
  public:
    using priority_t = uint8_t;

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

    friend class serializer;

    static const inline std::vector<uint8_t> signature = {'=', 'V', 'O', 'L'};

private:
    uint8_t format_version_[2];
    priority_t priority_;
    node root_ = node("root", this, nullptr);
};
} // namespace datastore
