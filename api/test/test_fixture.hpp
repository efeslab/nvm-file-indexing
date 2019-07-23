#pragma once

#include <stdint.h>
#include <errno.h>
#include <new>

#include "gtest/gtest.h"

#include "common/common.h"

#include "mock_device.hpp"

class TestFixture : public ::testing::Test {
    public:
        static const size_t NBLK;
        static const size_t BLK_SZ;
        static MockDevice device;

        static void set_restricted_data_alloc(bool enable) { enable_ = enable; }

        static int mock_get_dev_info(device_info_t* di);

        static ssize_t mock_alloc_metadata(size_t nblocks, paddr_t *blk);

        static ssize_t mock_dealloc_metadata(size_t nblocks, paddr_t blk);

        static ssize_t mock_alloc_data(size_t nblocks, paddr_t *blk);

        static ssize_t mock_dealloc_data(size_t nblocks, paddr_t blk);

        static ssize_t mock_read(paddr_t blk, off_t off, size_t nbytes, char* buf);

        static ssize_t mock_write(paddr_t blk, off_t off, size_t nbytes, const char* buf);

        static ssize_t mock_get_addr(paddr_t blk, off_t off, char **buf);

    protected:
        virtual void SetUp() override;

        virtual void TearDown() override;

        idx_spec_t idx_spec;

        static bool enable_;
};

