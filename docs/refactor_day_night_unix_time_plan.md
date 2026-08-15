# Refactoring Plan: `DayNightController` — Unix Time API + Dawn Alert in mA

## Goal

Two related cleanups to eliminate boilerplate and improve safety:

1. **Replace `(bool is_synced, uint8_t hour, uint8_t minute, uint16_t day_of_year)` params** with `std::optional<time_t> unix_time` across all three `DayNightController` methods. The decomposition of `tm_hour`, `tm_min`, `tm_yday` moves inside the controller, where it belongs (domain logic). The three `localtime_r()` boilerplate blocks scattered in `solar_sensor.cpp` are eliminated.

2. **Replace raw INA226 alert register `DEFAULT_DAWN_WAKEUP_ALERT_LIMIT = 12`** with a human-readable `dawn_alert_current_ma` field in `InaSensorConfig`. The conversion to the raw 16-bit register value is computed once inside `InaSensorTask::prepare_for_sleep()`, using the shunt resistance stored in `Ina226Config`.

---

## User Review Required

> [!IMPORTANT]
> `classify_wake()` currently classifies **any** GPIO wakeup as `DAWN_GPIO`, ignoring time of day. This refactoring is the right moment to also fix that bug (the field sleep/wake loop you observed at dusk). The fix is included in the plan as **Step 2c**. If you want to separate the two concerns into independent commits, let me know.

> [!IMPORTANT]
> `localtime_r()` converts the UTC `time_t` to **local time using the system TZ**. In ESP-IDF, the TZ is configured via `setenv("TZ", ...)`. The controller already has `tz_offset_hours` in `DayNightConfig`, but currently that field is only used conceptually (not in the computation). The tests on the host will call `localtime_r()` with the system TZ, which may differ from the target TZ. This is acceptable since the tests already pass fixed `hour`/`minute` values — but after the refactoring, the host tests need to supply a `time_t` that matches UTC so `localtime_r()` returns the expected values, or we can use `gmtime_r()` + `tz_offset_hours` internally (pure, no system TZ dependency). **Which do you prefer?**
> - Option A: Use `localtime_r()` — simple, relies on system TZ (consistent with how the ESP32 sets TZ via SNTP today).
> - Option B: Use `gmtime_r()` + `config_.tz_offset_hours` — pure, no system-level TZ dependency, easier to test portably.

---

## Open Questions

> [!IMPORTANT]
> **Dawn alert threshold**: Currently `DEFAULT_DAWN_WAKEUP_ALERT_LIMIT = 12` raw units ≈ `0.3 mA`. This is clearly too sensitive (triggered by dusk noise). What should the new `dawn_alert_current_ma` default value be? Suggestion: **`5 mA`** (same as `DEFAULT_DAWN_CURRENT_THRESHOLD_MA`), so that the INA only wakes the MCU when the panel is already producing meaningful current.

---

## Background

### The 3 Boilerplate Blocks in `solar_sensor.cpp`

All three call sites repeat the exact same pattern:

```cpp
// Block 1 — run_day_cycle (L237-248)
bool is_synced = time_manager_.is_synchronized();
uint8_t hour = 0; uint8_t minute = 0; uint16_t day_of_year = 81;
if (is_synced) {
    time_t now = time_manager_.get_timestamp_sec();
    struct tm timeinfo{};
    localtime_r(&now, &timeinfo);
    hour = static_cast<uint8_t>(timeinfo.tm_hour);
    minute = static_cast<uint8_t>(timeinfo.tm_min);
    day_of_year = static_cast<uint16_t>(timeinfo.tm_yday + 1);
}
process_ina_samples(is_synced, hour, minute, day_of_year); // -> should_enter_night_mode

// Block 2 — enter_deep_sleep (L717-728)
// Same 10 lines again -> calculate_night_sleep_time_us

// Block 3 — evaluate_boot_mode (L840-847)
// Partial repeat (only hour) -> classify_wake
```

### Current Signatures (to be replaced)

```cpp
// day_night_controller.hpp
bool should_enter_night_mode(
    uint16_t current_ma,
    bool is_time_synced,
    uint8_t current_hour_local,
    uint8_t current_minute_local,
    uint16_t day_of_year);

WakeType classify_wake(
    bool is_gpio_wakeup,
    uint16_t current_ma,
    bool is_time_synced,
    uint8_t current_hour_local) const;

uint64_t calculate_night_sleep_time_us(
    bool is_time_synced,
    uint8_t current_hour_local,
    uint8_t current_minute_local,
    uint16_t day_of_year) const;
```

