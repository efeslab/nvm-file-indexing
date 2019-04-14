#pragma once

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <vector>

#include "gtest/gtest.h"

#include "common/common.h"

struct MockDevice {
    MockDevice(size_t nblocks, size_t block_sz)
        : raw_device_(nblocks * block_sz, 0),
          allocated_(nblocks, false),
          blksz_(block_sz) {}

    void set_range(paddr_t block, size_t nblocks, bool is_allocated) {
        for (paddr_t i = block; i < block + nblocks; ++i) {
            ASSERT_NE((bool)allocated_[i], is_allocated);
            allocated_[i] = is_allocated;
            if (!is_allocated) {
                memset(raw_device_.data() + (i * blksz_), 0, blksz_);
            }
        }
    }

    paddr_t allocate(size_t nblocks) {
        paddr_t largest_region = 0;
        size_t  largest_region_size = 0;

        if_then_panic(!nblocks, "Asked to allocate 0 blocks!\n");

        paddr_t i = 0;
        paddr_t cur = 0;
        size_t  cur_size = 0;
        while(i < allocated_.size()) {
            if (!allocated_[i] && cur_size == 0) {
                cur = i;
                cur_size = 1;
            } else if (!allocated_[i]) {
                ++cur_size;
            } else {
                if (cur_size > largest_region_size) {
                    largest_region = cur;
                    largest_region_size = cur_size;
                }

                cur_size = 0;
            }

            if (cur_size >= nblocks) {
                largest_region = cur;
                largest_region_size = cur_size;
                break;
            }

            ++i;
        }

        if_then_panic(largest_region_size == 0, "Could not allocate region!\n");

        set_range(largest_region, largest_region_size, true);
        return largest_region;
    }

    void deallocate(paddr_t block, size_t nblocks) {
        set_range(block, nblocks, false);
    }

    void deallocate() {
        for (unsigned i = 0; i < allocated_.size(); ++i) {
            if (allocated_[i]) {
                deallocate(i, 1);
            }
        }
    }

    size_t num_allocated() const {
        size_t num = 0;
        for (bool is_alloc : allocated_) {
            num += is_alloc ? 1 : 0;
        }

        return num;
    }

    char* operator[](int blockno) {
        return raw_device_.data() + (blockno * blksz_);
    }

    size_t blksz_;
    std::vector<char> raw_device_;
    std::vector<bool> allocated_;
};
