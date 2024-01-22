/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
 *
 * rpicam_hello.cpp - libcamera "hello world" app.
 */

#include <chrono>

#include "core/rpicam_app.hpp"
#include "core/options.hpp"

#include <iostream>
#include <pigpio.h>
#include "rotary_encoder.hpp"
#include <thread>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <pigpiod_if2.h>

#include "RED.hpp"
#include <cmath>


const int ENCODER1_A = 26;
const int ENCODER1_B = 19;
const int ENCODER1_SW = 13;

const int ENCODER2_A = 10;
const int ENCODER2_B = 22;
const int ENCODER2_SW = 27;

using namespace std::placeholders;

// The main event loop for the application.
static RPiCamApp app;
static float maxZoom = 0.25;
static float zoom = 1.0;

static bool started = false;
static bool running = true;
static libcamera::Rectangle scalerCropMaximum;
static libcamera::Rectangle scalerCrop;
static int pi;

//auto lastAutofocusTextDraw = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>(std::chrono::milliseconds(0));
//auto lastZoomTextDraw = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>(std::chrono::milliseconds(0));

auto lastAutofocusTextDraw = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
auto lastZoomTextDraw = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());

static bool autofocusLocked = false;
static void toggleAutofocus() {
	libcamera::ControlList controls;

	if(autofocusLocked) {
		controls.set(libcamera::controls::AfMode, libcamera::controls::AfModeEnum::AfModeContinuous);
		autofocusLocked = false;
	} else {
		controls.set(libcamera::controls::AfMode, libcamera::controls::AfModeEnum::AfModeManual);
		autofocusLocked = true;
	}
 
	app.SetControls(controls);
	lastAutofocusTextDraw = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
}

static void setZoom() {
	const int DENOMINATOR = 100;
	libcamera::Rectangle scaledRectangle = scalerCropMaximum.scaledBy(libcamera::Size(zoom*zoom*DENOMINATOR, zoom*zoom*DENOMINATOR), libcamera::Size(DENOMINATOR, DENOMINATOR));

	libcamera::Point maxCenter = scalerCropMaximum.center();
	libcamera::Point zoomCenter = scaledRectangle.center();
	libcamera::Point centerDiff = libcamera::Point(maxCenter.x - zoomCenter.x, maxCenter.y - zoomCenter.y);
	scaledRectangle.translateBy(centerDiff);

	libcamera::ControlList controls;
	controls.set(libcamera::controls::ScalerCrop, scaledRectangle);
 
	app.SetControls(controls);
	lastZoomTextDraw = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
}


static void shaderRotaryCallback(int newPos) {
	if(!started) return;
   	static int pos = 0;
   	int direction = pos - newPos;

   	if(direction > 0) {
		app.nextShader();
   	} else {
		app.prevShader();
   	}
	pos = newPos;
}

static void zoomRotaryCallback(int newPos) {
	if(!started) return;
   	static int pos = 0;
   	int direction = pos - newPos;
   
   	if(direction > 0) {
		zoom += 0.01;
   	} else {
		zoom -= 0.01;
    }

	if(zoom < maxZoom) zoom = maxZoom;
	if(zoom > 1) zoom = 1;
	
	setZoom();

	pos = newPos;
}

static long getTimeDiff(std::chrono::time_point<std::chrono::_V2::system_clock> timePoint) {
	auto diff = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()) - timePoint;
	return std::chrono::duration_cast<std::chrono::milliseconds>(diff).count();
}

static float getZoomLevel() {
	return (1 - ((zoom - maxZoom) / (1 - maxZoom)));
}

static float lerp(float a, float b, float t) {
	return (1-t) * a + t * b;
}

static int drawCounter = 0;
static float green[3] = {0.2, 1, 0.2};
static float red[3] = {1, 0.2, 0.2};
static void onDraw() {
	const int shadowXOffset = 22;
	const int shadowYOffset = 13;

	float shadowR = 0;
	float shadowG = 0;
	float shadowB = 0;

	float textR = 1;
	float textG = 1;
	float textB = 1;
	if(getTimeDiff(lastZoomTextDraw) < 2000) {
		app.drawText("Zoom", 1896+shadowXOffset, 2200+shadowYOffset, 1, shadowR,shadowG,shadowB);
		app.drawText("Zoom", 1896, 2200, 1, textR,textG,textB);

		int x = 1920;
		//keep the number centered
		if(getZoomLevel() < 1) {
			x += 95;
		}else if(getZoomLevel() < 0.1) {
			x += 40;
		}

		app.drawText(std::to_string((int)(getZoomLevel()*100)) + std::string("%"), x+shadowXOffset, 2450+shadowYOffset, 1, shadowR,shadowG,shadowB);
		app.drawText(std::to_string((int)(getZoomLevel()*100)) + std::string("%"), x, 2450, 1, lerp(red[0], green[0], getZoomLevel()), lerp(red[1], green[1], getZoomLevel()), lerp(red[2], green[2], getZoomLevel()));
	}

	if(getTimeDiff(lastAutofocusTextDraw) < 2000) {
		//app.drawText(std::string("Autofocus ") + std::string(autofocusLocked ? "Disabled" : "Enabled"), 90+shadowXOffset, 350+shadowYOffset, 1, shadowR,shadowG,shadowB);
		//app.drawText(std::string("Autofocus ") + std::string(autofocusLocked ? "Disabled" : "Enabled"), 90, 350, 1, textR,textG,textB);

		app.drawText("Autofocus ", 90+shadowXOffset, 350+shadowYOffset, 1, shadowR,shadowG,shadowB);
		app.drawText("Autofocus ", 90, 350, 1, textR,textG,textB);

		std::string autofocusText = autofocusLocked ? "Disabled" : "Enabled";
		float* color = autofocusLocked ? red : green;
		app.drawText(autofocusText, 1300+shadowXOffset, 350+shadowYOffset, 1, shadowR,shadowG,shadowB);
		app.drawText(autofocusText, 1300, 350, 1, color[0],color[1],color[2]);
	}

	drawCounter++;
}

