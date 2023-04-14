#include "datastore/node.hpp"
#include "datastore/node_view.hpp"
#include "datastore/vault.hpp"
#include "datastore/volume.hpp"

#include <iostream>

int main()
{
    datastore::volume vol1(2);
    vol1.root()->create_subnode("1.3");
    vol1.root()->create_subnode("8.4");
    vol1.root()->create_subnode("8.5");

    vol1.root()->open_subnode("8")->set_value("k", "v1");

    // std::cout << "vol1: " << *vol1.root() << std::endl;

    bool success = vol1.unload("vol1.vol");
    datastore::volume test = datastore::volume::load("vol1.vol").value();
    // std::cout << "test: " << *test.root() << std::endl;


    datastore::volume vol2(1);
    vol2.root()->create_subnode("2")->create_subnode("9");
    vol2.root()->create_subnode("7")->set_value("k3", "v");

    vol2.root()->create_subnode("2")->set_value("k", "v2");
    // std::cout << "vol2: " << *vol2.root() << std::endl;
    success = vol2.unload("vol2.vol");

    
    datastore::vault vault1;
    vault1.root()->load_subnode("vol1", vol1.root());
    vault1.root()->open_subnode("vol1")->create_symlink_subnode("2", "vol1.8");
    vault1.root()->open_subnode("vol1.2.4")->set_value("kk", "vv");
    vault1.root()->open_subnode("vol1.2")->delete_subnode_tree("4");

    vault1.root()->load_subnode("vol2", vol2.root());
    vault1.root()->open_subnode("vol2")->create_symlink_subnode("2", "vol2.7");

    std::cout << "vault1: " << *vault1.root() << std::endl;
}
