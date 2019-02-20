#pragma once

#include <stdint.h>
#include <errno.h>
#include <new>

#include "gtest/gtest.h"

#include "common/common.h"
#include "hashtable/hashtable.h"

#include "test_fixture.hpp"

class HashTableFixture : public TestFixture {
    protected:
        void SetUp() override {
            TestFixture::SetUp();
            init_err = hashtable_initialize(&idx_spec, &hashtable, &metadata_loc);
        }

        idx_struct_t hashtable = {};
        paddr_t metadata_loc = 0;
        int init_err = 0;
};

