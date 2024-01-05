/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
 *
 * libcamera_hello.cpp - libcamera "hello world" app.
 */

#include <chrono>

#include "core/libcamera_app.hpp"
#include "core/options.hpp"

#include <iostream>
#include <unistd.h>
#include <pigpio.h>
#include "rotary_encoder.hpp"
#include <thread>

using namespace std::placeholders;

// The main event loop for the application.
static LibcameraApp app;
static void callback(int way)
{
   static int pos = 0;

   pos += way;

   std::cout << "pos=" << pos << std::endl;
   	if(way > 0) {
		app.nextShader();
   	}else {
		app.prevShader();
   }
}


static void event_loop(LibcameraApp &app)
{
	Options const *options = app.GetOptions();

	app.OpenCamera();
	app.ConfigureViewfinder();
	app.StartCamera();

	auto start_time = std::chrono::high_resolution_clock::now();

	for (unsigned int count = 0; ; count++)
	{
		LibcameraApp::Msg msg = app.Wait();
		if (msg.type == LibcameraApp::MsgType::Timeout)
		{
			LOG_ERROR("ERROR: Device timeout detected, attempting a restart!!!");
			app.StopCamera();
			app.StartCamera();
			continue;
		}
		if (msg.type == LibcameraApp::MsgType::Quit)
			return;
		else if (msg.type != LibcameraApp::MsgType::RequestComplete)
			throw std::runtime_error("unrecognised message!");

		LOG(2, "Viewfinder frame " << count);
		auto now = std::chrono::high_resolution_clock::now();
		if (options->timeout && (now - start_time) > options->timeout.value)
			return;

		CompletedRequestPtr &completed_request = std::get<CompletedRequestPtr>(msg.payload);
		app.ShowPreview(completed_request, app.ViewfinderStream());
	}
}

void gpio() {
	std::cout << "Starting GPIO Thread" << std::endl;

	if (gpioInitialise() < 0) return;
	std::cout << "GPIO Thread init" << std::endl;
   	re_decoder dec(7, 8, callback);
 
   sleep(15000);
   dec.re_cancel();
   gpioTerminate();
	std::cout << "Encoder finished" << std::endl;
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

int main(int argc, char *argv[])
{
	std::thread first (gpio);
	std::thread second (camera, argc, argv);
	first.join();
	second.join();
}
