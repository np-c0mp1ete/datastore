#pragma once

#include "datastore/node_view.hpp"

namespace datastore
{
class vault
{
  public:
    constexpr static size_t max_tree_depth = 255;

    // TODO: implement copy&move constructors and call set_vault()

    // Creates a subnode and loads the data from the specified node into that subnode
    // node_view* load_subnode(const std::string& subnode_name, std::string_view volume_filepath);

    // Unloads the specified subnode and its subnodes from the vault
    // The key referred to by the subnode_name parameter must have been created by using the load_subnode function.
    // void unload_subview(const std::string& subnode_name);

    node_view* root()
    {
        return &root_;
    }

  private:
    node_view root_ = node_view("root", this, nullptr);
    std::unordered_map<std::string, volume> volumes_;
};
} // namespace datastore
