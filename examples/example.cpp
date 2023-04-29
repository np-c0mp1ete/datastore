#include "datastore/node.hpp"
#include "datastore/node_view.hpp"
#include "datastore/vault.hpp"
#include "datastore/volume.hpp"

#include <iostream>

using namespace datastore;

int main()
{
    volume vol1(volume::priority_class::medium);
    vol1.root()->create_subnode("1.3");
    vol1.root()->create_subnode("8.4");
    vol1.root()->create_subnode("8.5");

    vol1.root()->open_subnode("8")->set_value("k", "v1");

    std::cout << "vol1: " << *vol1.root() << std::endl;

    vol1.unload("vol1.vol");
    volume test = volume::load("vol1.vol").value();


    volume vol2(volume::priority_class::high);
    vol2.root()->create_subnode("2")->create_subnode("9");
    vol2.root()->create_subnode("7")->set_value("k3", "v");

    vol2.root()->create_subnode("2")->set_value("k", "v2");
    vol2.unload("vol2.vol");

    vault vault1;
    vault1.root()->load_subnode_tree("vol1", vol1.root());
    vault1.root()->open_subnode("vol1")->load_subnode_tree("2", vol1.root()->open_subnode("8"));
    vault1.root()->open_subnode("vol1.2.4")->set_value("kk", "vv");
    vault1.root()->open_subnode("vol1.2")->delete_subview_tree("4");

    std::cout << "vault1: " << *vault1.root() << std::endl;

    vault1.root()->open_subnode("vol1.8.5")->load_subnode_tree("vol2", vol2.root());
    vault1.root()->open_subnode("vol1.8.5.vol2")->load_subnode_tree("2", vol2.root()->open_subnode("7"));

    std::cout << "vault1: " << *vault1.root() << std::endl;
}