---

## Proposed Changes

### Step 1 — INA226 Alert Limit in mA

---

#### [MODIFY] `ina_sensor_types.hpp`

Replace the raw constant with a human-readable default in mA, and update the comment:

```diff
-/**
- * @brief Default Shunt Over Voltage raw threshold for dawn wakeup detection.
- * Calculation: I_dawn = ~0.3 mA, V_sh = 30 uV, raw = 30/2.5 = 12
- */
-static constexpr uint16_t DEFAULT_DAWN_WAKEUP_ALERT_LIMIT = 12;
+/**
+ * @brief Default dawn wakeup current threshold in mA.
+ *
+ * The INA226 SHUNT_OVER_VOLTAGE alert fires when the shunt current
+ * exceeds this value, waking the MCU from deep sleep at dawn.
+ * Must be high enough to reject dusk noise (recommend >= 5 mA).
+ */
+static constexpr uint16_t DEFAULT_DAWN_ALERT_CURRENT_MA = 5;
```

Add a new field to `InaSensorConfig`:

```diff
 struct InaSensorConfig
 {
     uint16_t delta_threshold_ma = 10;
     float delta_threshold_percent = 0.03f;
     uint16_t heartbeat_interval_ms = 1000;
     bool enable_ema_filter = true;
     float ema_alpha = 0.8f;
     uint32_t task_stack_size = 4096;
     UBaseType_t task_priority = 5;
+    uint16_t dawn_alert_current_ma = DEFAULT_DAWN_ALERT_CURRENT_MA; ///< INA dawn wakeup current threshold in mA
     InaNightConfig night_config{};
 };
```

#### [MODIFY] `ina_sensor_task.cpp` — `prepare_for_sleep()`

Replace static constant with dynamic conversion:

```diff
+// INA226 Shunt Voltage LSB = 2.5 uV/LSB.
+// V_shunt_uv = I_ma * R_shunt_mohm  =>  raw = V_shunt_uv / 2.5
+// With R_shunt = 100 mOhm: raw = I_ma * 100 / 2.5 = I_ma * 40
+float r_shunt_mohm = ina_day_config_.r_shunt_ohms * 1000.0f;
+float v_shunt_uv = static_cast<float>(config_.dawn_alert_current_ma) * r_shunt_mohm;
+uint16_t alert_limit = static_cast<uint16_t>(v_shunt_uv / 2.5f);
 err = driver_.configure_alert(
-    static_cast<uint16_t>(ina226::AlertFlag::SHUNT_OVER_VOLTAGE), DEFAULT_DAWN_WAKEUP_ALERT_LIMIT);
+    static_cast<uint16_t>(ina226::AlertFlag::SHUNT_OVER_VOLTAGE), alert_limit);
```

> [!NOTE]
> `InaSensorTask` already receives the `Ina226Config` (which contains `r_shunt_ohms`) through its constructor. The conversion happens once at `prepare_for_sleep()` time — no new dependencies needed.

---

### Step 2 — `DayNightController` Unix Time API

---

#### [MODIFY] `day_night_controller.hpp`

Replace decomposed time params with `std::optional<time_t>`:

```diff
+#include <ctime>
+#include <optional>

 bool should_enter_night_mode(
     uint16_t current_ma,
-    bool is_time_synced,
-    uint8_t current_hour_local,
-    uint8_t current_minute_local,
-    uint16_t day_of_year);
+    std::optional<time_t> unix_time);

 WakeType classify_wake(
     bool is_gpio_wakeup,
     uint16_t current_ma,
-    bool is_time_synced,
-    uint8_t current_hour_local) const;
+    std::optional<time_t> unix_time) const;

 uint64_t calculate_night_sleep_time_us(
-    bool is_time_synced,
-    uint8_t current_hour_local,
-    uint8_t current_minute_local,
-    uint16_t day_of_year) const;
+    std::optional<time_t> unix_time) const;
```

Add a private helper that centralizes the `localtime_r` decomposition:

```cpp
private:
    struct LocalTime {
        uint8_t hour;
        uint8_t minute;
        uint16_t day_of_year;
    };
    LocalTime decompose(time_t unix_time) const;
```

#### [MODIFY] `day_night_controller.cpp`

Add `decompose()` implementation:

