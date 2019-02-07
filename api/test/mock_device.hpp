#pragma once

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <vector>

#include "gtest/gtest.h"

#include "common.h"

struct MockMemPage {
    MockMemPage(size_t size) : data(size) {}

    ssize_t read(off_t offset, size_t bytes, char *buf) {
        if (offset + bytes > data.size()) return -EINVAL;
        memmove(buf, data.data() + offset, bytes);
        return (ssize_t)bytes;
    }

    ssize_t write(off_t offset, size_t bytes, const char *buf) {
        if (offset + bytes > data.size()) return -EINVAL;
        memmove(data.data() + offset, buf, bytes);
        return (ssize_t)bytes;
    }

    std::vector<char> data;
};

struct MockDevice {
    MockDevice(size_t nblocks, size_t block_sz)
        : device(nblocks, MockMemPage(block_sz)),
          allocated(nblocks, false) {}

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

    std::vector<MockMemPage> device;
    std::vector<bool> allocated;

};
