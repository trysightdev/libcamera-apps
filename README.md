# libcamera-apps - HDMI OpenGL Display
Tested on Raspberry Pi 4, 32 bit Bookworm OS Lite

Do not update to latest libcamera-apps, their main branch can be broken sometimes.
This project uses xserver and creates a xwindow with an eGL display using openGL.
OpenGL effects can be added in preview/egl_preview.cpp

Before setting up this program we need to install FreeType to render text to OpenGL

```bash
wget http://download.savannah.gnu.org/releases/freetype/freetype-2.13.2.tar.gz
tar -xvzf freetype-2.13.2.tar.gz

cd freetype2
mkdir build && cd build
cmake ..      # generates Makefile + deactivates HarfBuzz if not found
make          # compile libs
sudo make install  # install libs & headers
```

Setup
-----
```bash
sudp apt install pigpio pigpiod
sudo apt install -y python3-pip
sudo apt install -y libcamera-dev libepoxy-dev libjpeg-dev libtiff5-dev libegl1-mesa-dev libpng-dev
sudo apt install -y cmake libboost-program-options-dev libdrm-dev libexif-dev
sudo pip3 install ninja meson --break-system-packages
sudo apt install -y xserver-xorg xinit x11-xserver-utils
```

Build
-----
```bash
cd ~/
sudo apt install git
git clone https://github.com/trysightdev/libcamera-apps.git
cd libcamera-apps
meson setup build -Denable_libav=false -Denable_drm=false -Denable_egl=true -Denable_qt=false -Denable_opencv=false -Denable_tflite=false
meson compile -C build -j4
```

Run
-----
```bash
cd ~/libcamera-apps
sudo pigpiod
sudo xinit ./build/apps/rpicam-hello -t 0 -f
```
Use rpi-config to disable blanking and use splash screen.
Boot config.txt set hdmi 2 for the new monitor.

Splash Screen
-----
```
sudo apt -y install rpd-plym-splash
sudo mv ~/trysight_splash.png /usr/share/plymouth/themes/pix/splash.png
sudo update-initramfs -u
```
Install plymouth
Move the trysight_splash.png from this repo to the directory
Rebuild the boot image to use the updated splash.png

## Known Issues
* During demo RPI didn't load up, not sure why
