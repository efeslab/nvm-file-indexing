#pragma once

#include <stdint.h>
#include <errno.h>
#include <new>

#include "gtest/gtest.h"

#include "common/common.h"
#include "radixtree/radixtree.h"

#include "test_fixture.hpp"

class RadixTreeFixture : public TestFixture {
    protected:
        void SetUp() override {
            TestFixture::SetUp();
            // Block 0 is considered taboo.
            metadata_loc = device.allocate(2) + 1;
            init_err = radixtree_init(&idx_spec, &radixtree, &metadata_loc);
        }

        idx_struct_t radixtree = {};
        paddr_t metadata_loc;
        int init_err = 0;
};