static void event_loop(RPiCamApp &app) {
	Options const *options = app.GetOptions();

	app.OpenCamera();
	app.ConfigureViewfinder();
	app.StartCamera();

	app.SetTextDrawCallback(onDraw);
	libcamera::ControlList properties = app.GetProperties();
	scalerCropMaximum = *properties.get(libcamera::properties::ScalerCropMaximum);

	started = true;
	auto start_time = std::chrono::high_resolution_clock::now();

	for (unsigned int count = 0; ; count++)
	{
		RPiCamApp::Msg msg = app.Wait();
		if (msg.type == RPiCamApp::MsgType::Timeout)
		{
			LOG_ERROR("ERROR: Device timeout detected, attempting a restart!!!");
			app.StopCamera();
			app.StartCamera();
			continue;
		}
		if (msg.type == RPiCamApp::MsgType::Quit)
			return;
		else if (msg.type != RPiCamApp::MsgType::RequestComplete)
			throw std::runtime_error("unrecognised message!");

		LOG(2, "Viewfinder frame " << count);
		auto now = std::chrono::high_resolution_clock::now();
		if (options->timeout && (now - start_time) > options->timeout.value)
			return;

		CompletedRequestPtr &completed_request = std::get<CompletedRequestPtr>(msg.payload);
		app.ShowPreview(completed_request, app.ViewfinderStream());
	}
}


void gpioCallback(int a, int b, RED_CB_t callback) {
   	if (pi >= 0) {
   		RED_t *renc;
      	renc = RED(pi, a, b, RED_MODE_DETENT, callback);
      	RED_set_glitch_filter(renc, 1000);
      	while(running) sleep(1);

      	RED_cancel(renc);
   }
}

void buttonCallbacks() {
   	set_pull_up_down(pi, ENCODER1_SW, PI_PUD_UP);
   	set_pull_up_down(pi, ENCODER2_SW, PI_PUD_UP);

	set_glitch_filter(pi, ENCODER1_SW, 1000);
	set_glitch_filter(pi, ENCODER2_SW, 1000);

	callback(pi, ENCODER1_SW, RISING_EDGE, [](int pi, unsigned gpio, unsigned level, uint32_t tick){
		std::cout << "Button Up 1" << std::endl;
	});

	callback(pi, ENCODER2_SW, RISING_EDGE, [](int pi, unsigned gpio, unsigned level, uint32_t tick){
		std::cout << "Button Up 2" << std::endl;
	});

	
	callback(pi, ENCODER1_SW, FALLING_EDGE, [](int pi, unsigned gpio, unsigned level, uint32_t tick){
		std::cout << "Button Down 1" << std::endl;
		app.swapOriginalAndActiveShader();
	});

	callback(pi, ENCODER2_SW, FALLING_EDGE, [](int pi, unsigned gpio, unsigned level, uint32_t tick){
		std::cout << "Button Down 2" << std::endl;
		/*
		zoom = 1;
		setZoom();
		*/

		toggleAutofocus();
	});
}

int camera(int argc, char *argv[]) {
	try
	{
		

		Options *options = app.GetOptions();
		if (options->Parse(argc, argv))
		{
			if (options->verbose >= 2)
				options->Print();


			event_loop(app);
		}
	}
	catch (std::exception const &e)
	{
		LOG_ERROR("ERROR: *** " << e.what() << " ***");
		return -1;
	}
	return 0;
}

char *optHost   = NULL;
char *optPort   = NULL;
int main(int argc, char *argv[]) {
	pi = pigpio_start(NULL, NULL); /* Connect to Pi. */
	std::cout << "Starting GPIO | PI: " << pi << std::endl;

	std::thread cameraThread (camera, argc, argv);
	std::thread gpioShaderThread (gpioCallback, ENCODER1_A, ENCODER1_B, shaderRotaryCallback);
	std::thread gpioZoomThread (gpioCallback, ENCODER2_A, ENCODER2_B, zoomRotaryCallback);
	std::thread buttonsThread (buttonCallbacks);
	
	cameraThread.join();
	running = false;

	gpioShaderThread.join();
	gpioZoomThread.join();
	pigpio_stop(pi);

	buttonsThread.join();
}
