#pragma once

#include "datastore/node_view.hpp"

namespace datastore
{
class vault
{
  public:
    constexpr static size_t max_tree_depth = 255;

    // TODO: implement copy&move constructors and call set_vault()

    node_view* root()
    {
        return &root_;
    }

  private:
    node_view root_ = node_view("root", this, nullptr);
    std::unordered_map<std::string, volume> volumes_;
};
} // namespace datastore
