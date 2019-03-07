#pragma once

#include "test/test_utils.hpp"

#include "levelhash/levelhash.h"

class LevelHashingFixture : public TestFixture {
    protected:
        void SetUp() override {
            TestFixture::SetUp();
            set_restricted_data_alloc(false);
            srand(0);
            inode_space = {
                .pr_start      = 0,
                .pr_blk_offset = 0,
                .pr_nbytes     = 256
            };

            // Since the per-inode extent trees don't initially allocate any
            // space, the first data block allocated could be zero. Since this
            // could also be an error condition, we will go ahead and allocate a
            // dummy block here.
            device.allocate(1);

            init_err = levelhash_init(&idx_spec, &inode_space, &level_idx);
        }

        void TearDown() override {
            TestFixture::TearDown();
            // Deallocate all blocks to reset everything.
            device.deallocate();
        }

        idx_struct_t level_idx = {};
        paddr_range_t inode_space = {};
        int init_err = 0;
};
