#include <gtest/gtest.h>

#include "util/rate_limit.hpp"

#include <chrono>
#include <thread>

TEST(RateLimiter, BurstThenRefuseThenRefill) {
    RateLimiter limiter(3, 1);   // burst 3, 1 token per second

    EXPECT_TRUE(limiter.allow("1.2.3.4"));
    EXPECT_TRUE(limiter.allow("1.2.3.4"));
    EXPECT_TRUE(limiter.allow("1.2.3.4"));
    EXPECT_FALSE(limiter.allow("1.2.3.4")) << "4th quick request must be refused";

    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_TRUE(limiter.allow("1.2.3.4")) << "one token should have refilled after 1 s";
    EXPECT_FALSE(limiter.allow("1.2.3.4")) << "but only one";
}

TEST(RateLimiter, KeysHaveSeparateBuckets) {
    RateLimiter limiter(1, 1);
    EXPECT_TRUE(limiter.allow("a"));
    EXPECT_FALSE(limiter.allow("a"));
    EXPECT_TRUE(limiter.allow("b")) << "b must not be charged for a's requests";
}
