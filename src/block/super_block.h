#pragma once
#include "common.h"

union super_block{
    struct {
        uint32_t magic_number;

        bid_t nr_block;

        // all in terms of block 
        bid_t s_iblock;
        bid_t nr_iblock;
        
        // all in terms of block 
        bid_t s_dblock;
        bid_t nr_dblock;

        bid_t h_dblock;
    };
    uint8_t data[BLOCK_SIZE];
};