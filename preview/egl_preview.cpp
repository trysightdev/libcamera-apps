/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
 *
 * egl_preview.cpp - X/EGL-based preview window.
 */

#include <map>
#include <string>

// Include libcamera stuff before X11, as X11 #defines both Status and None
// which upsets the libcamera headers.

#include "core/options.hpp"

#include "preview.hpp"

#include <libdrm/drm_fourcc.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
// We don't use Status below, so we could consider #undefining it here.
// We do use None, so if we had to #undefine it we could replace it by zero
// in what follows below.

#include <epoxy/egl.h>
#include <epoxy/gl.h>

#include <string>
#include <chrono>
#include <math.h>

#include <ft2build.h>
#include FT_FREETYPE_H 


static float contrastA = 0.7;
static float contrastB = 0.2;
static float contrastC = 0.2;
static float contrast = 1.0;

std::string SC_MEGASHADER = "#extension GL_OES_EGL_image_external : enable\n"
	"precision mediump float;\n" // Set the default precision to medium. 
    "uniform samplerExternalOES s;\n" // The contrast lookup table.
	"uniform float shaderIndex;\n"
	"uniform float u_ContrastA;\n"
	"uniform float u_ContrastB;\n"
	"uniform float u_ContrastC;\n"
    //"float u_ContrastA = 0.7;\n" // Subtracted Value
    //"float u_ContrastB = 0.2;\n" // Multiplied Value
    //"float u_ContrastC = 0.2;\n" // Added Value
    "uniform float u_Contrast;\n"
    "varying vec2 texcoord;\n" // Interpolated texture coordinate per fragment.
    "void main() {\n" //The entry point for our fragment shader.
    "   gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);\n"
	"	vec4 tempColor = texture2D(s, texcoord);\n" // Sample the texture value
	"	tempColor.rgb = ((tempColor.rgb - 0.5) * max(u_Contrast, 0.0)) + 0.5;\n"
	"   float grayScale = (tempColor.r * 0.299) + (tempColor.g * 0.587) + (tempColor.b *  0.114) ;\n"
    "   float binarized = smoothstep(u_ContrastA,u_ContrastB,grayScale);"
	""
	"	if(shaderIndex == 1.0) {"
    "		gl_FragColor = vec4(1.0-binarized,1.0-binarized,binarized,1);" // BLUE_ON_YELLOW
	"	} else if(shaderIndex == 2.0) {"
    "		gl_FragColor = vec4(binarized,binarized,1.0-binarized,1);" // YELLOW_ON_BLUE
	"	} else if(shaderIndex == 3.0) {"
    "		gl_FragColor = vec4(binarized,binarized,binarized,1);" // BLACK_ON_WHITE
	"	} else if(shaderIndex == 4.0) {"
    "		gl_FragColor = vec4(1.0-binarized,1.0-binarized,1.0-binarized,1);" // WHITE_ON_BLACK
	"	} else if(shaderIndex == 5.0) {"
    "		gl_FragColor = vec4(binarized,binarized,0.0,1);" // BLACK_ON_YELLOW
	"	} else if(shaderIndex == 6.0) {"
    "		gl_FragColor = vec4(1.0-binarized,1.0-binarized,0.0,1);" // YELLOW_ON_BLACK
	"	} else if(shaderIndex == 7.0) {"
    "		gl_FragColor = vec4(0.0,binarized,0.0,1);" // BLACK_ON_GREEN
	"	} else if(shaderIndex == 8.0) {"
    "		gl_FragColor = vec4(0.0,1.0-binarized,0.0,1);" // GREEN_ON_BLACK
    "	} else {"
	"		gl_FragColor.x = (tempColor.x - u_ContrastC) * 1.50 + u_ContrastC;"
    "		gl_FragColor.y = (tempColor.y - u_ContrastC) * 1.50 + u_ContrastC;" 
    "		gl_FragColor.z = (tempColor.z - u_ContrastC) * 1.50 + u_ContrastC;"
	"	}"
    "}"
    "";

const uint NUM_SHADERS = 9;

struct Vec2 {
	int x;
	int y;
	Vec2() : x(0), y(0) { }
	Vec2(int x, int y) : x(x), y(y) { }
};

