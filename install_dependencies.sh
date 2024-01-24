#!/bin/bash

# Exit if any command fails
set -e

# Update package list
sudo apt update
# Install other dependencies
echo "Installing other dependencies..."
sudo apt install -y pigpio pigpiod
sudo apt install -y libcamera-dev libepoxy-dev libjpeg-dev libtiff5-dev libegl1-mesa-dev libpng-dev
sudo apt install -y cmake libboost-program-options-dev libdrm-dev libexif-dev
sudo apt install -y xserver-xorg xinit x11-xserver-utils

# Install FreeType
echo "Installing FreeType..."
wget http://download.savannah.gnu.org/releases/freetype/freetype-2.13.2.tar.gz
tar -xvzf freetype-2.13.2.tar.gz
cd freetype-2.13.2
mkdir build && cd build
cmake ..      # generates Makefile + deactivates HarfBuzz if not found
make          # compile libs
sudo make install  # install libs & headers
cd ../..



echo "Installation complete."
