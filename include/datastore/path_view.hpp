#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <variant>

namespace datastore
{
class node_view;
class volume;

constexpr static char path_separator = '.';

class path_element_view
{
  public:
    path_element_view(const char* value)
    {
        parse(value);
    }

    path_element_view(std::string_view value)
    {
        parse(value);
    }

    [[nodiscard]] bool valid() const
    {
        return !value_.empty();
    }

    [[nodiscard]] size_t size() const
    {
        return value_.size();
    }

    [[nodiscard]] std::string str() const
    {
        return std::string(value_);
    }

    operator std::string_view() const
    {
        return value_;
    }

  private:
    void parse(std::string_view value)
    {
        if (value.find(path_separator) != std::string_view::npos)
            return;

        value_ = value;
    }

    std::string_view value_;
};

class path_view
{
public:
    path_view(const char* path) : path_(path)
    {
        parse(path);
    }

    path_view(std::string_view path) : path_(path)
    {
        parse(path);
    }

    path_view(const std::string& path) : path_(path)
    {
        parse(path);
    }

    path_view(path_element_view path) : path_(path)
    {
        parse(path);
    }

    [[nodiscard]] bool valid() const
    {
        return !elements_.empty();
    }

    [[nodiscard]] bool composite() const
    {
        return elements_.size() > 1;
    }

    [[nodiscard]] std::optional<path_element_view> front() const
    {
        if (elements_.empty())
            return std::nullopt;

        return elements_.front();
    }

    [[nodiscard]] std::optional<path_element_view> back() const
    {
        if (elements_.empty())
            return std::nullopt;

        return elements_.back();
    }

    void pop_front()
    {
        if (elements_.empty())
            return;

        path_.remove_prefix(elements_.front().size());
        if (!path_.empty() && path_[0] == path_separator)
            path_.remove_prefix(1);
        elements_.pop_front();
    }

    [[nodiscard]] size_t size() const
    {
        return elements_.size();
    }

    [[nodiscard]] std::string str() const
    {
        return std::string(path_);
    }

private:
    void parse(std::string_view path)
    {
        size_t start = 0;

        while (true)
        {
            const size_t end = path.find(path_separator, start);

            if (end == std::string_view::npos)
                break;

            elements_.emplace_back(std::string_view(&path[start], end - start));

            start = end + 1;
        }

        if (start < path.size())
            elements_.emplace_back(std::string_view(&path[start], path.size() - start));
        else
            elements_.emplace_back("");

    }

    std::string_view path_;
    std::list<path_element_view> elements_;
};
} // namespace datastore
