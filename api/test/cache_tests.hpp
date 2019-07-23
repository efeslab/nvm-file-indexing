#pragma once

#include "test/test_utils.hpp"
#include "extents/extents.h"
#include "hashtable/hashtable.h"
#include "levelhash/levelhash.h"
#include "radixtree/radixtree.h"

class CachingTestFixture : public TestFixture,
                           public ::testing::WithParamInterface<idx_fns_t*>  {
    protected:
        void SetUp() override {
            TestFixture::SetUp();
            inode_space = {
                .pr_start      = 0,
                .pr_blk_offset = 0,
                .pr_nbytes     = 256
            };

            idx_fns = GetParam();

            if (idx_fns->im_init) {
                metadata_loc = device.allocate(2) + 1;
                init_err = idx_fns->im_init(&idx_spec, &idx_struct,
                                            &metadata_loc);
                init_err |= idx_fns->im_init(&idx_spec, &idx_other,
                                            &metadata_loc);
            } else {
                // Since the per-inode extent trees don't initially allocate any
                // space, the first data block allocated could be zero. Since this
                // could also be an error condition, we will go ahead and allocate a
                // dummy block here.
                device.allocate(1);

                init_err = idx_fns->im_init_prealloc(&idx_spec, &inode_space,
                                                     &idx_struct);
                init_err |= idx_fns->im_init_prealloc(&idx_spec, &inode_space,
                                                     &idx_other);
            }

            FN(&idx_struct, im_set_caching, &idx_struct, true);
            FN(&idx_other, im_set_caching, &idx_other, false);
        }

        void TearDown() override {
            TestFixture::TearDown();
            // Deallocate all blocks to reset everything.
            device.deallocate();
            // Invalidate caches.
            FN(&idx_struct, im_invalidate, &idx_struct);
            FN(&idx_other, im_invalidate, &idx_other);
        }

        idx_fns_t *idx_fns = nullptr;
        idx_struct_t idx_struct = {};
        idx_struct_t idx_other = {};
        paddr_range_t inode_space = {};
        paddr_t metadata_loc = 0;
        int init_err = 0;
};
