#ifndef SENSOR_LOGIC_H
#define SENSOR_LOGIC_H

inline int clampPercent(long value) {
  if (value < 0) {
    return 0;
  }
  if (value > 100) {
    return 100;
  }
  return static_cast<int>(value);
}

inline int calculateMoisturePercent(int rawValue, int airValue, int waterValue) {
  if (airValue == waterValue) {
    return 0;
  }

  const long numerator = static_cast<long>(rawValue - airValue) * 100L;
  const long denominator = static_cast<long>(waterValue - airValue);
  const long percent = numerator / denominator;
  return clampPercent(percent);
}

#endif
