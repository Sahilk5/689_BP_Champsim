#pragma once

#include <cstddef>
#include <cassert>
#include <cstdint>
#include <vector>

#include "ram_node.h"

class Discriminator {
private:
    std::vector<RamNode> ram_nodes;
//    uint32_t num_nodes;
public:
    Discriminator(uint32_t num_ram_nodes, uint8_t counter_bits) {
        ram_nodes.reserve(num_ram_nodes);
        for (uint32_t i = 0; i < num_ram_nodes; i++) {
            ram_nodes.emplace_back(counter_bits);
        }
    }

    void train(std::vector<uint64_t>& addresses) {
        assert(addresses.size() == ram_nodes.size()); // Need to check if all ram nodes are to be accessed 
        for (uint32_t i = 0; i < ram_nodes.size(); i++) {
            ram_nodes[i].train(addresses[i]);
        }
    }

    uint32_t get_response(std::vector<uint64_t> &addresses, uint32_t bleach_threshold) const {
        assert(addresses.size() == ram_nodes.size()); // Need to check if all ram nodes are to be accessed 
        uint32_t sum = 0;
        for(uint32_t i = 0; i < ram_nodes.size(); i++) {
            sum += ram_nodes[i].read_bleached(addresses[i], bleach_threshold);
        }
        return sum;
    }
    
};
