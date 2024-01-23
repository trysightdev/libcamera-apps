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
make install  # install libs & headers
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
meson setup build -Denable_libav=false -Denable_drm=false -Denable_egl=true -Denable_qt=false -Denable_opencv=false -Denable_tflite=false
meson compile -C build -j4
```

Run
-----
```bash
cd build/apps
sudo xinit ./libcamera-hello -t 0 -f
```
