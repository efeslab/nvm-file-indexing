#pragma once

#include <stdint.h>
#include <errno.h>
#include <new>

#include "gtest/gtest.h"

#include "common/common.h"
#include "cuckoohash/cuckoohash.h"

#include "test_fixture.hpp"

class CuckooHashFixture : public TestFixture {
    protected:
        void SetUp() override {
            TestFixture::SetUp();
            init_err = cuckoohash_initialize(&idx_spec, &cht, &metadata_loc);
        }

        idx_struct_t cht = {};
        paddr_t metadata_loc = 0;
        int init_err = 0;
};

