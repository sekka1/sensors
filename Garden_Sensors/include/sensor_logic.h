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

// Human-readable WiFi signal quality from RSSI (dBm)
inline const char* rssiQualityLabel(int rssiDbm) {
  if (rssiDbm >= -60) {
    return "excellent";
  }
  if (rssiDbm >= -70) {
    return "good";
  }
  if (rssiDbm >= -80) {
    return "marginal";
  }
  return "poor";
}

// True when WiFi has been down continuously for at least thresholdMs.
// lostSinceMs == 0 means WiFi is currently up. Unsigned math is millis()-rollover safe.
inline bool shouldRestartForWifiLoss(unsigned long lostSinceMs, unsigned long nowMs, unsigned long thresholdMs) {
  if (lostSinceMs == 0) {
    return false;
  }
  return (nowMs - lostSinceMs) >= thresholdMs;
}

#endif
