/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
 *
 * preview.hpp - preview window interface
 */

#pragma once

#include <functional>
#include <iostream>
#include <string>

#include <libcamera/base/span.h>

#include "core/stream_info.hpp"

struct Options;

class Preview
{
public:
	typedef std::function<void(int fd)> DoneCallback;

	Preview(Options const *options) : options_(options) {}
	virtual ~Preview() {}
	// This is where the application sets the callback it gets whenever the viewfinder
	// is no longer displaying the buffer and it can be safely recycled.
	void SetDoneCallback(DoneCallback callback) { done_callback_ = callback; }
	void SetTextDrawCallback(std::function<void()> callback) { textDrawCallback = callback; }
	
	std::function<void()> textDrawCallback;
	virtual void SetInfoText(const std::string &text) {}
	// Display the buffer. You get given the fd back in the BufferDoneCallback
	// once its available for re-use.
	virtual void Show(int fd, libcamera::Span<uint8_t> span, StreamInfo const &info) = 0;
	// Reset the preview window, clearing the current buffers and being ready to
	// show new ones.
	virtual void Reset() = 0;
	// Check if preview window has been shut down.
	virtual bool Quit() { return false; }
	// Return the maximum image size allowed.
	virtual void MaxImageSize(unsigned int &w, unsigned int &h) const = 0;
	virtual void cycleShader(int amount) {

	}
	virtual void swapOriginalAndActiveShader() {

	}
	virtual void glRenderText(std::string = "", float x = 0, float y = 0, float scale = 1, float r = 255, float g = 255, float b = 255, float opacity = 1) {}
	virtual void setShaderValues(float a, float b, float c) {}

protected:
	DoneCallback done_callback_;
	Options const *options_;
};

Preview *make_preview(Options const *options);

