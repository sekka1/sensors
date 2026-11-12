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

void test_calculate_capacitive_moisture_at_air_point_is_zero() {
  TEST_ASSERT_EQUAL_INT(0, calculateMoisturePercent(3200, 3200, 1300));
}

void test_calculate_capacitive_moisture_at_water_point_is_hundred() {
  TEST_ASSERT_EQUAL_INT(100, calculateMoisturePercent(1300, 3200, 1300));
}

void test_calculate_capacitive_moisture_midpoint() {
  TEST_ASSERT_EQUAL_INT(50, calculateMoisturePercent(2250, 3200, 1300));
}

void test_rssi_quality_labels() {
  TEST_ASSERT_EQUAL_STRING("excellent", rssiQualityLabel(-45));
  TEST_ASSERT_EQUAL_STRING("excellent", rssiQualityLabel(-60));
  TEST_ASSERT_EQUAL_STRING("good", rssiQualityLabel(-61));
  TEST_ASSERT_EQUAL_STRING("good", rssiQualityLabel(-70));
  TEST_ASSERT_EQUAL_STRING("marginal", rssiQualityLabel(-71));
  TEST_ASSERT_EQUAL_STRING("marginal", rssiQualityLabel(-80));
  TEST_ASSERT_EQUAL_STRING("poor", rssiQualityLabel(-81));
}

void test_wifi_loss_restart_logic() {
  TEST_ASSERT_FALSE(shouldRestartForWifiLoss(0, 999999, 120000));      // wifi up
  TEST_ASSERT_FALSE(shouldRestartForWifiLoss(1000, 100000, 120000));   // down, under threshold
  TEST_ASSERT_TRUE(shouldRestartForWifiLoss(1000, 121000, 120000));    // exactly at threshold
  TEST_ASSERT_TRUE(shouldRestartForWifiLoss(1000, 500000, 120000));    // well past
  TEST_ASSERT_TRUE(shouldRestartForWifiLoss(4294967000UL, 120000UL, 120000UL)); // millis rollover
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_calculate_moisture_at_air_point_is_zero);
  RUN_TEST(test_calculate_moisture_at_water_point_is_hundred);
  RUN_TEST(test_calculate_moisture_midpoint);
  RUN_TEST(test_calculate_moisture_clamps_low);
  RUN_TEST(test_calculate_moisture_clamps_high);
  RUN_TEST(test_calculate_capacitive_moisture_at_air_point_is_zero);
  RUN_TEST(test_calculate_capacitive_moisture_at_water_point_is_hundred);
  RUN_TEST(test_calculate_capacitive_moisture_midpoint);
  RUN_TEST(test_rssi_quality_labels);
  RUN_TEST(test_wifi_loss_restart_logic);
  return UNITY_END();
}
