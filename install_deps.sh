#!/bin/bash
# install_deps.sh - Install dependencies for Vehicle Edge Platform
#
# Usage: ./install_deps.sh
#
# This script installs all required system packages using apt-get.
# It will prompt for sudo password if needed.

set -e

echo "=================================================="
echo "Vehicle Edge Platform - Dependency Installer"
echo "=================================================="
echo ""

# Use sudo for privileged commands (prompts for password if needed)
SUDO=""
if [ "$EUID" -ne 0 ]; then
    SUDO="sudo"
    echo "This script requires sudo privileges to install packages."
    echo ""
fi

echo "Updating package lists..."
$SUDO apt-get update

echo ""
echo "Installing build tools..."
$SUDO apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    git

echo ""
echo "Installing CycloneDDS..."
$SUDO apt-get install -y \
    cyclonedds-dev \
    cyclonedds-tools \
    libcycloneddsidl0t64 \
    libddsc0t64

echo ""
echo "Installing Mosquitto MQTT client library..."
$SUDO apt-get install -y \
    libmosquitto-dev

echo ""
echo "Installing Google logging (glog)..."
$SUDO apt-get install -y \
    libgoogle-glog-dev

echo ""
echo "Installing yaml-cpp..."
$SUDO apt-get install -y \
    libyaml-cpp-dev

echo ""
echo "Installing nlohmann-json..."
$SUDO apt-get install -y \
    nlohmann-json3-dev

echo ""
echo "Installing Protobuf..."
$SUDO apt-get install -y \
    libprotobuf-dev \
    protobuf-compiler

echo ""
echo "Installing zstd compression library..."
$SUDO apt-get install -y \
    libzstd-dev

echo ""
echo "Installing Lua (for libvssdag)..."
$SUDO apt-get install -y \
    liblua5.4-dev \
    lua5.4

echo ""
echo "Installing Docker (for Mosquitto container)..."
if ! command -v docker &> /dev/null; then
    $SUDO apt-get install -y \
        docker.io \
        docker-compose
    $SUDO systemctl enable docker
    $SUDO systemctl start docker
    echo "Docker installed. You may need to log out and back in to use docker without sudo."
else
    echo "Docker already installed."
fi

echo ""
echo "=================================================="
echo "System dependencies installed successfully!"
echo "=================================================="
echo ""
echo "Next steps:"
echo ""
echo "1. Build libvss-types (if not already installed):"
echo "   cd ../libvss-types && cmake -B build && cmake --build build && sudo cmake --install build"
echo ""
echo "2. Build libvssdag (if not already installed):"
echo "   cd ../libvssdag && cmake -B build && cmake --build build && sudo cmake --install build"
echo ""
echo "3. Build this project:"
echo "   cmake -B build && cmake --build build"
echo ""
echo "4. Run tests to verify:"
echo "   cd build && ctest"
echo ""
echo "5. Run the framework (starts Mosquitto + KUKSA via Docker):"
echo "   ./run_framework.sh"
echo ""
