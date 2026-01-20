// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#include "can_writer.hpp"

#include <glog/logging.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

namespace vep {

SocketCANWriter::SocketCANWriter() = default;

SocketCANWriter::~SocketCANWriter() {
    close();
}

bool SocketCANWriter::open(const std::string& interface) {
    if (socket_fd_ >= 0) {
        LOG(WARNING) << "SocketCANWriter already open on " << interface_name_;
        return false;
    }

    // Create raw CAN socket
    socket_fd_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (socket_fd_ < 0) {
        LOG(ERROR) << "Failed to create CAN socket: " << strerror(errno);
        return false;
    }

    // Get interface index
    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    std::strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    if (ioctl(socket_fd_, SIOCGIFINDEX, &ifr) < 0) {
        LOG(ERROR) << "Failed to get interface index for " << interface
                   << ": " << strerror(errno);
        ::close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    // Bind socket to CAN interface
    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(socket_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG(ERROR) << "Failed to bind CAN socket to " << interface
                   << ": " << strerror(errno);
        ::close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    interface_name_ = interface;
    frames_sent_ = 0;
    send_errors_ = 0;

    LOG(INFO) << "SocketCANWriter opened on " << interface;
    return true;
}

void SocketCANWriter::close() {
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        LOG(INFO) << "SocketCANWriter closed: " << frames_sent_ << " frames sent, "
                  << send_errors_ << " errors on " << interface_name_;
        socket_fd_ = -1;
    }
}

bool SocketCANWriter::is_open() const {
    return socket_fd_ >= 0;
}

bool SocketCANWriter::write(uint32_t can_id, const std::vector<uint8_t>& data) {
    return write(can_id, data.data(), static_cast<uint8_t>(std::min(data.size(), size_t(8))));
}

bool SocketCANWriter::write(uint32_t can_id, const uint8_t* data, uint8_t dlc) {
    if (socket_fd_ < 0) {
        LOG(ERROR) << "SocketCANWriter not open";
        return false;
    }

    if (dlc > 8) {
        dlc = 8;  // Classic CAN max
    }

    struct can_frame frame;
    std::memset(&frame, 0, sizeof(frame));

    // Set CAN ID with extended frame flag if needed
    if (can_id > 0x7FF) {
        frame.can_id = (can_id & CAN_EFF_MASK) | CAN_EFF_FLAG;
    } else {
        frame.can_id = can_id & CAN_SFF_MASK;
    }

    frame.can_dlc = dlc;
    if (data != nullptr && dlc > 0) {
        std::memcpy(frame.data, data, dlc);
    }

    ssize_t nbytes = ::write(socket_fd_, &frame, sizeof(struct can_frame));
    if (nbytes != sizeof(struct can_frame)) {
        LOG(ERROR) << "Failed to write CAN frame: " << strerror(errno);
        send_errors_++;
        return false;
    }

    frames_sent_++;
    VLOG(2) << "Sent CAN frame ID=0x" << std::hex << can_id
            << " DLC=" << std::dec << static_cast<int>(dlc);

    return true;
}

}  // namespace vep