```cpp
DayNightController::LocalTime DayNightController::decompose(time_t unix_time) const
{
    struct tm t{};
    localtime_r(&unix_time, &t);
    return {
        static_cast<uint8_t>(t.tm_hour),
        static_cast<uint8_t>(t.tm_min),
        static_cast<uint16_t>(t.tm_yday + 1)
    };
}
```

Update `should_enter_night_mode()`:

```diff
-bool DayNightController::should_enter_night_mode(
-    uint16_t current_ma, bool is_time_synced,
-    uint8_t current_hour_local, uint8_t current_minute_local, uint16_t day_of_year)
+bool DayNightController::should_enter_night_mode(
+    uint16_t current_ma, std::optional<time_t> unix_time)
 {
-    if (is_time_synced) {
+    if (unix_time.has_value()) {
-        SolarDayInfo day_info = calculate_solar_day(day_of_year);
-        float current_time_float = static_cast<float>(current_hour_local) + ...
+        auto [hour, minute, day_of_year] = decompose(*unix_time);
+        SolarDayInfo day_info = calculate_solar_day(day_of_year);
+        float current_time_float = static_cast<float>(hour) + (static_cast<float>(minute) / 60.0f);
         ...
     } else {
         required_samples = config_.unsynced_hysteresis_sample_count;
     }
```

#### 2c — Fix `classify_wake()` — Add Time Window Guard

This is the **bug fix** for the dusk wake loop. With `unix_time` now available:

```cpp
WakeType DayNightController::classify_wake(
    bool is_gpio_wakeup, uint16_t current_ma, std::optional<time_t> unix_time) const
{
    if (unix_time.has_value()) {
        auto [hour, minute, day_of_year] = decompose(*unix_time);
        SolarDayInfo day_info = calculate_solar_day(day_of_year);
        float current_time = static_cast<float>(hour) + (static_cast<float>(minute) / 60.0f);

        // Dawn window: from 30 min before calculated sunrise until noon
        float dawn_window_start = day_info.sunrise_hour_local - 0.5f;
        bool in_dawn_window = (current_time >= dawn_window_start && current_time < 12.0f);

        if (is_gpio_wakeup) {
            // GPIO outside the dawn window is dusk/night noise -> go back to sleep
            return in_dawn_window ? WakeType::DAWN_GPIO : WakeType::SPURIOUS_TIMER;
        }
        if (in_dawn_window && current_ma >= config_.dawn_current_threshold_ma) {
            return WakeType::DAWN_TIMER;
        }
        if (hour == config_.calibration_wake_hour) {
            return WakeType::CALIBRATION_TIMER;
        }
        return WakeType::SPURIOUS_TIMER;
    }

    // No time sync: fall back to current-only heuristic
    if (is_gpio_wakeup && current_ma >= config_.dawn_current_threshold_ma) {
        return WakeType::DAWN_GPIO;
    }
    if (current_ma >= config_.dawn_current_threshold_ma) {
        return WakeType::DAWN_TIMER;
    }
    return WakeType::SPURIOUS_TIMER;
}
```

Similarly update `calculate_night_sleep_time_us()` to use `decompose()` internally.

---

#### [MODIFY] `solar_sensor.cpp` — Eliminate boilerplate

A private helper added to `SolarSensor`:

```cpp
// solar_sensor.hpp (private):
std::optional<time_t> get_synced_time() const;

// solar_sensor.cpp:
std::optional<time_t> SolarSensor::get_synced_time() const
{
    if (!time_manager_.is_synchronized()) {
        return std::nullopt;
    }
    return static_cast<time_t>(time_manager_.get_timestamp_sec());
}
```

The three boilerplate blocks collapse to single lines:

```diff
-    bool is_synced = time_manager_.is_synchronized();
-    uint8_t hour = 0; uint8_t minute = 0; uint16_t day_of_year = 81;
-    if (is_synced) {
-        time_t now = time_manager_.get_timestamp_sec();
-        struct tm timeinfo{};
-        localtime_r(&now, &timeinfo);
-        hour   = static_cast<uint8_t>(timeinfo.tm_hour);
-        minute = static_cast<uint8_t>(timeinfo.tm_min);
-        day_of_year = static_cast<uint16_t>(timeinfo.tm_yday + 1);
-    }
-    process_ina_samples(is_synced, hour, minute, day_of_year);
+    process_ina_samples(get_synced_time());
```

`process_ina_samples` signature becomes:
```diff
-bool SolarSensor::process_ina_samples(bool is_synced, uint8_t hour, uint8_t minute, uint16_t day_of_year)
+bool SolarSensor::process_ina_samples(std::optional<time_t> unix_time)
```

