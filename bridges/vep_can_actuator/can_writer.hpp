// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vep {

/// @brief SocketCAN writer for sending CAN frames
///
/// Opens a raw CAN socket on a specified interface (e.g., vcan0, can0)
/// and provides methods to write CAN frames.
///
/// Usage:
///   SocketCANWriter writer;
///   if (writer.open("vcan0")) {
///       writer.write(0x123, {0x01, 0x02, 0x03});
///       writer.close();
///   }
class SocketCANWriter {
public:
    SocketCANWriter();
    ~SocketCANWriter();

    // Non-copyable
    SocketCANWriter(const SocketCANWriter&) = delete;
    SocketCANWriter& operator=(const SocketCANWriter&) = delete;

    /// @brief Open SocketCAN interface
    /// @param interface Interface name (e.g., "vcan0", "can0")
    /// @return true on success
    bool open(const std::string& interface);

    /// @brief Close the socket
    void close();

    /// @brief Check if socket is open
    bool is_open() const;

    /// @brief Write a CAN frame
    /// @param can_id CAN arbitration ID (standard or extended)
    /// @param data Frame data (up to 8 bytes for classic CAN)
    /// @return true on success
    bool write(uint32_t can_id, const std::vector<uint8_t>& data);

    /// @brief Write a CAN frame with explicit DLC
    /// @param can_id CAN arbitration ID
    /// @param data Pointer to frame data
    /// @param dlc Data length code (0-8 for classic CAN)
    /// @return true on success
    bool write(uint32_t can_id, const uint8_t* data, uint8_t dlc);

    /// @brief Get number of frames successfully sent
    uint64_t frames_sent() const { return frames_sent_; }

    /// @brief Get number of send errors
    uint64_t send_errors() const { return send_errors_; }

    /// @brief Get interface name
    const std::string& interface_name() const { return interface_name_; }

private:
    int socket_fd_ = -1;
    std::string interface_name_;
    uint64_t frames_sent_ = 0;
    uint64_t send_errors_ = 0;
};

}  // namespace vep
