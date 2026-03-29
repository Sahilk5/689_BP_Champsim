#include "wisard.h"

//instantiate the predictor class 
//WIP
wisard::wisard(O3_CPU* cpu) 
    : champsim::modules::branch_predictor(cpu),
      disc_taken(NUM_RAM_NODES, TUPLE_BITS, COUNTER_BITS),
      disc_not_taken(NUM_RAM_NODES, TUPLE_BITS, COUNTER_BITS) 
{
    (void)cpu;

    for (uint32_t size : LHT_SIZES) {
        lhts.push_back(std::vector<uint32_t>(size, 0));
    }
    last_result.addresses.resize(NUM_RAM_NODES, 0);
    static_ram_mapping.resize(NUM_RAM_NODES, std::vector<int>(TUPLE_BITS));
    
    std::vector<int> input_indices(2158);
    std::iota(input_indices.begin(), input_indices.end(), 0);
    
    //shuffle with fixed random seed
    //can try a bunch of these and see which seed does best
    std::mt19937 rng(67); 
    std::shuffle(input_indices.begin(), input_indices.end(), rng);

    int idx_ptr = 0;
    for (uint32_t i = 0; i < NUM_RAM_NODES; ++i) {
        for (uint32_t b = 0; b < TUPLE_BITS; ++b) {
            //if we need more than 2158 bits, just reshuffle
            //later can replace this with using overlapping tuples
            //but only if we have time and it's unclear if it's actually beneficial (according to authors)
            if (idx_ptr >= 2158) {
                std::shuffle(input_indices.begin(), input_indices.end(), rng);
                idx_ptr = 0;
            }
            static_ram_mapping[i][b] = input_indices[idx_ptr++];
        }
    }
}

std::vector<uint64_t> wisard::compute_ram_addresses(champsim::address pc){
    //architectural information
    uint64_t pc_24 = pc.slice_lower<champsim::data::bits{PC_LENGTH}>().to<uint64_t>();
    uint64_t ghr_24 = spec_global_history.to_ullong(); 
    uint64_t pc_xor_ghr = pc_24 ^ ghr_24;
    uint64_t lhr0 = lhts[0][pc_24 % LHT_SIZES[0]] & 0xFFFFFF; 
    uint64_t lhr1 = lhts[1][pc_24 % LHT_SIZES[1]] & 0xFFFF;   
    uint64_t lhr2 = lhts[2][pc_24 % LHT_SIZES[2]] & 0x1FF;    
    uint64_t lhr3 = lhts[3][pc_24 % LHT_SIZES[3]] & 0x7F;     
    uint64_t lhr4 = lhts[4][pc_24 % LHT_SIZES[4]] & 0x1F;

    std::vector<uint64_t> addresses(NUM_RAM_NODES);
    std::bitset<2158> input_vector;
    int offset = 0;

    // Helper lambda to append 'val' repeatedly according to its 'weight'
    auto append_bits = [&](uint64_t val, int bit_width, int replications_weight) {
        for (int r = 0; r < replications_weight; ++r) {
            for (int b = 0; b < bit_width; ++b) {
                input_vector[offset++] = (val >> b) & 1;
            }
        }
    };

    //input should be exactly 2158
    //TODO: verify this?
    append_bits(pc_24,       24, 24); // a = 24
    append_bits(ghr_24,      24, 12); // b = 12
    append_bits(pc_xor_ghr,  24, 12); // c = 12
    append_bits(lhr0,        24,  8); // d0 = 8
    append_bits(lhr1,        16,  8); // d1 = 8
    append_bits(lhr2,         9,  8); // d2 = 8
    append_bits(lhr3,         7,  6); // d3 = 6
    append_bits(lhr4,         5, 12); // d4 = 12
    append_bits(global_path_history, 64, 8); // e = 8

    for (uint32_t i = 0; i < NUM_RAM_NODES; ++i) {
        uint64_t addr = 0;
        for (uint32_t b = 0; b < TUPLE_BITS; ++b) {
            //based on the mapping done in the constructor
            int mapped_index = static_ram_mapping[i][b];
            //set the corresponding bit in the RAM address
            if (input_vector[mapped_index]) {
                addr |= (1ULL << b);
            }
        }
        addresses[i] = addr;
    }

    return addresses;

}

bool wisard::apply_bleaching(const std::vector<uint64_t>& addresses){
    uint32_t b = 1; 
    uint32_t max_val = (1 << COUNTER_BITS) - 1;

    while (b <= max_val) {
        uint32_t sum_not_taken = disc_not_taken.get_response(addresses, b);
        uint32_t sum_taken = disc_taken.get_response(addresses, b);

        if (sum_taken > sum_not_taken) return true;
        if (sum_not_taken > sum_taken) return false;
        b++;
    }
    return false;
}

bool wisard::predict_branch(champsim::address pc){

    last_result.addresses = compute_ram_addresses(pc);
    last_result.sum_not_taken = disc_not_taken.get_response(last_result.addresses, 0);
    last_result.sum_taken = disc_taken.get_response(last_result.addresses, 0);

    if (last_result.sum_taken > last_result.sum_not_taken) {
        last_result.prediction = true;
    } else if (last_result.sum_not_taken > last_result.sum_taken) {
        last_result.prediction = false;
    } else {
        last_result.prediction = apply_bleaching(last_result.addresses);
    }

    //speculative update
    spec_global_history <<= 1;
    spec_global_history.set(0, last_result.prediction);

    return last_result.prediction;
}

void wisard::last_branch_result(champsim::address pc_addr, champsim::address branch_target, bool taken, uint8_t branch_type){

    uint64_t pc = pc_addr.to<uint64_t>();

    if (++branches_since_decay >= DECAY_INTERVAL) {
        disc_taken.decay();
        disc_not_taken.decay();
        branches_since_decay = 0;
    }

    //train
    if (taken) {
        disc_taken.train(last_result.addresses);
    } else {
        disc_not_taken.train(last_result.addresses);
    }

    //update real ghist
    real_global_history <<= 1;
    real_global_history.set(0, taken);

    //overwrite speculative update if wrong
    if (last_result.prediction != taken) {
        spec_global_history = real_global_history;
    }

    //update local histories
    for (uint32_t i = 0; i < NUM_LHTS; i++) {
        uint64_t lht_idx = pc % LHT_SIZES[i];
        uint32_t width = LHT_LENGTHS[i];
        uint32_t mask = (width >= 32) ? 0xFFFFFFFFu : ((uint32_t{1} << width) - 1);
        lhts[i][lht_idx] = ((lhts[i][lht_idx] << 1) | (taken ? 1u : 0u)) & mask;
    }

    //update GPHR
    global_path_history <<= PC_BITS_TO_SHIFT;
    uint64_t pc_bits = pc_addr.slice_lower<champsim::data::bits{PC_BITS_TO_SHIFT}>().to<uint64_t>();
    global_path_history ^= pc_bits;
    global_path_history &= ((1ULL << GPHR_LENGTH) - 1);
}
