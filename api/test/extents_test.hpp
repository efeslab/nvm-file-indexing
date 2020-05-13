#pragma once

#include "test/test_utils.hpp"

#include "extents/ext_util.h"
#include "extents/extents.h"

class ExtentTreeFixture : public TestFixture,
                          public ::testing::WithParamInterface<bool>  {
    protected:
        void SetUp() override {
            TestFixture::SetUp();
            inode_space = {
                .pr_start      = 0,
                .pr_blk_offset = 0,
                .pr_nbytes     = 256
            };

            init_err = extent_tree_init(&idx_spec, &inode_space, &ext_idx);
            // Since the per-inode extent trees don't initially allocate any
            // space, the first data block allocated could be zero. Since this
            // could also be an error condition, we will go ahead and allocate a
            // dummy block here.
            device.allocate(1);

            extent_tree_set_caching(&ext_idx, GetParam());
        }

        void TearDown() override {
            TestFixture::TearDown();
            // Deallocate all blocks to reset everything.
            device.deallocate();
        }

        idx_struct_t ext_idx = {};
        paddr_range_t inode_space = {};
        int init_err = 0;
};

bool operator== (const ext_meta_t& lhs, const ext_meta_t& rhs);
bool operator== (const idx_struct_t& lhs, const idx_struct_t& rhs);
