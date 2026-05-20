// test/lib/test_math_utils_gtest.cpp - tests for lib/math_utils
#include <gtest/gtest.h>
#include <cmath>
#include "../../lib/math_utils.h"

TEST(MathUtilsTest, ClampInt) {
    EXPECT_EQ(lib_math::clamp(5, 0, 10), 5);
    EXPECT_EQ(lib_math::clamp(-3, 0, 10), 0);
    EXPECT_EQ(lib_math::clamp(15, 0, 10), 10);
    EXPECT_EQ(lib_math::clamp(0, 0, 10), 0);
    EXPECT_EQ(lib_math::clamp(10, 0, 10), 10);
}

TEST(MathUtilsTest, ClampFloat) {
    EXPECT_FLOAT_EQ(lib_math::clamp(0.5f, 0.0f, 1.0f), 0.5f);
    EXPECT_FLOAT_EQ(lib_math::clamp(-1.0f, 0.0f, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(lib_math::clamp(2.0f, 0.0f, 1.0f), 1.0f);
}

TEST(MathUtilsTest, Sign) {
    EXPECT_EQ(lib_math::sign(0), 0);
    EXPECT_EQ(lib_math::sign(5), 1);
    EXPECT_EQ(lib_math::sign(-5), -1);
    EXPECT_EQ(lib_math::sign(0.0), 0);
    EXPECT_EQ(lib_math::sign(3.14), 1);
    EXPECT_EQ(lib_math::sign(-3.14), -1);
}

TEST(MathUtilsTest, Lerp) {
    EXPECT_FLOAT_EQ(lib_math::lerp(0.0f, 10.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(lib_math::lerp(0.0f, 10.0f, 1.0f), 10.0f);
    EXPECT_FLOAT_EQ(lib_math::lerp(0.0f, 10.0f, 0.5f), 5.0f);
    EXPECT_FLOAT_EQ(lib_math::lerp(-5.0f, 5.0f, 0.5f), 0.0f);
    // extrapolation
    EXPECT_FLOAT_EQ(lib_math::lerp(0.0f, 10.0f, 1.5f), 15.0f);
}

TEST(MathUtilsTest, AbsVal) {
    EXPECT_EQ(lib_math::abs_val(5), 5);
    EXPECT_EQ(lib_math::abs_val(-5), 5);
    EXPECT_EQ(lib_math::abs_val(0), 0);
    EXPECT_FLOAT_EQ(lib_math::abs_val(-3.14f), 3.14f);
}

TEST(MathUtilsTest, MinMax) {
    EXPECT_EQ(lib_math::min_val(3, 7), 3);
    EXPECT_EQ(lib_math::min_val(7, 3), 3);
    EXPECT_EQ(lib_math::max_val(3, 7), 7);
    EXPECT_EQ(lib_math::max_val(7, 3), 7);
    EXPECT_EQ(lib_math::min_val(-3, -7), -7);
    EXPECT_EQ(lib_math::max_val(-3, -7), -3);
}

TEST(MathUtilsTest, ClampByte) {
    EXPECT_EQ(clamp_byte(0), 0u);
    EXPECT_EQ(clamp_byte(255), 255u);
    EXPECT_EQ(clamp_byte(128), 128u);
    EXPECT_EQ(clamp_byte(-1), 0u);
    EXPECT_EQ(clamp_byte(-1000), 0u);
    EXPECT_EQ(clamp_byte(256), 255u);
    EXPECT_EQ(clamp_byte(1 << 20), 255u);
}

TEST(MathUtilsTest, ClampUnit) {
    EXPECT_FLOAT_EQ(clamp_unit(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(clamp_unit(1.0f), 1.0f);
    EXPECT_FLOAT_EQ(clamp_unit(0.5f), 0.5f);
    EXPECT_FLOAT_EQ(clamp_unit(-0.1f), 0.0f);
    EXPECT_FLOAT_EQ(clamp_unit(1.1f), 1.0f);
    EXPECT_FLOAT_EQ(clamp_unit(-1000.0f), 0.0f);
    EXPECT_FLOAT_EQ(clamp_unit(1000.0f), 1.0f);
}

TEST(MathUtilsTest, MacroFormsWorkInCppToo) {
    // these macros are exposed to both C and C++.
    EXPECT_EQ(LMB_CLAMP(5, 0, 10), 5);
    EXPECT_EQ(LMB_MIN(3, 7), 3);
    EXPECT_EQ(LMB_MAX(3, 7), 7);
    EXPECT_EQ(LMB_ABS(-4), 4);
    EXPECT_EQ(LMB_SIGN(-2), -1);
    EXPECT_EQ(LMB_SIGN(2), 1);
    EXPECT_EQ(LMB_SIGN(0), 0);
}