struct Character {
    GLuint TextureID;  // ID handle of the glyph texture
    Vec2   Size;       // Size of glyph
    Vec2   Bearing;    // Offset from baseline to left/top of glyph
    int Advance;    // Offset to advance to next glyph
	Character() {}
	Character(GLuint textureId, Vec2 size, Vec2 bearing, FT_Pos advance) : TextureID(textureId), Size(size), Bearing(bearing), Advance(advance) {}
};

std::map<char, Character> Characters;

class EglPreview : public Preview
{
public:
	EglPreview(Options const *options);
	~EglPreview();
	virtual void SetInfoText(const std::string &text) override;
	// Display the buffer. You get given the fd back in the BufferDoneCallback
	// once its available for re-use.
	virtual void Show(int fd, libcamera::Span<uint8_t> span, StreamInfo const &info) override;
	// Reset the preview window, clearing the current buffers and being ready to
	// show new ones.
	virtual void Reset() override;
	// Check if the window manager has closed the preview.
	virtual bool Quit() override;
	// Return the maximum image size allowed.
	virtual void MaxImageSize(unsigned int &w, unsigned int &h) const override
	{
		w = max_image_width_;
		h = max_image_height_;
	}
	void cycleShader(int amount) override;
	void swapOriginalAndActiveShader() override;
	void glRenderText(std::string = "", float x = 0, float y = 0, float scale = 1, float r = 1, float g = 1, float b = 1, float opacity = 1) override;
	void setShaderValues(float a, float b, float c, float d);

private:
	struct Buffer
	{
		Buffer() : fd(-1) {}
		int fd;
		size_t size;
		StreamInfo info;
		GLuint texture;
	};
	void makeWindow(char const *name);
	void makeBuffer(int fd, size_t size, StreamInfo const &info, Buffer &buffer);
	::Display *display_;
	EGLDisplay egl_display_;
	Window window_;
	EGLContext egl_context_;
	EGLSurface egl_surface_;
	std::map<int, Buffer> buffers_; // map the DMABUF's fd to the Buffer
	int last_fd_;
	bool first_time_;
	Atom wm_delete_window_;
	// size of preview window
	int x_;
	int y_;
	int width_;
	int height_;
	unsigned int max_image_width_;
	unsigned int max_image_height_;
};


static int lastShaderIndex = 0;
static int shaderIndex = 0;

static GLint compile_shader(GLenum target, const char *source)
{
	GLuint s = glCreateShader(target);
	glShaderSource(s, 1, (const GLchar **)&source, NULL);
	glCompileShader(s);

	GLint ok;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);

	if (!ok)
	{
		GLchar *info;
		GLint size;

		glGetShaderiv(s, GL_INFO_LOG_LENGTH, &size);
		info = (GLchar *)malloc(size);

		glGetShaderInfoLog(s, size, NULL, info);
		throw std::runtime_error("failed to compile shader: " + std::string(info) + "\nsource:\n" +
								 std::string(source));
	}

	return s;
}

static GLint link_program(GLint vs, GLint fs)
{
	GLint prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);

	GLint ok;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		/* Some drivers return a size of 1 for an empty log.  This is the size
		 * of a log that contains only a terminating NUL character.
		 */
		GLint size;
		GLchar *info = NULL;
		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &size);
		if (size > 1)
		{
			info = (GLchar *)malloc(size);
			glGetProgramInfoLog(prog, size, NULL, info);
		}

		throw std::runtime_error("failed to link: " + std::string(info ? info : "<empty log>"));
	}

	return prog;
}



static GLint vs_s;

static GLint textShader;
static GLint textVertexShader;
static GLint textFragmentShader;

static GLuint VAO, VBO;
static GLuint textVAO, textVBO;

static GLuint testImage;
static GLuint testImage2;

static GLint prog;

