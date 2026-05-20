#!/bin/bash
set -e

# https://gazebosim.org/docs/harmonic/install_ubuntu/
apt-get install -y curl lsb-release gnupg

# Install gazebo harmonic (https://gazebosim.org/docs/harmonic/install/)
curl https://packages.osrfoundation.org/gazebo.gpg --output /usr/share/keyrings/pkgs-osrf-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/pkgs-osrf-archive-keyring.gpg] http://packages.osrfoundation.org/gazebo/ubuntu-stable $(lsb_release -cs) main" | sudo tee /etc/apt/sources.list.d/gazebo-stable.list > /dev/null
apt-get update
apt-get install -y gz-harmonic

## Install pairing ROS pairing libraries
apt-get install -y ros-jazzy-ros-gz

apt-get autoremove -y
apt-get clean
rm -rf /var/lib/apt/lists/*