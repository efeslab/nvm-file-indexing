#include "stats_test.hpp"

#include <iostream>

using namespace std;

TEST(StatsTest, StructInit) {
    test_stats_t s;
    INIT_STATS(&s);
}

TEST(StatsTest, StructVarInit) {
    test_stats_var_t s;
    
    for (int i = 0; i < s.nfields; ++i) {
        cout << s.field_names[i] << endl;
    }

    cout << s.a_tsc << endl;
    cout << s.b_tsc << endl;
}

TEST(StatsTest, StructOmega) {
    omega_stats_t os;
    INIT_STATS(&os);

    print_omega_stats(&os);
}
