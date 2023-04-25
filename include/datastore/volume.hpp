#pragma once

#include <string_view>
#include <filesystem>
#include <optional>

#include "datastore/node.hpp"


namespace datastore
{
namespace detail
{
// TODO: add serializer unit tests
class serializer final
{
  public:
    std::optional<node> deserialize_node(uint8_t volume_priority, const std::string& path, size_t cur_depth,
                                         std::vector<uint8_t>& buffer, size_t& pos);
    bool serialize_node(node& n, std::vector<uint8_t>& buffer);

    std::optional<volume> deserialize_volume(std::vector<uint8_t>& buffer);
    bool serialize_volume(volume& vol, std::vector<uint8_t>& buffer);
};
} // namespace detail

class volume final
{
    friend class detail::serializer;

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

    constexpr static size_t max_tree_depth = 255;

    volume(priority_t priority);

    volume(const volume& other) = delete;
    volume(volume&& other) noexcept = default;

    volume& operator=(const volume& rhs) = delete;
    volume& operator=(volume&& rhs) noexcept = default;

    bool unload(const std::filesystem::path& filepath);
    static std::optional<volume> load(const std::filesystem::path& filepath);

    std::shared_ptr<node> root()
    {
        return root_;
    }

    [[nodiscard]] priority_t priority() const
    {
        return priority_;
    }

private:
    priority_t priority_;
    std::shared_ptr<node> root_;
};
} // namespace datastore
