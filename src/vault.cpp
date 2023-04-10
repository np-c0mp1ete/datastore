#include "datastore/vault.hpp"

#include "datastore/node_view.hpp"
#include "datastore/volume.hpp"

namespace datastore
{
node_view* vault::load_subnode(const std::string& subnode_name, std::string_view volume_filepath)
{
    std::optional<volume> vol = volume::load(volume_filepath);
    if (!vol)
        return nullptr;

    auto [it, inserted] = volumes_.emplace(subnode_name, std::move(vol.value()));
    if (!inserted)
        return nullptr;

    node_view* subnode = root_.assign_subnode(subnode_name, it->second.root());
    if (!subnode)
    {
        volumes_.erase(subnode_name);
        return nullptr;
    }

    return subnode;
}

void vault::unload_subview(const std::string& subnode_name)
{
    root_.subviews_.erase(subnode_name);
}
}
