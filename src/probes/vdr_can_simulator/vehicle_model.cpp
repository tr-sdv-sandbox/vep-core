// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#include "vehicle_model.hpp"
#include <algorithm>
#include <cmath>

namespace simulator {

VehicleModel::VehicleModel()
    : scenario_start_(std::chrono::steady_clock::now()),
      rng_(std::random_device{}()) {
    update_signals();
}

void VehicleModel::set_scenario(Scenario scenario) {
    scenario_ = scenario;
    scenario_start_ = std::chrono::steady_clock::now();

    switch (scenario) {
        case Scenario::PARKED:
            target_speed_ = 0;
            gear_ = "P";
            break;
        case Scenario::CITY_DRIVING:
            target_speed_ = 50;
            gear_ = "D";
            break;
        case Scenario::HIGHWAY_DRIVING:
            target_speed_ = 110;
            gear_ = "D";
            break;
        case Scenario::AGGRESSIVE_DRIVING:
            target_speed_ = 140;
            gear_ = "D";
            break;
        case Scenario::CHARGING:
            target_speed_ = 0;
            gear_ = "P";
            break;
    }
}

void VehicleModel::update(std::chrono::milliseconds dt) {
    double dt_sec = dt.count() / 1000.0;

    auto elapsed = std::chrono::steady_clock::now() - scenario_start_;
    auto elapsed_sec = std::chrono::duration<double>(elapsed).count();

    // Scenario-specific behavior
    switch (scenario_) {
        case Scenario::PARKED:
            target_speed_ = 0;
            accel_pedal_ = 0;
            brake_pressed_ = false;
            // Occasional door/trunk activity
            door_fl_open_ = (std::fmod(elapsed_sec, 30.0) < 2.0);
            break;

        case Scenario::CITY_DRIVING:
            // Simulate stop-and-go traffic
            if (std::fmod(elapsed_sec, 20.0) < 5.0) {
                target_speed_ = 0;  // Red light
                brake_pressed_ = speed_ > 5;
                accel_pedal_ = 0;
            } else if (std::fmod(elapsed_sec, 20.0) < 8.0) {
                target_speed_ = 30;  // Accelerating
                brake_pressed_ = false;
                accel_pedal_ = 40;
            } else {
                target_speed_ = 50;  // Cruising
                brake_pressed_ = false;
                accel_pedal_ = 20;
            }
            // Turn signals
            left_turn_ = (std::fmod(elapsed_sec, 60.0) > 25.0 && std::fmod(elapsed_sec, 60.0) < 30.0);
            right_turn_ = (std::fmod(elapsed_sec, 60.0) > 45.0 && std::fmod(elapsed_sec, 60.0) < 50.0);
            steering_angle_ = 15.0 * std::sin(elapsed_sec * 0.3);
            break;

        case Scenario::HIGHWAY_DRIVING:
            target_speed_ = 110 + 10 * std::sin(elapsed_sec * 0.1);  // Slight variation
            brake_pressed_ = false;
            accel_pedal_ = 30;
            steering_angle_ = 3.0 * std::sin(elapsed_sec * 0.2);  // Lane keeping
            left_turn_ = false;
            right_turn_ = false;
            break;

        case Scenario::AGGRESSIVE_DRIVING:
            // Rapid acceleration/deceleration
            if (std::fmod(elapsed_sec, 15.0) < 5.0) {
                target_speed_ = 140;
                accel_pedal_ = 95;
                brake_pressed_ = false;
            } else if (std::fmod(elapsed_sec, 15.0) < 8.0) {
                target_speed_ = 60;
                accel_pedal_ = 0;
                brake_pressed_ = true;
            } else {
                target_speed_ = 100;
                accel_pedal_ = 60;
                brake_pressed_ = false;
            }
            steering_angle_ = 45.0 * std::sin(elapsed_sec * 0.5);  // Sharp turns
            break;

        case Scenario::CHARGING:
            target_speed_ = 0;
            speed_ = 0;
            gear_ = "P";
            // Simulate charging
            if (soc_ < 95.0) {
                soc_ += 0.1 * dt_sec;  // ~6%/min fast charging
                battery_current_ = -150.0;  // Charging current (negative = charging)
            } else {
                battery_current_ = -5.0;  // Trickle
            }
            break;
    }

    // Physics simulation
    double speed_diff = target_speed_ - speed_;
    double max_accel = 3.0;  // m/s² normal
    double max_decel = 8.0;  // m/s² braking

    if (scenario_ == Scenario::AGGRESSIVE_DRIVING) {
        max_accel = 6.0;
        max_decel = 12.0;
    }

    if (speed_diff > 0) {
        acceleration_ = std::min(speed_diff * 0.5, max_accel);
    } else {
        acceleration_ = std::max(speed_diff * 0.5, -max_decel);
    }

    // Update speed (km/h, acceleration in m/s²)
    speed_ += acceleration_ * dt_sec * 3.6;
    speed_ = std::max(0.0, std::min(speed_, 200.0));

    // Motor simulation
    if (speed_ > 0) {
        motor_rpm_ = speed_ * 50;  // Simplified: RPM ~ speed * 50
        motor_torque_ = acceleration_ * 100 + speed_ * 0.5;  // Simplified torque model
    } else {
        motor_rpm_ = 0;
        motor_torque_ = 0;
    }

    // Battery simulation (not charging scenario)
    if (scenario_ != Scenario::CHARGING) {
        double power_kw = std::abs(motor_torque_ * motor_rpm_ * 0.0001047);  // P = T * ω
        battery_current_ = power_kw * 1000 / battery_voltage_;
        soc_ -= power_kw * dt_sec / 3600.0 / 75.0 * 100;  // 75 kWh battery
        soc_ = std::max(0.0, std::min(100.0, soc_));
    }

    // Battery voltage varies with SOC
    battery_voltage_ = 350 + soc_ * 0.5;

    update_signals();
}

double VehicleModel::add_noise(double value, double noise_percent) {
    return value * (1.0 + noise_dist_(rng_) * noise_percent / 100.0);
}

void VehicleModel::update_signals() {
    // Map to Tesla Model 3 DBC signal names
    signals_["DI_vehicleSpeed"] = add_noise(std::abs(speed_), 0.5);
    signals_["DI_accelPedalPos"] = accel_pedal_;
    signals_["DI_brakePedalState"] = brake_pressed_ ? 1.0 : 0.0;
    signals_["DI_motorRPM"] = add_noise(motor_rpm_, 1.0);
    signals_["DI_torqueActual"] = add_noise(motor_torque_, 2.0);

    // Gear: DI_GEAR_P=1, DI_GEAR_R=2, DI_GEAR_N=3, DI_GEAR_D=4
    if (gear_ == "P") signals_["DI_gear"] = 1;
    else if (gear_ == "R") signals_["DI_gear"] = 2;
    else if (gear_ == "N") signals_["DI_gear"] = 3;
    else if (gear_ == "D") signals_["DI_gear"] = 4;
    else signals_["DI_gear"] = 0;

    signals_["SteeringAngle129"] = add_noise(steering_angle_, 0.5);

    // ESP/ABS - active during hard braking at speed
    bool abs_active = brake_pressed_ && speed_ > 30 && acceleration_ < -6.0;
    signals_["ESP_absBrakeEvent"] = abs_active ? 1.0 : 0.0;

    // Battery
    signals_["SOCUI292"] = soc_;
    signals_["BattVoltage132"] = add_noise(battery_voltage_, 0.2);
    signals_["SmoothBattCurrent132"] = add_noise(battery_current_, 1.0);

    // Turn signals
    signals_["VCLEFT_turnSignalStatus"] = left_turn_ ? 1.0 : 0.0;
    signals_["VCRIGHT_turnSignalStatus"] = right_turn_ ? 1.0 : 0.0;

    // Reverse light
    signals_["VCRIGHT_reverseLightStatus"] = (gear_ == "R") ? 1.0 : 0.0;

    // Doors - map to DBC values
    signals_["VCLEFT_frontDoorState"] = door_fl_open_ ? 1.0 : 0.0;  // DOOR_STATE_OPEN=1
    signals_["VCLEFT_rearDoorState"] = door_rl_open_ ? 1.0 : 0.0;
    signals_["VCRIGHT_trunkLatchStatus"] = trunk_open_ ? 3.0 : 0.0;  // LATCH_OPEN=3

    // HVAC
    signals_["HVAC_targetTempLeft"] = hvac_temp_left_;
    signals_["HVAC_targetTempRight"] = hvac_temp_right_;
}

}  // namespace simulator
