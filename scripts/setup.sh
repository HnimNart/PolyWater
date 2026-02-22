#!/bin/bash

set -e

# Installing deps
echo "Running \"sudo apt install -y libxinerama-dev libxcursor-dev libxi-dev\""
sudo apt install -y libxinerama-dev libxcursor-dev libxi-dev
sudo apt install -y libx11-dev libxcb1-dev libxcb-keysyms1-dev libxcursor-dev libxi-dev libxinerama-dev libxrandr-dev libxxf86vm-dev libtbb-dev libgl-dev


# Install vulkansdk
./scripts/install-vulkansdk.sh
