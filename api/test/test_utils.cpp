#include "test_utils.hpp"

bool operator== (const paddr_range_t& lhs, const paddr_range_t& rhs) {
    return lhs.pr_blk_offset == rhs.pr_blk_offset &&
           lhs.pr_nbytes     == rhs.pr_nbytes     &&
           lhs.pr_start      == rhs.pr_start;
}
