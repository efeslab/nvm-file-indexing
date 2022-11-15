#include "gtest/gtest.h"

#include "mock_device.hpp"
#include "hashtable_test.hpp"

int main(int argc, char **argv){
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
