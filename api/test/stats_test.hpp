#include "gtest/gtest.h"
#include "common/common.h"

extern "C" {
    typedef struct test_stats {
        STAT_FIELD(a);
        STAT_FIELD(b);
    } test_stats_t;

    typedef struct test_stats_var {
        STAT_FIELDS(a, b);
    } test_stats_var_t;

    STAT_STRUCT(omega_stats, foo);
}
