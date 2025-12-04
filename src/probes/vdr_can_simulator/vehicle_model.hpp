// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>

namespace simulator {

/// Simulates vehicle state and generates realistic CAN signal values
class VehicleModel {
public:
    VehicleModel();

    /// Update the vehicle state (call at regular intervals)
    void update(std::chrono::milliseconds dt);

    /// Get current signal values (CAN signal names -> raw values)
    const std::unordered_map<std::string, double>& get_signals() const {
        return signals_;
    }

    /// Set driving scenario
    enum class Scenario {
        PARKED,
        CITY_DRIVING,
        HIGHWAY_DRIVING,
        AGGRESSIVE_DRIVING,
        CHARGING
    };
    void set_scenario(Scenario scenario);
    Scenario get_scenario() const { return scenario_; }

    // Direct state access for debugging
    double speed() const { return speed_; }
    double soc() const { return soc_; }
    double motor_rpm() const { return motor_rpm_; }
    std::string gear() const { return gear_; }

private:
    void update_signals();
    double add_noise(double value, double noise_percent);

    // Vehicle state
    double speed_ = 0.0;           // km/h
    double target_speed_ = 0.0;    // km/h
    double acceleration_ = 0.0;    // m/s²
    double soc_ = 85.0;            // %
    double battery_voltage_ = 380.0; // V
    double battery_current_ = 0.0; // A
    double motor_rpm_ = 0.0;
    double motor_torque_ = 0.0;    // Nm
    double steering_angle_ = 0.0;  // degrees
    double accel_pedal_ = 0.0;     // %
    bool brake_pressed_ = false;
    std::string gear_ = "P";
    bool left_turn_ = false;
    bool right_turn_ = false;
    double hvac_temp_left_ = 21.0;
    double hvac_temp_right_ = 21.0;
    bool door_fl_open_ = false;
    bool door_rl_open_ = false;
    bool trunk_open_ = false;

    Scenario scenario_ = Scenario::PARKED;
    std::chrono::steady_clock::time_point scenario_start_;

    // Signal output map (CAN signal names from DBC)
    std::unordered_map<std::string, double> signals_;

    // Random generator for noise
    std::mt19937 rng_;
    std::uniform_real_distribution<double> noise_dist_{-1.0, 1.0};
};

}  // namespace simulator
