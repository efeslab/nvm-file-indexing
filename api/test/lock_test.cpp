#include "lock_test.hpp"

TEST_F(LockFixture, RdLock) {

    pmlock_rd_lock(&pmlock);

    ASSERT_EQ(1, pmlock.nreaders);

    pmlock_rd_unlock(&pmlock);

    ASSERT_EQ(0, pmlock.nreaders);
}
