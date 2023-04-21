#include "datastore/node.hpp"
#include "datastore/volume.hpp"

#include <iostream>

int main()
{
    using namespace std::chrono_literals;

    datastore::volume vol1(datastore::volume::priority_class::medium);

    std::atomic_bool exit = false;

    auto set_value = [&] {
        while (!exit)
        {
            vol1.root()->set_value("k", "v");
            std::cout << "set\n";
        }
    };

    auto get_value = [&] {
        while (!exit)
        {
            auto value = vol1.root()->get_value<std::string>("k");
            std::cout << (value ? value.value() + "\n" : "null\n");
        }
    };

    auto get_value_kind = [&] {
        while (!exit)
        {
            auto kind = vol1.root()->get_value_kind("k");
            std::cout << (kind ? "str\n" : "null\n");
        }
    };

    auto delete_value = [&] {
        while (!exit)
        {
            vol1.root()->delete_value("k");
            std::cout << "delete\n";
        }
    };

    std::vector<std::function<void()>> actions = {set_value, get_value, get_value_kind, delete_value};

    std::vector<std::thread> executors(std::thread::hardware_concurrency());

    for (size_t i = 0; i < executors.size(); ++i)
    {
        executors[i] = std::thread(actions[i % actions.size()]);
    }

    std::this_thread::sleep_for(5s);

    exit = true;

    for (auto& thread : executors)
    {
        thread.join();
    }
}
