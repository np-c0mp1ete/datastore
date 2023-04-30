#include "datastore/vault.hpp"

#include <array>
#include <thread>

#include "datastore/volume.hpp"

#include <iostream>

using namespace datastore;

namespace
{
template <size_t... Ints>
auto to_string(std::integer_sequence<size_t, Ints...>)
{
    return std::array<std::string, sizeof...(Ints)>{{std::to_string(Ints)...}};
}

// Cache stringified indices
constexpr size_t max_node_idx = std::max(node::max_num_subnodes, node::max_num_values);
constexpr size_t max_node_view_idx = std::max(node_view::max_num_subviews, node_view::max_num_values);
constexpr size_t max_idx = std::max(max_node_idx, max_node_view_idx);
const auto idx_str = to_string(std::make_index_sequence<max_idx>{});

const std::string max_str = std::string(max_str_value_size_bytes, 'a');

volume vol1(volume::priority_class::medium);
volume vol2(volume::priority_class::medium);

void node_create_tree(const std::shared_ptr<node>& parent, size_t cur_depth = 1)
{
    if (cur_depth >= volume::max_tree_depth)
        return;

    for (size_t subnode_idx = 0; subnode_idx < node::max_num_subnodes; subnode_idx++)
    {
        std::shared_ptr<node> subnode = parent->create_subnode(idx_str[subnode_idx]);
        node_create_tree(subnode, cur_depth + 1);
    }

    for (size_t value_idx = 0; value_idx < node::max_num_values; value_idx++)
    {
        parent->set_value(idx_str[value_idx], max_str);
    }
}

void node_view_create_tree(const std::shared_ptr<node_view>& parent, size_t cur_depth = 1)
{
    if (cur_depth >= vault::max_tree_depth)
        return;

    for (size_t subnode_idx = 0; subnode_idx < node_view::max_num_subviews; subnode_idx++)
    {
        std::shared_ptr<node_view> subnode = parent->create_subnode(idx_str[subnode_idx]);

        // Can happen if e.g. the parent was deleted from the vault
        if (!subnode)
            continue;

        node_view_create_tree(subnode, cur_depth + 1);
    }

    for (size_t value_idx = 0; value_idx < node_view::max_num_values; value_idx++)
    {
        parent->set_value(idx_str[value_idx], max_str);
    }
}

void node_get_tree(const std::shared_ptr<node>& parent, size_t cur_depth = 1)
{
    if (cur_depth >= volume::max_tree_depth)
        return;

    for (size_t subnode_idx = 0; subnode_idx < node::max_num_subnodes; subnode_idx++)
    {
        const std::shared_ptr<node> subnode = parent->open_subnode(idx_str[subnode_idx]);
        if (!subnode)
            continue;
        node_get_tree(subnode, cur_depth + 1);
    }

    for (size_t value_idx = 0; value_idx < node::max_num_values; value_idx++)
    {
        DATASTORE_UNUSED(parent->get_value_kind(idx_str[value_idx]));
        DATASTORE_UNUSED(parent->get_value<std::string>(idx_str[value_idx]));
    }

    parent->for_each_subnode([&](const std::shared_ptr<node>& subnode) {
        // Make sure it's safe to work with values while holding a subnodes lock
        parent->for_each_value([&](const attr& a) {
            DATASTORE_UNUSED(*a.get_value_kind());
            DATASTORE_UNUSED(*a.get_value<std::string>());
        });

        node_get_tree(subnode, cur_depth + 1);
    });

    parent->for_each_value([&](const attr& a) {
        DATASTORE_UNUSED(*a.get_value_kind());
        DATASTORE_UNUSED(*a.get_value<std::string>());
    });
}

void node_view_get_tree(const std::shared_ptr<node_view>& parent, size_t cur_depth = 1)
{
    if (cur_depth >= vault::max_tree_depth)
        return;

    for (size_t subnode_idx = 0; subnode_idx < node_view::max_num_subviews; subnode_idx++)
    {
        const std::shared_ptr<node_view> subnode = parent->open_subnode(idx_str[subnode_idx]);
        if (!subnode)
            continue;
        node_view_get_tree(subnode, cur_depth + 1);
    }

    for (size_t value_idx = 0; value_idx < node_view::max_num_values; value_idx++)
    {
        DATASTORE_UNUSED(parent->get_value_kind(idx_str[value_idx]));
        DATASTORE_UNUSED(parent->get_value<std::string>(idx_str[value_idx]));
    }

    parent->for_each_subnode([&](const std::shared_ptr<node_view>& subnode) {
        // Due to a potential deadlock caused by a cycle in a lock order graph,
        // it's NOT safe to work with values while holding a subviews lock
        // Run with thread sanitizer for details.

        // parent->for_each_value([&](const attr& a) {
        //     DATASTORE_UNUSED(*a.get_value_kind());
        //     DATASTORE_UNUSED(*a.get_value<std::string>());
        // });

        node_view_get_tree(subnode, cur_depth + 1);
    });

    parent->for_each_value([&](const attr& a) {
        DATASTORE_UNUSED(*a.get_value_kind());
        DATASTORE_UNUSED(*a.get_value<std::string>());
    });
}

void node_delete_tree(const std::shared_ptr<node>& parent, size_t cur_depth = 1)
{
    if (cur_depth >= volume::max_tree_depth)
        return;

    for (size_t subnode_idx = 0; subnode_idx < node::max_num_subnodes; subnode_idx++)
    {
        const std::shared_ptr<node> subnode = parent->open_subnode(idx_str[subnode_idx]);
        if (!subnode)
            continue;
        node_delete_tree(subnode, cur_depth + 1);

        parent->delete_subnode_tree(idx_str[subnode_idx]);
    }

    // Test deleting attributes one by one
    for (size_t value_idx = 0; value_idx < node::max_num_values; value_idx++)
    {
        parent->delete_value(idx_str[value_idx]);
    }

    parent->for_each_subnode([&](const std::shared_ptr<node>& subnode) {
        node_delete_tree(subnode, cur_depth + 1);

        subnode->delete_values();
    });

    parent->delete_subnode_tree();
    parent->delete_values();
}

void node_view_delete_tree(const std::shared_ptr<node_view>& parent, size_t cur_depth = 1)
{
    if (cur_depth >= vault::max_tree_depth)
        return;

    for (size_t subnode_idx = 0; subnode_idx < node_view::max_num_subviews; subnode_idx++)
    {
        const std::shared_ptr<node_view> subnode = parent->open_subnode(idx_str[subnode_idx]);
        if (!subnode)
            continue;
        node_view_delete_tree(subnode, cur_depth + 1);

        parent->delete_subview_tree(idx_str[subnode_idx]);
    }

    // Test deleting attributes one by one
    for (size_t value_idx = 0; value_idx < node_view::max_num_values; value_idx++)
    {
        parent->delete_value(idx_str[value_idx]);
    }

    parent->for_each_subnode([&](const std::shared_ptr<node_view>& subnode) {
        node_view_delete_tree(subnode, cur_depth + 1);

        subnode->delete_values();
    });

    parent->delete_subview_tree();
    parent->delete_values();
}

void node_view_load_subnode(const std::shared_ptr<node_view>& parent, size_t cur_depth = 1)
{
    if (cur_depth >= vault::max_tree_depth)
        return;

    for (size_t subnode_idx = 0; subnode_idx < node_view::max_num_subviews; subnode_idx++)
    {
        const std::shared_ptr<node_view> subnode = parent->load_subnode_tree(idx_str[subnode_idx], vol1.root());
        parent->load_subnode_tree(idx_str[subnode_idx], vol2.root());
        if (!subnode)
            continue;
        node_view_load_subnode(subnode, cur_depth + 1);
    }
}

void node_view_unload_subnode(const std::shared_ptr<node_view>& parent, size_t cur_depth = 1)
{
    if (cur_depth >= vault::max_tree_depth)
        return;

    for (size_t subnode_idx = 0; subnode_idx < node_view::max_num_subviews; subnode_idx++)
    {
        const std::shared_ptr<node_view> subnode = parent->open_subnode(idx_str[subnode_idx]);
        if (!subnode)
            continue;
        node_view_unload_subnode(subnode, cur_depth + 1);

        parent->unload_subnode_tree(idx_str[subnode_idx]);
    }

    parent->unload_subnode_tree();
}
} // namespace

