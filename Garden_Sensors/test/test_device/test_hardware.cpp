#include <Arduino.h>
#include <unity.h>

void test_device_sanity() {
  TEST_ASSERT_TRUE(true);
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_device_sanity);
  UNITY_END();
}

void loop() {}
