#pragma once

#include <stdint.h>
#include <errno.h>
#include <new>

#include "gtest/gtest.h"

#include "common/common.h"
#include "extents/extents.h"

#include "mock_device.hpp"
#include "test_fixture.hpp"

class ExtentTreeFixture : public TestFixture {
    protected:
        void SetUp() override {
            TestFixture::SetUp();
            inode_space = {
                .pr_start      = 0,
                .pr_blk_offset = 0,
                .pr_nbytes     = 256
            };
            init_err = extent_tree_init(&idx_spec, &inode_space, &ext_idx);
        }

        idx_struct_t ext_idx = {0,};
        paddr_range_t inode_space;
        int init_err;
};

