#pragma once

#include <stdint.h>
#include <errno.h>
#include <new>

#include "gtest/gtest.h"

#include "common/common.h"
#include "hashtable/hashtable.h"

#include "mock_device.hpp"

class HashTableFixture : public ::testing::Test {
    public:
        static const size_t NBLK;
        static const size_t BLK_SZ;
        static MockDevice device;

        static int mock_get_dev_info(device_info_t* di) {
            di->di_size_blocks = NBLK;
            di->di_block_size  = BLK_SZ;
            return 0;
        }

        static ssize_t mock_alloc_metadata(size_t nblocks, paddr_t *blk) {
            *blk = device.allocate(nblocks);
            return (ssize_t)(nblocks);
        }

        static ssize_t mock_dealloc_metadata(size_t nblocks, paddr_t blk) {
            device.deallocate(blk, nblocks);
            return (ssize_t)(nblocks);
        }

        static ssize_t mock_alloc_data(size_t nblocks, paddr_t *blk) {
            return mock_alloc_metadata(nblocks, blk);
        }

        static ssize_t mock_dealloc_data(size_t nblocks, paddr_t blk) {
            return mock_dealloc_metadata(nblocks, blk);
        }

        static ssize_t mock_read(paddr_t blk, off_t off, size_t nbytes, char* buf) {
            return device[blk].read(off, nbytes, buf);
        }

        static ssize_t mock_write(paddr_t blk, off_t off, size_t nbytes, const char* buf) {
            return device[blk].write(off, nbytes, buf);
        }

    protected:
        void SetUp() override {
           idx_spec.idx_mem_man            = new mem_man_fns_t;
           idx_spec.idx_mem_man->mm_malloc = malloc;
           idx_spec.idx_mem_man->mm_free   = free;

           idx_spec.idx_callbacks                      = new callback_fns_t;
           idx_spec.idx_callbacks->cb_write            = mock_write;
           idx_spec.idx_callbacks->cb_read             = mock_read;
           idx_spec.idx_callbacks->cb_alloc_metadata   = mock_alloc_metadata;
           idx_spec.idx_callbacks->cb_alloc_data       = mock_alloc_data;
           idx_spec.idx_callbacks->cb_dealloc_metadata = mock_dealloc_metadata;
           idx_spec.idx_callbacks->cb_dealloc_data     = mock_dealloc_data;
           idx_spec.idx_callbacks->cb_get_dev_info     = mock_get_dev_info;
        }

        void TearDown() override {

           delete idx_spec.idx_mem_man;
           delete idx_spec.idx_callbacks;
        }


        idx_spec_t idx_spec;
};

