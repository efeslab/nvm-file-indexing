#pragma once

#include <stdint.h>
#include <errno.h>
#include <new>

#include "gtest/gtest.h"

#include "common/common.h"

#include "mock_device.hpp"
#include "test_fixture.hpp"

bool operator== (const paddr_range_t& lhs, const paddr_range_t& rhs);