// Followed this tutorial to add all of the text rendering stuff https://learnopengl.com/In-Practice/Text-Rendering
// Adapted it a bit to work with this 
static void loadFont() {
	FT_Library ft;
	if (FT_Init_FreeType(&ft)) {
		std::cout << "ERROR::FREETYPE: Could not init FreeType Library" << std::endl;
	}
	
	FT_Face face;
	if (FT_New_Face(ft, "Arial.ttf", 0, &face)) {
		std::cout << "ERROR::FREETYPE: Failed to load font" << std::endl;
	}

	FT_Set_Pixel_Sizes(face, 0, 256); 

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // disable byte-alignment restriction
  
	for (unsigned char c = 0; c < 128; c++) {
		// load character glyph 
		if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
			std::cout << "ERROR::FREETYTPE: Failed to load Glyph" << std::endl;
			continue;
		}
		// generate texture
		GLuint texture;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);

		glTexImage2D(
			GL_TEXTURE_2D,
			0,
			GL_RED,
			face->glyph->bitmap.width,
			face->glyph->bitmap.rows,
			0,
			GL_RED,
			GL_UNSIGNED_BYTE,
			face->glyph->bitmap.buffer
		);

		// set texture options
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		// now store character for later use
		Character character = {
			texture, 
			Vec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
			Vec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
			face->glyph->advance.x
		};
		Characters.insert(std::pair<char, Character>(c, character));
	}
}

static GLint shaderIndexLocation;
static GLint contrastALocation, contrastBLocation, contrastCLocation, contrastLocation;
static void gl_setup(int width, int height, int window_width, int window_height)
{
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  

	float w_factor = width / (float)window_width;
	float h_factor = height / (float)window_height;
	float max_dimension = std::max(w_factor, h_factor);
	w_factor /= max_dimension;
	h_factor /= max_dimension;
	char vs[256];
	
	snprintf(vs, sizeof(vs),
			 "attribute vec4 pos;\n"
			 "varying vec2 texcoord;\n"
			 "\n"
			 "void main() {\n"
			 "  gl_Position = pos;\n"
			 "  texcoord.x = pos.x / %f + 0.5;\n"
			 "  texcoord.y = 0.5 - pos.y / %f;\n"
			 "}\n",
			 2.0 * w_factor, 2.0 * h_factor);
	vs[sizeof(vs) - 1] = 0;
	vs_s = compile_shader(GL_VERTEX_SHADER, vs);
	std::cout << "SELECT SHADER " << shaderIndex << std::endl;
	const char *fs;
	//fs = shaders[shaderIndex].c_str();
	fs = SC_MEGASHADER.c_str();

	 
	GLint fs_s = compile_shader(GL_FRAGMENT_SHADER, fs);
	prog = link_program(vs_s, fs_s);

	glUseProgram(prog);

	
	contrastALocation = glGetUniformLocation(prog, "u_ContrastA");
	contrastBLocation = glGetUniformLocation(prog, "u_ContrastB");
	contrastCLocation = glGetUniformLocation(prog, "u_ContrastC");
	contrastLocation = glGetUniformLocation(prog, "u_Contrast");
	shaderIndexLocation = glGetUniformLocation(prog, "shaderIndex");
	glUniform1f(contrastALocation, contrastA);
	glUniform1f(contrastBLocation, contrastB);
	glUniform1f(contrastCLocation, contrastC);
	glUniform1f(contrastLocation, contrast);

	glGenVertexArrays(1, &VAO);

	glBindVertexArray(VAO);
	static const float verts[] = { -w_factor, -h_factor, 1, 1, w_factor, -h_factor, 1, 1, w_factor, h_factor, 1, 1, -w_factor, h_factor, 1, 1 };
	
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, verts);

	char textVertexShaderCode[256];
	snprintf(textVertexShaderCode, sizeof(vs),
		"attribute vec4 vertex;\n"
		"varying vec2 TexCoords;\n"

		"void main() {\n"
		"	vec4 newPos;"
		"	newPos = vec4(vertex.x / %d.0, vertex.y / %d.0, 0.0, 1.0);"
		"	newPos = vec4((newPos.xy - 1.0), 0.0, 1.0);\n"
		"	newPos.y *= -1.0;"
		"	gl_Position = newPos;\n"
		"  	TexCoords = vertex.wz;\n"
		"}\n", width, height);

	std::string textFragmentShaderCode = "#extension GL_OES_EGL_image_external : enable\n"
		"precision mediump float;\n"
		"varying vec2 TexCoords;\n"

		"uniform sampler2D text;\n"
		"uniform vec3 textColor;\n"
		"uniform float opacity;\n"

		"void main() {\n"    
		"	vec4 sampled = vec4(1.0, 1.0, 1.0, texture2D(text, TexCoords).r);\n"
		"	gl_FragColor = vec4(textColor, opacity) * sampled;\n"
		"}\n"
		"";
		
	GLint textVertexShader = compile_shader(GL_VERTEX_SHADER, textVertexShaderCode);
	GLint textFragmentShader = compile_shader(GL_FRAGMENT_SHADER, textFragmentShaderCode.c_str());
	textShader = link_program(textVertexShader, textFragmentShader);

	glUseProgram(textShader);
	glGenVertexArrays(1, &textVAO);
	loadFont();
}

