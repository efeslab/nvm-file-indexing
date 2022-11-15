#pragma once

#include "test/test_utils.hpp"

#include "extents/ext_util.h"
#include "extents/extents.h"

class LockFixture : public TestFixture {
    protected:
        void SetUp() override {
            TestFixture::SetUp();

            memset(&pmlock, 0, sizeof(pmlock));
        }

        void TearDown() override {
            TestFixture::TearDown();
        }

        pmlock_t pmlock;
};
