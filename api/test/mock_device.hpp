#pragma once

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <vector>

#include "gtest/gtest.h"

#include "common/common.h"

struct MockMemPage {
    MockMemPage(size_t size) :
        data_(size, 0),
        data_ptr_(data_.data()),
        data_sz_(size) {}

    MockMemPage(char *data_addr, size_t sz) :
        data_ptr_(data_addr),
        data_sz_(sz) {}

    ssize_t read(off_t offset, size_t bytes, char *buf) {
        if (offset + bytes > data_sz_) return -EINVAL;
        memmove(buf, data_ptr_ + offset, bytes);
        return (ssize_t)bytes;
    }

    ssize_t write(off_t offset, size_t bytes, const char *buf) {
        if (offset + bytes > data_sz_) return -EINVAL;
        memmove(data_ptr_ + offset, buf, bytes);
        return (ssize_t)bytes;
    }

    size_t size() const { return data_sz_; }

    char *data()  const { return data_ptr_; }

    std::vector<char> data_;
    char *data_ptr_;
    size_t   data_sz_;
};

struct MockDevice {
    MockDevice(size_t nblocks, size_t block_sz)
        : raw_device(nblocks * block_sz, 0),
          allocated(nblocks, false) {

        for (size_t blk = 0; blk < nblocks; ++blk) {
            char* start_addr = raw_device.data() + (blk * block_sz);
            device.emplace_back(start_addr, block_sz);
        }

    }

    void set_range(paddr_t block, size_t nblocks, bool is_allocated) {
        for (paddr_t i = block; i < block + nblocks; ++i) {
            ASSERT_NE(allocated[i], is_allocated);
            allocated[i] = is_allocated;
        }
    }

    paddr_t allocate(size_t nblocks) {
        paddr_t largest_region = -1;
        size_t  largest_region_size = -1;

        paddr_t i = 0;
        paddr_t cur = 0;
        size_t  cur_size = 0;
        while(i < device.size()) {
            if (!allocated[i] && cur_size == 0) {
                cur = i;
                cur_size = 1;
            } else if (!allocated[i]) {
                ++cur_size;
                if (cur_size == nblocks) break;
            } else {
                cur_size = 0;
            }

            ++i;
        }

        set_range(cur, nblocks, true);
        return cur;
    }

    void deallocate(paddr_t block, size_t nblocks) {
        set_range(block, nblocks, false);
    }

    MockMemPage& operator[](int i) {
        return device[i];
    }

    std::vector<char> raw_device;
    std::vector<MockMemPage> device;
    std::vector<bool> allocated;

};
