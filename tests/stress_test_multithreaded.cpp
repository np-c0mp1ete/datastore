#include <thread>
#include <iostream>

#include "datastore/vault.hpp"
#include "datastore/volume.hpp"

#include "load_test_common.hpp"

using namespace datastore;

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
    vault1.root()->load_subnode_tree("vol", load_test::vol1.root());
    vault1.root()->load_subnode_tree("vol", load_test::vol2.root());

    vault vault2;
    vault2.root()->load_subnode_tree("vol", load_test::vol1.root());
    vault2.root()->load_subnode_tree("vol", load_test::vol2.root());

    const std::vector<std::function<void(const std::shared_ptr<node>&, size_t)>> node_actions = {
        load_test::node_create_tree,
        load_test::node_get_tree,
        load_test::node_delete_tree,
        load_test::volume_save
    };

    const std::vector<std::function<void(const std::shared_ptr<node_view>&, size_t)>> node_view_actions = {
        load_test::node_view_create_tree,
        load_test::node_view_get_tree,
        load_test::node_view_delete_tree,
        load_test::node_view_load_subnode,
        load_test::node_view_unload_subnode};

    std::vector<std::thread> node_actors(std::thread::hardware_concurrency() / 2);
    std::vector<std::thread> node_view_actors(std::thread::hardware_concurrency() / 2);

    std::atomic_bool exit = false;

    std::cout << "Starting " << node_actors.size() << " node actors\n";

    for (size_t i = 0; i < node_actors.size(); ++i)
    {
        auto& action = node_actions[i % node_actions.size()];
        node_actors[i] = std::thread([&] {
            while (!exit.load(std::memory_order_relaxed))
            {
                action(load_test::vol1.root(), 1);
                action(load_test::vol2.root(), 1);
            }
        });
    }

    std::cout << "Starting " << node_view_actors.size() << " node_view actors\n";

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

    std::cout << "Waiting for node actors to finish...\n";

    size_t node_actors_left = node_actors.size();
    for (auto& thread : node_actors)
    {
        std::cout << node_actors_left << " node actors left\n";
        thread.join();
        node_actors_left--;
    }

    std::cout << "Waiting for node_view actors to finish...\n";

    size_t node_view_actors_left = node_view_actors.size();
    for (auto& thread : node_view_actors)
    {
        std::cout << node_view_actors_left << " node_view actors left\n";
        thread.join();
        node_view_actors_left--;
    }

    std::cout << "Done\n";
}