void EglPreview::setShaderValues(float a, float b, float c, float d) {
	contrastA = a;
	contrastB = b;
	contrastC = c;
	contrast = d;
}

void EglPreview::glRenderText(std::string text, float x, float y, float scale, float r, float g, float b, float opacity) {
	glUseProgram(textShader);
	auto textLocation = glGetUniformLocation(textShader, "text");
	auto textColorLocation = glGetUniformLocation(textShader, "textColor");
	auto opacityLocation = glGetUniformLocation(textShader, "opacity");

	glUniform1f(opacityLocation, opacity);
	glUniform3f(textColorLocation, r, g, b);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(textVAO);

    // iterate through all characters
    std::string::const_iterator c;
    for (c = text.begin(); c != text.end(); c++)
    {
        Character ch = Characters[*c];

        float xpos = (x + ch.Bearing.x) * scale;
        float ypos = (y - ch.Bearing.y) * scale;

        float w = ch.Size.x * scale;
        float h = ch.Size.y * scale;

		const float verts[] = { xpos,ypos+h,1,0,  xpos+w,ypos+h,1,1,  xpos+w,ypos,0,1,  xpos,ypos,0,0 };

        // render glyph texture over quad
        glBindTexture(GL_TEXTURE_2D, ch.TextureID);
		// update content of VBO memory
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, verts);

		
        // render quad
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        // now advance cursors for next glyph (note that advance is number of 1/64 pixels)
		
        x += (ch.Advance >> 6) * scale; // bitshift by 6 to get value in pixels (2^6 = 64)
    }
	
}

EglPreview::EglPreview(Options const *options) : Preview(options), last_fd_(-1), first_time_(true)
{
	display_ = XOpenDisplay(NULL);
	if (!display_)
		throw std::runtime_error("Couldn't open X display");

	egl_display_ = eglGetDisplay(display_);
	if (!egl_display_)
		throw std::runtime_error("eglGetDisplay() failed");

	EGLint egl_major, egl_minor;

	if (!eglInitialize(egl_display_, &egl_major, &egl_minor))
		throw std::runtime_error("eglInitialize() failed");

	x_ = options_->preview_x;
	y_ = options_->preview_y;
	width_ = options_->preview_width;
	height_ = options_->preview_height;
	makeWindow("rpicam-app");
	// gl_setup() has to happen later, once we're sure we're in the display thread.
}

EglPreview::~EglPreview()
{
}

static void no_border(Display *display, Window window)
{
	static const unsigned MWM_HINTS_DECORATIONS = (1 << 1);
	static const int PROP_MOTIF_WM_HINTS_ELEMENTS = 5;

	typedef struct
	{
		unsigned long flags;
		unsigned long functions;
		unsigned long decorations;
		long inputMode;
		unsigned long status;
	} PropMotifWmHints;

	PropMotifWmHints motif_hints;
	Atom prop, proptype;
	unsigned long flags = 0;

	/* setup the property */
	motif_hints.flags = MWM_HINTS_DECORATIONS;
	motif_hints.decorations = flags;

	/* get the atom for the property */
	prop = XInternAtom(display, "_MOTIF_WM_HINTS", True);
	if (!prop)
	{
		/* something went wrong! */
		return;
	}

	/* not sure this is correct, seems to work, XA_WM_HINTS didn't work */
	proptype = prop;

	XChangeProperty(display, window, /* display, window */
					prop, proptype, /* property, type */
					32, /* format: 32-bit datums */
					PropModeReplace, /* mode */
					(unsigned char *)&motif_hints, /* data */
					PROP_MOTIF_WM_HINTS_ELEMENTS /* nelements */
	);
}

