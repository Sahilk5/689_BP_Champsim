#ifndef BRANCH_WISARD_H
#define BRANCH_WISARD_H

#include <vector>
#include <cstdint>
#include <array>

#include "modules.h"
#include "discriminator.h"
#include "msl/bits.h"
#include "msl/fwcounter.h"

class wisard : public champsim::modules::branch_predictor {
private:
    //hardware knobs
    constexpr static uint32_t NUM_RAM_NODES = 12;
    constexpr static uint32_t TABLE_SIZE = 1024;
    constexpr static uint8_t COUNTER_BITS = 8;

    Discriminator disc_taken;
    Discriminator disc_not_taken;

    //input building

    //used for passing values from prediction to training
    struct wisard_result {
        std::vector<uint64_t> addresses;
        uint32_t sum_taken = 0;
        uint32_t sum_not_taken = 0;
        bool prediction = false;
    };

    wisard_result last_result;

    td::vector<uint64_t> compute_ram_addresses(uint64_t pc);
    bool apply_bleaching(const std::vector<uint64_t>& addresses);

public:
    wisard_predictor();
    bool predict_branch(champsim::address pc);
    void last_branch_result(champsim::address pc, champsim::address branch_target, bool taken, uint8_t branch_type);
}