And inside it:
```diff
-    if (day_night_controller_.should_enter_night_mode(
-            sample.isc_current_ma, is_synced, hour, minute, day_of_year)) {
+    if (day_night_controller_.should_enter_night_mode(sample.isc_current_ma, unix_time)) {
```

Same simplification for `enter_deep_sleep()` and `evaluate_boot_mode()`.

---

#### [MODIFY] `test_day_night_controller.cpp`

All existing tests need updating to pass a `std::optional<time_t>` instead of decomposed fields.

```diff
-EXPECT_FALSE(sut_->should_enter_night_mode(0, false, 12, 0, 81));
+EXPECT_FALSE(sut_->should_enter_night_mode(0, std::nullopt));

-EXPECT_FALSE(sut_->should_enter_night_mode(0, true, 12, 0, 81));
+// 12:00 PM UTC on Day 81 (March 22) => construct a time_t for that
+time_t t_noon_day81 = make_test_time(81, 12, 0); // helper
+EXPECT_FALSE(sut_->should_enter_night_mode(0, t_noon_day81));
```

A small test helper `make_test_time(day_of_year, hour, minute)` will be added to the test file to construct deterministic `time_t` values (UTC epoch-based), avoiding any system TZ dependency in the tests.

New tests for the `classify_wake()` bug fix:

```cpp
TEST_F(DayNightControllerTest, ClassifyWakeGpioAtDuskIsTreatedAsSpurious)
{
    // 17:36 local time, sunrise ~06:00, gpio wakeup -> SPURIOUS (not dawn window)
    time_t t_dusk = make_test_time(81, 17, 36);
    WakeType type = sut_->classify_wake(true, 0, t_dusk);
    EXPECT_EQ(type, WakeType::SPURIOUS_TIMER);
}

TEST_F(DayNightControllerTest, ClassifyWakeGpioAtDawnIsAccepted)
{
    // 06:10 local time, sunrise ~06:00, gpio wakeup -> DAWN_GPIO
    time_t t_dawn = make_test_time(81, 6, 10);
    WakeType type = sut_->classify_wake(true, 10, t_dawn);
    EXPECT_EQ(type, WakeType::DAWN_GPIO);
}
```

---

## Summary of Touched Files

| File                                                                     | Change                                                                                                                         |
| ------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------ |
| `main/include/ina_sensor_types.hpp`                                      | Replace `DEFAULT_DAWN_WAKEUP_ALERT_LIMIT` → `DEFAULT_DAWN_ALERT_CURRENT_MA` + add `dawn_alert_current_ma` to `InaSensorConfig` |
| `main/include/interfaces/i_ina_sensor_task.hpp`                          | Update docs reference                                                                                                          |
| `main/src/ina_sensor_task.cpp`                                           | Compute raw alert limit from mA + r_shunt at sleep time                                                                        |
| `main/include/day_night_controller.hpp`                                  | New signatures + `LocalTime` private struct                                                                                    |
| `main/src/day_night_controller.cpp`                                      | New signatures + `decompose()` helper + bug fix in `classify_wake()`                                                           |
| `main/include/solar_sensor.hpp`                                          | Add private `get_synced_time()` + update `process_ina_samples` signature                                                       |
| `main/src/solar_sensor.cpp`                                              | Remove 3× `localtime_r` blocks; simplify 3 call sites                                                                          |
| `host_test/test_day_night_controller/main/test_day_night_controller.cpp` | Update all tests + add `make_test_time()` helper + new `classify_wake` tests                                                   |
| `host_test/test_ina_sensor_task/main/test_ina_sensor_task.cpp`           | Update `prepare_for_sleep` test expectation                                                                                    |

---

## Verification Plan

### Automated Tests

```bash
# DayNightController unit tests
cd host_test/test_day_night_controller
idf.py build && ./build/test_day_night_controller.elf

# InaSensorTask unit tests
cd host_test/test_ina_sensor_task
idf.py build && ./build/test_ina_sensor_task.elf

# Full host test suite
cd host_test
ctest --output-on-failure

# ESP32 target build
cd test_apps/test_build
idf.py build
```

### Manual Verification

After the next field deployment:
- `Entering deep sleep for X min` log should appear at dusk **once** and the node should stay in sleep until ~06:00.
- No more alternating `Night: YES / Night: NO` in the hub logs at sunset.
- The log `Evaluated boot mode: DAWN_GPIO` should only appear in the early morning window.