void EglPreview::makeWindow(char const *name)
{
	int screen_num = DefaultScreen(display_);
	XSetWindowAttributes attr;
	unsigned long mask;
	Window root = RootWindow(display_, screen_num);
	int screen_width = DisplayWidth(display_, screen_num);
	int screen_height = DisplayHeight(display_, screen_num);

	// Default behaviour here is to use a 1024x768 window.
	if (width_ == 0 || height_ == 0)
	{
		width_ = 1024;
		height_ = 768;
	}

	if (options_->fullscreen || x_ + width_ > screen_width || y_ + height_ > screen_height)
	{
		x_ = y_ = 0;
		width_ = DisplayWidth(display_, screen_num);
		height_ = DisplayHeight(display_, screen_num);
	}

	static const EGLint attribs[] =
		{
			EGL_RED_SIZE, 1,
			EGL_GREEN_SIZE, 1,
			EGL_BLUE_SIZE, 1,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
			EGL_NONE
		};
	EGLConfig config;
	EGLint num_configs;
	if (!eglChooseConfig(egl_display_, attribs, &config, 1, &num_configs))
		throw std::runtime_error("couldn't get an EGL visual config");

	EGLint vid;
	if (!eglGetConfigAttrib(egl_display_, config, EGL_NATIVE_VISUAL_ID, &vid))
		throw std::runtime_error("eglGetConfigAttrib() failed\n");

	XVisualInfo visTemplate = {};
	visTemplate.visualid = (VisualID)vid;
	int num_visuals;
	XVisualInfo *visinfo = XGetVisualInfo(display_, VisualIDMask, &visTemplate, &num_visuals);

	/* window attributes */
	attr.background_pixel = 0;
	attr.border_pixel = 0;
	attr.colormap = XCreateColormap(display_, root, visinfo->visual, AllocNone);
	attr.event_mask = StructureNotifyMask | ExposureMask | KeyPressMask;
	/* XXX this is a bad way to get a borderless window! */
	mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;

	window_ = XCreateWindow(display_, root, x_, y_, width_, height_, 0, visinfo->depth, InputOutput, visinfo->visual,
							mask, &attr);

	if (options_->fullscreen)
		no_border(display_, window_);

	/* set hints and properties */
	{
		XSizeHints sizehints;
		sizehints.x = x_;
		sizehints.y = y_;
		sizehints.width = width_;
		sizehints.height = height_;
		sizehints.flags = USSize | USPosition;
		XSetNormalHints(display_, window_, &sizehints);
		XSetStandardProperties(display_, window_, name, name, None, (char **)NULL, 0, &sizehints);
	}

	eglBindAPI(EGL_OPENGL_ES_API);

	static const EGLint ctx_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	egl_context_ = eglCreateContext(egl_display_, config, EGL_NO_CONTEXT, ctx_attribs);
	if (!egl_context_)
		throw std::runtime_error("eglCreateContext failed");

	XFree(visinfo);

	XMapWindow(display_, window_);

	// This stops the window manager from closing the window, so we get an event instead.
	wm_delete_window_ = XInternAtom(display_, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(display_, window_, &wm_delete_window_, 1);

	egl_surface_ = eglCreateWindowSurface(egl_display_, config, reinterpret_cast<EGLNativeWindowType>(window_), NULL);
	if (!egl_surface_)
		throw std::runtime_error("eglCreateWindowSurface failed");

	// We have to do eglMakeCurrent in the thread where it will run, but we must do it
	// here temporarily so as to get the maximum texture size.
	eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context_);
	int max_texture_size = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
	max_image_width_ = max_image_height_ = max_texture_size;
	// This "undoes" the previous eglMakeCurrent.
	eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

void EglPreview::cycleShader(int amount) {
	if(shaderIndex == 0 && amount < 0) {
		shaderIndex = NUM_SHADERS - 1;
	}else{
		shaderIndex = (shaderIndex+amount) % NUM_SHADERS;
	}
}

void EglPreview::swapOriginalAndActiveShader() {
	if(shaderIndex != 0) {
		lastShaderIndex = shaderIndex;
		shaderIndex = 0;
	} else {
		shaderIndex = lastShaderIndex;
	}
	
	//glUniform1f(shaderIndexLocation, shaderIndex);
}

static void get_colour_space_info(std::optional<libcamera::ColorSpace> const &cs, EGLint &encoding, EGLint &range)
{
	encoding = EGL_ITU_REC601_EXT;
	range = EGL_YUV_NARROW_RANGE_EXT;

	if (cs == libcamera::ColorSpace::Sycc)
		range = EGL_YUV_FULL_RANGE_EXT;
	else if (cs == libcamera::ColorSpace::Smpte170m)
		/* all good */;
	else if (cs == libcamera::ColorSpace::Rec709)
		encoding = EGL_ITU_REC709_EXT;
	else
		LOG(1, "EglPreview: unexpected colour space " << libcamera::ColorSpace::toString(cs));
}

void EglPreview::makeBuffer(int fd, size_t size, StreamInfo const &info, Buffer &buffer)
{
	if (first_time_)
	{
		// This stuff has to be delayed until we know we're in the thread doing the display.
		if (!eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_))
			throw std::runtime_error("eglMakeCurrent failed");
		gl_setup(info.width, info.height, width_, height_);
		first_time_ = false;
	}

	buffer.fd = fd;
	buffer.size = size;
	buffer.info = info;

	EGLint encoding, range;
	get_colour_space_info(info.colour_space, encoding, range);

	EGLint attribs[] = {
		EGL_WIDTH, static_cast<EGLint>(info.width),
		EGL_HEIGHT, static_cast<EGLint>(info.height),
		EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_YUV420,
		EGL_DMA_BUF_PLANE0_FD_EXT, fd,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
		EGL_DMA_BUF_PLANE0_PITCH_EXT, static_cast<EGLint>(info.stride),
		EGL_DMA_BUF_PLANE1_FD_EXT, fd,
		EGL_DMA_BUF_PLANE1_OFFSET_EXT, static_cast<EGLint>(info.stride * info.height),
		EGL_DMA_BUF_PLANE1_PITCH_EXT, static_cast<EGLint>(info.stride / 2),
		EGL_DMA_BUF_PLANE2_FD_EXT, fd,
		EGL_DMA_BUF_PLANE2_OFFSET_EXT, static_cast<EGLint>(info.stride * info.height + (info.stride / 2) * (info.height / 2)),
		EGL_DMA_BUF_PLANE2_PITCH_EXT, static_cast<EGLint>(info.stride / 2),
		EGL_YUV_COLOR_SPACE_HINT_EXT, encoding,
		EGL_SAMPLE_RANGE_HINT_EXT, range,
		EGL_NONE
	};

	EGLImage image = eglCreateImageKHR(egl_display_, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attribs);
	if (!image)
		throw std::runtime_error("failed to import fd " + std::to_string(fd));

	glGenTextures(1, &buffer.texture);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, buffer.texture);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, image);


	glGenTextures(1, &testImage);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, testImage);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, image);

	eglDestroyImageKHR(egl_display_, image);
}

