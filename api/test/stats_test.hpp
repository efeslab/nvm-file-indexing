#include "gtest/gtest.h"
#include "common/common.h"

extern "C" {
    typedef struct test_stats {
        STAT_FIELD(a);
        STAT_FIELD(b);
    } test_stats_t;
}
