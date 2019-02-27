#include "stats_test.hpp"

#include <iostream>

using namespace std;

TEST(StatsTest, StructInit) {
    test_stats_t s;
    INIT_STATS(&s);
}