void EglPreview::SetInfoText(const std::string &text)
{
	if (!text.empty())
		XStoreName(display_, window_, text.c_str());
}


void EglPreview::Show(int fd, libcamera::Span<uint8_t> span, StreamInfo const &info)
{
	Buffer &buffer = buffers_[fd];
	if (buffer.fd == -1)
		makeBuffer(fd, span.size(), info, buffer);


	glUniform1f(contrastALocation, contrastA);
	glUniform1f(contrastBLocation, contrastB);
	glUniform1f(contrastCLocation, contrastC);
	glUniform1f(contrastLocation, contrast);

	glUniform1f(shaderIndexLocation, shaderIndex);

	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(prog);
    glBindVertexArray(VAO);

	glBindTexture(GL_TEXTURE_EXTERNAL_OES, buffer.texture);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	
	textDrawCallback();

	EGLBoolean success [[maybe_unused]] = eglSwapBuffers(egl_display_, egl_surface_);
	if (last_fd_ >= 0)
		done_callback_(last_fd_);
	last_fd_ = fd;
}

void EglPreview::Reset()
{
	for (auto &it : buffers_)
		glDeleteTextures(1, &it.second.texture);
	buffers_.clear();
	last_fd_ = -1;
	eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	first_time_ = true;
}

bool EglPreview::Quit()
{
	XEvent event;
	while (XCheckTypedWindowEvent(display_, window_, ClientMessage, &event))
	{
		if (static_cast<Atom>(event.xclient.data.l[0]) == wm_delete_window_)
			return true;
	}
	return false;
}

Preview *make_egl_preview(Options const *options)
{
	return new EglPreview(options);
}
