#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ram_node.h"

class Discriminator {
private:
    std::vector<RamNode> ram_nodes;

public:
    Discriminator(uint32_t num_ram_nodes, uint8_t tuple_bits, uint8_t counter_bits) {
        ram_nodes.reserve(num_ram_nodes);
        for (uint32_t i = 0; i < num_ram_nodes; ++i) {
            ram_nodes.emplace_back(tuple_bits, counter_bits);
        }
    }

    void train(const std::vector<uint64_t>& addresses) {
        // One address per RAM node in the discriminator.
        assert(addresses.size() == ram_nodes.size());
        for (std::size_t i = 0; i < ram_nodes.size(); ++i) {
            ram_nodes[i].train(addresses[i]);
        }
    }

    uint32_t get_response(const std::vector<uint64_t>& addresses, uint32_t bleach_threshold) const {
        assert(addresses.size() == ram_nodes.size());
        uint32_t sum = 0;
        for (std::size_t i = 0; i < ram_nodes.size(); ++i) {
            // Response is the number of RAM nodes that survive bleaching.
            sum += ram_nodes[i].read_bleached(addresses[i], bleach_threshold);
        }
        return sum;
    }

    void decay() {
        for (RamNode& node : ram_nodes) {
            node.decay();
        }
    }

    std::size_t size() const {
        return ram_nodes.size();
    }
};
