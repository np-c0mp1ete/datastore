#pragma once

#include <algorithm>
#include <list>
#include <optional>
#include <string>

namespace datastore
{
// Paths in the form "^[a-zA-Z0-9]+(\.[a-zA-Z0-9]+)*$" are supported, e.g. "abc" or "a.b.c"
// Only alphanumeric path elements are allowed
class path_view
{
  public:
    constexpr static char path_separator = '.';

    template <typename T, typename = std::enable_if_t<std::is_constructible_v<std::string_view, T>>>
    path_view(T&& path) : path_(std::forward<T>(path))
    {
        valid_ = parse(path);
    }

    path_view(nullptr_t) = delete;

    [[nodiscard]] bool valid() const
    {
        return valid_ && !elements_.empty();
    }

    [[nodiscard]] bool composite() const
    {
        return valid_ && elements_.size() > 1;
    }

    [[nodiscard]] std::optional<std::string_view> front() const
    {
        if (!valid_ || elements_.empty())
            return std::nullopt;

        return elements_.front();
    }

    [[nodiscard]] std::optional<std::string_view> back() const
    {
        if (!valid_ || elements_.empty())
            return std::nullopt;

        return elements_.back();
    }

    void pop_front()
    {
        if (!valid_ || elements_.empty())
            return;

        path_.remove_prefix(elements_.front().size());
        if (!path_.empty() && path_.front() == path_separator)
            path_.remove_prefix(1);

        elements_.pop_front();
    }

    void pop_back()
    {
        if (!valid_ || elements_.empty())
            return;

        path_.remove_suffix(elements_.back().size());
        if (!path_.empty() && path_.back() == path_separator)
            path_.remove_suffix(1);

        elements_.pop_back();
    }

    // TODO: add noexcept-s
    [[nodiscard]] size_t size() const
    {
        return valid_ ? elements_.size() : 0;
    }

    [[nodiscard]] std::string str() const
    {
        return valid_ ? std::string(path_) : "";
    }

    operator std::string_view() const
    {
        return valid_ ? path_ : "";
    }

  private:
    bool parse(std::string_view path)
    {
        // Only alphanumeric characters and path separators are allowed
        if (!std::all_of(path.begin(), path.end(),
                         [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == path_separator; }))
            return false;

        // Split the string by the path separator
        size_t start = 0;
        while (true)
        {
            const size_t end = path.find(path_separator, start);

            if (end == std::string_view::npos)
                break;

            elements_.emplace_back(&path[start], end - start);

            start = end + 1;
        }

        if (start < path.size())
            elements_.emplace_back(&path[start], path.size() - start);
        else
            elements_.emplace_back("");

        // Check that all path elements are not empty
        return std::all_of(elements_.begin(), elements_.end(), [](auto element) { return !element.empty(); });
    }

    std::string_view path_;
    std::list<std::string_view> elements_;
    bool valid_;
};
} // namespace datastore
