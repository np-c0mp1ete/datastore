#include "datastore/node.hpp"
#include "datastore/volume.hpp"

void delete_while_iterate()
{
    using namespace std::chrono_literals;

    while (true)
    {
        datastore::volume vol(datastore::volume::priority_class::medium);
        vol.root()->create_subnode("1");
        vol.root()->create_subnode("2");

        std::atomic_bool mutator_turn = false;

        std::thread iterator([&] {
            size_t num_subnodes = 0;
            vol.root()->for_each_subnode([&](const std::pair<std::string, std::shared_ptr<datastore::node>>&) {
                // Iterate over a subnode
                num_subnodes++;

                // Step 2: Notify the mutator that it can delete one subnode
                mutator_turn.store(true, std::memory_order_release);

                // Step 3: Wait until the mutator is done
                while (mutator_turn.load(std::memory_order_acquire))
                {
                    std::this_thread::sleep_for(1ms);
                }
            });

            // CHECK(num_subnodes == 1);
        });

        std::thread mutator([&] {
            // Step 1: Wait until the iterator is done with the first subnode
            while (!mutator_turn.load(std::memory_order_acquire))
            {
                std::this_thread::sleep_for(1ms);
            }

            // Delete one subnode
            const bool success = vol.root()->delete_subnode_tree("2");
            // CHECK(success);

            // Step 4: Notify the itarator that the subnode is deleted
            mutator_turn.store(false, std::memory_order_release);
        });

        iterator.join();
        mutator.join();

        // std::cout << "done\n";
    }
}

int main(int argc, char* argv[])
{
    using namespace std::chrono_literals;

    auto test_duration = 500s;
    if (argc == 2)
    {
        test_duration = std::chrono::seconds(std::stoi(argv[1]));
    }

    datastore::volume vol(datastore::volume::priority_class::medium);
    vol.root()->create_subnode("1.2.3");

    std::atomic_bool exit = false;

    auto create_subnode = [&] {
        return vol.root()->create_subnode("1.2.3");
    };

    auto open_subnode = [&] {
        return vol.root()->open_subnode("1.2.3");
    };

    auto delete_subnode_tree = [&] {
        return vol.root()->open_subnode("1.2")->delete_subnode_tree("3");
    };

    auto set_value = [&] {
        return vol.root()->set_value("k", "v");
    };

    auto get_value = [&] {
        return vol.root()->get_value<std::string>("k");
    };

    auto get_value_kind = [&] {
        return vol.root()->get_value_kind("k");
    };

    auto delete_value = [&] {
        return vol.root()->delete_value("k");
    };

    const std::vector<std::function<void()>> actions = {
        create_subnode,
        open_subnode,
        delete_subnode_tree,
        set_value,
        get_value,
        get_value_kind,
        delete_value
    };

    std::vector<std::thread> actors(std::thread::hardware_concurrency());

    for (size_t i = 0; i < actors.size(); ++i)
    {
        auto& action = actions[i % actions.size()];
        actors[i] = std::thread([&]{
            while (!exit.load(std::memory_order_relaxed))
            {
                action();
            }
        });
    }

    std::this_thread::sleep_for(test_duration);

    exit = true;

    for (auto& thread : actors)
    {
        thread.join();
    }
}
