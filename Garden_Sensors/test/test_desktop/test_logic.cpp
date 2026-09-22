#include <unity.h>

#include "sensor_logic.h"

void test_calculate_moisture_at_air_point_is_zero() {
  TEST_ASSERT_EQUAL_INT(0, calculateMoisturePercent(3500, 3500, 100));
}

void test_calculate_moisture_at_water_point_is_hundred() {
  TEST_ASSERT_EQUAL_INT(100, calculateMoisturePercent(100, 3500, 100));
}

void test_calculate_moisture_midpoint() {
  TEST_ASSERT_EQUAL_INT(50, calculateMoisturePercent(1800, 3500, 100));
}

void test_calculate_moisture_clamps_low() {
  TEST_ASSERT_EQUAL_INT(0, calculateMoisturePercent(4500, 3500, 100));
}

void test_calculate_moisture_clamps_high() {
  TEST_ASSERT_EQUAL_INT(100, calculateMoisturePercent(-200, 3500, 100));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_calculate_moisture_at_air_point_is_zero);
  RUN_TEST(test_calculate_moisture_at_water_point_is_hundred);
  RUN_TEST(test_calculate_moisture_midpoint);
  RUN_TEST(test_calculate_moisture_clamps_low);
  RUN_TEST(test_calculate_moisture_clamps_high);
  return UNITY_END();
}
