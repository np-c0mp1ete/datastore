#pragma once

#include "datastore/node_view.hpp"

namespace datastore
{
class vault final
{
  public:
    constexpr static size_t max_tree_depth = 7;

    std::shared_ptr<node_view> root()
    {
        return root_;
    }

  private:
    std::shared_ptr<node_view> root_ = std::shared_ptr<node_view>(new node_view("root"));
};
} // namespace datastore