int main(int argc, char* argv[])
{
    using namespace std::chrono_literals;

    auto test_duration = 50s;
    if (argc == 2)
    {
        test_duration = std::chrono::seconds(std::stoi(argv[1]));
    }

    std::cout << "Test duration is " << test_duration.count() << " seconds\n";

    vault vault1;
    vault1.root()->load_subnode_tree("vol", vol1.root());
    vault1.root()->load_subnode_tree("vol", vol2.root());

    vault vault2;
    vault2.root()->load_subnode_tree("vol", vol1.root());
    vault2.root()->load_subnode_tree("vol", vol2.root());

    auto volume_unload = [&](const std::shared_ptr<node>&, size_t) {
        vol1.save("vol1.vol");
        vol2.save("vol2.vol");
    };

    const std::vector<std::function<void(const std::shared_ptr<node>&, size_t)>> node_actions = {
        node_create_tree, node_get_tree, node_delete_tree, volume_unload
    };

    const std::vector<std::function<void(const std::shared_ptr<node_view>&, size_t)>> node_view_actions = {
        node_view_create_tree, node_view_get_tree, node_view_delete_tree, node_view_load_subnode, node_view_unload_subnode
    };

    std::vector<std::thread> node_actors(std::thread::hardware_concurrency() / 2);
    std::vector<std::thread> node_view_actors(std::thread::hardware_concurrency() / 2);

    std::atomic_bool exit = false;

    std::cout << "Starting actors...\n";

    for (size_t i = 0; i < node_actors.size(); ++i)
    {
        auto& action = node_actions[i % node_actions.size()];
        node_actors[i] = std::thread([&] {
            while (!exit.load(std::memory_order_relaxed))
            {
                action(vol1.root(), 1);
                action(vol2.root(), 1);
            }
        });
    }

    for (size_t i = 0; i < node_view_actors.size(); ++i)
    {
        auto& action = node_view_actions[i % node_view_actions.size()];
        node_view_actors[i] = std::thread([&] {
            while (!exit.load(std::memory_order_relaxed))
            {
                action(vault1.root()->open_subnode("vol"), 2);
                action(vault2.root()->open_subnode("vol"), 2);
            }
        });
    }

    while (test_duration.count() > 0)
    {
        std::cout << test_duration.count() << " seconds left\n";
        --test_duration;
        std::this_thread::sleep_for(1s);
    }

    exit = true;

    std::cout << "Waiting for actors to finish...\n";

    for (auto& thread : node_actors)
    {
        thread.join();
    }

    for (auto& thread : node_view_actors)
    {
        thread.join();
    }

    std::cout << "Done\n";
}
