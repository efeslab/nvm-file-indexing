#pragma once

#include <stdint.h>
#include <errno.h>
#include <new>

#include "gtest/gtest.h"

#include "common/common.h"
#include "p-hot/p-hot.h"

#include "test_fixture.hpp"

class PHotFixture : public TestFixture {
    protected:
        void SetUp() override {
            TestFixture::SetUp();
            inode_space = {
                .pr_start      = 1,
                .pr_blk_offset = 0,
                .pr_nbytes     = 64,
            };

            // Since the per-inode extent trees don't initially allocate any
            // space, the first data block allocated could be zero. Since this
            // could also be an error condition, we will go ahead and allocate a
            // dummy block here.
            device.allocate(2);

            init_err = p_hot_init(&idx_spec, &inode_space, &p_hot);
        }

        idx_struct_t p_hot = {};
        paddr_range_t inode_space = {};
        int init_err = 0;
};

