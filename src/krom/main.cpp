#if 0

// env
node::Environment *env = node::Environment::GetCurrent(args);

// get int
args[0].As<Int32>()->Value()

// add pointer to object
Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
templ->SetInternalFieldCount(1);

Local<Object> obj = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
obj->SetInternalField(0, External::New(env->isolate(), texture));

// get pointer from object
Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
kinc_g4_index_buffer_t *buffer = (kinc_g4_index_buffer_t *)field->Value();

// create C-String
node::Utf8Value format(env->isolate(), args[1]);

// create JS-String
String::NewFromUtf8(env->isolate(), "image")

// create array-buffer
std::shared_ptr<v8::BackingStore> store =
	    v8::ArrayBuffer::NewBackingStore(data, byteLength, do_not_actually_delete, nullptr);
Local<ArrayBuffer> abuffer = ArrayBuffer::New(env->isolate(), store);

// get data from array-buffer
Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
auto store = buffer->GetBackingStore();

// get property
Local<Value> imageObj = args[0].As<Object>()->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "image").ToLocalChecked()).ToLocalChecked();

// set property
args[0].As<Object>()->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "image").ToLocalChecked(), obj);

#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kinc/audio1/audio.h>
#include <kinc/audio1/sound.h>
#include <kinc/audio1/soundstream.h>
#include <kinc/audio2/audio.h>
#include <kinc/compute/compute.h>
#include <kinc/display.h>
#include <kinc/graphics4/graphics.h>
#include <kinc/graphics4/indexbuffer.h>
#include <kinc/graphics4/pipeline.h>
#include <kinc/graphics4/shader.h>
#include <kinc/graphics4/texture.h>
#include <kinc/graphics4/vertexbuffer.h>
#include <kinc/input/gamepad.h>
#include <kinc/input/keyboard.h>
#include <kinc/input/mouse.h>
#include <kinc/input/pen.h>
#include <kinc/io/filereader.h>
#include <kinc/io/filewriter.h>
#include <kinc/log.h>
#include <kinc/math/random.h>
#include <kinc/system.h>
#include <kinc/threads/mutex.h>
#include <kinc/threads/thread.h>
#include <kinc/window.h>

#include "debug.h"
#include "debug_server.h"
#include "worker.h"

#include <algorithm>
#include <assert.h>
#include <fstream>
#include <map>
#include <sstream>
#include <stdarg.h>
#include <vector>

#ifdef KORE_WINDOWS
#include <Windows.h> // AttachConsole
#endif

#ifndef KORE_WINDOWS
#include <unistd.h>
#endif

#include "env-inl.h"
#include "node_external_reference.h"
#include "string_bytes.h"

#ifdef __MINGW32__
#include <io.h>
#endif // __MINGW32__

#ifdef __POSIX__
#include <climits>  // PATH_MAX on Solaris.
#include <unistd.h> // gethostname, sysconf
#endif              // __POSIX__

#include <array>
#include <cerrno>
#include <cstring>

using v8::Array;
using v8::ArrayBuffer;
using v8::Boolean;
using v8::Context;
using v8::External;
using v8::Float64Array;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::Global;
using v8::HandleScope;
using v8::Int32;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::NewStringType;
using v8::Null;
using v8::Number;
using v8::Object;
using v8::ObjectTemplate;
using v8::String;
using v8::TryCatch;
using v8::Uint32Array;
using v8::Value;

const int KROM_API = 6;
const int KROM_DEBUG_API = 2;

bool AttachProcess(HANDLE hmod);

#ifdef KORE_MACOS
const char *macgetresourcepath();
#endif

const char *getExeDir();

namespace {
	int _argc;
	char **_argv;
	bool debugMode = false;
	bool watch = false;
	bool enableSound = false;
	bool nowindow = false;
	bool serialized = false;
	unsigned int serializedLength = 0;
}

Global<Function> updateFunction;
Global<Function> dropFilesFunction;
Global<Function> cutFunction;
Global<Function> copyFunction;
Global<Function> pasteFunction;
Global<Function> foregroundFunction;
Global<Function> resumeFunction;
Global<Function> pauseFunction;
Global<Function> backgroundFunction;
Global<Function> shutdownFunction;
Global<Function> keyboardDownFunction;
Global<Function> keyboardUpFunction;
Global<Function> keyboardPressFunction;
Global<Function> mouseDownFunction;
Global<Function> mouseUpFunction;
Global<Function> mouseMoveFunction;
Global<Function> mouseWheelFunction;
Global<Function> penDownFunction;
Global<Function> penUpFunction;
Global<Function> penMoveFunction;
Global<Function> gamepadAxisFunction;
Global<Function> gamepadButtonFunction;
Global<Function> audioFunction;

std::map<std::string, bool> imageChanges;
std::map<std::string, bool> shaderChanges;
std::map<std::string, std::string> shaderFileNames;

kinc_mutex_t mutex;
kinc_mutex_t audioMutex;
int audioSamples = 0;
int audioReadLocation = 0;

void update();
void updateAudio(kinc_a2_buffer_t *buffer, int samples);
void dropFiles(wchar_t *filePath);
char *cut();
char *copy();
void paste(char *data);
void foreground();
void resume();
void pause();
void background();
void shutdown();
void keyDown(int code);
void keyUp(int code);
void keyPress(unsigned int character);
void mouseMove(int window, int x, int y, int mx, int my);
void mouseDown(int window, int button, int x, int y);
void mouseUp(int window, int button, int x, int y);
void mouseWheel(int window, int delta);
void penDown(int window, int x, int y, float pressure);
void penUp(int window, int x, int y, float pressure);
void penMove(int window, int x, int y, float pressure);
void gamepadAxis(int pad, int axis, float value);
void gamepadButton(int pad, int button, float value);

const int tempStringSize = 1024 * 1024 - 1;
char tempString[tempStringSize + 1];
char tempStringVS[tempStringSize + 1];
char tempStringFS[tempStringSize + 1];

JsPropertyIdRef buffer_id;

void sendLogMessageArgs(const char *format, va_list args) {
	char msg[4096];
	vsnprintf(msg, sizeof(msg) - 2, format, args);
	kinc_log(KINC_LOG_LEVEL_INFO, "%s", msg);

	/*if (debugMode) {
	  std::vector<int> message;
	  message.push_back(IDE_MESSAGE_LOG);
	  size_t messageLength = strlen(msg);
	  message.push_back(messageLength);
	  for (size_t i = 0; i < messageLength; ++i) {
	    message.push_back(msg[i]);
	  }
	  sendMessage(message.data(), message.size());
	}*/
}

void sendLogMessage(const char *format, ...) {
	va_list args;
	va_start(args, format);
	sendLogMessageArgs(format, args);
	va_end(args);
}

static void krom_init(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	node::BufferValue title(env->isolate(), args[0]);
	int width = args[1].As<Int32>()->Value();
	int height = args[2].As<Int32>()->Value();
	int samplesPerPixel = args[3].As<Int32>()->Value();
	bool vSync = args[4].As<Boolean>()->Value();
	int windowMode = args[5].As<Int32>()->Value();
	int windowFeatures = args[6].As<Int32>()->Value();

	int apiVersion = 0;
	if (args.Length() > 7) {
		apiVersion = args[7].As<Int32>()->Value();
	}

	if (apiVersion != KROM_API) {
		const char *outdated;
		if (apiVersion < KROM_API) {
			outdated = "Kha";
		}
		else if (KROM_API < apiVersion) {
			outdated = "Krom";
		}
		sendLogMessage("Krom uses API version %i but Kha targets API version %i. "
		               "Please update %s.",
		               KROM_API, apiVersion, outdated);
		exit(1);
	}

	kinc_window_options_t win;
	win.title = *title;
	win.width = width;
	win.height = height;
	win.x = -1;
	win.y = -1;
	win.display_index = -1;
	win.visible = !nowindow;
	win.mode = (kinc_window_mode_t)windowMode;
	win.window_features = windowFeatures;
	kinc_framebuffer_options_t frame;
	frame.vertical_sync = vSync;
	frame.samples_per_pixel = samplesPerPixel;
	kinc_init(*title, width, height, &win, &frame);

	kinc_mutex_init(&mutex);
	kinc_mutex_init(&audioMutex);
	if (enableSound) {
		kinc_a2_set_callback(updateAudio);
		kinc_a2_init();
	}
	kinc_random_init((int)(kinc_time() * 1000));

	kinc_set_update_callback(update);
	kinc_set_drop_files_callback(dropFiles);
	kinc_set_copy_callback(copy);
	kinc_set_cut_callback(cut);
	kinc_set_paste_callback(paste);
	kinc_set_foreground_callback(foreground);
	kinc_set_resume_callback(resume);
	kinc_set_pause_callback(pause);
	kinc_set_background_callback(background);
	kinc_set_shutdown_callback(shutdown);

	kinc_keyboard_set_key_down_callback(keyDown);
	kinc_keyboard_set_key_up_callback(keyUp);
	kinc_keyboard_set_key_press_callback(keyPress);
	kinc_mouse_set_move_callback(mouseMove);
	kinc_mouse_set_press_callback(mouseDown);
	kinc_mouse_set_release_callback(mouseUp);
	kinc_mouse_set_scroll_callback(mouseWheel);
	kinc_pen_set_press_callback(penDown);
	kinc_pen_set_release_callback(penUp);
	kinc_pen_set_move_callback(penMove);
	kinc_gamepad_set_axis_callback(gamepadAxis);
	kinc_gamepad_set_button_callback(gamepadButton);
}

static void krom_log(const FunctionCallbackInfo<Value> &args) {
	if (args.Length() < 1) {
		return;
	}

	node::Environment *env = node::Environment::GetCurrent(args);
	node::BufferValue stringValue(env->isolate(), args[0]);
	sendLogMessage(*stringValue);
}

static void krom_graphics_clear(const FunctionCallbackInfo<Value> &args) {
	int flags = args[0].As<Int32>()->Value();
	int color = args[1].As<Int32>()->Value();
	double depth = 0.0f;
	if (!args[2]->IsUndefined()) {
		depth = args[2].As<Number>()->Value();
	}
	int stencil = 0;
	if (!args[3]->IsUndefined()) {
		stencil = args[3].As<Int32>()->Value();
	}
	kinc_g4_clear(flags, color, (float)depth, stencil);
}

static void krom_set_callback(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	updateFunction.Reset(env->isolate(), Local<Function>::Cast(args[0]));
}

static void krom_set_drop_files_callback(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	dropFilesFunction.Reset(env->isolate(), Local<Function>::Cast(args[0]));
}

static void krom_set_cut_copy_paste_callback(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	cutFunction.Reset(env->isolate(), Local<Function>::Cast(args[0]));
	copyFunction.Reset(env->isolate(), Local<Function>::Cast(args[1]));
	pasteFunction.Reset(env->isolate(), Local<Function>::Cast(args[2]));
}

static void krom_set_application_state_callback(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	foregroundFunction.Reset(env->isolate(), Local<Function>::Cast(args[0]));
	resumeFunction.Reset(env->isolate(), Local<Function>::Cast(args[1]));
	pauseFunction.Reset(env->isolate(), Local<Function>::Cast(args[2]));
	backgroundFunction.Reset(env->isolate(), Local<Function>::Cast(args[3]));
	shutdownFunction.Reset(env->isolate(), Local<Function>::Cast(args[4]));
}

static void krom_set_keyboard_down_callback(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	keyboardDownFunction.Reset(env->isolate(), Local<Function>::Cast(args[0]));
}

static void krom_set_keyboard_up_callback(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	keyboardUpFunction.Reset(env->isolate(), Local<Function>::Cast(args[0]));
}

static void krom_set_keyboard_press_callback(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	keyboardPressFunction.Reset(env->isolate(), Local<Function>::Cast(args[0]));
}

static void krom_set_mouse_down_callback(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	mouseDownFunction.Reset(env->isolate(), Local<Function>::Cast(args[0]));
}

static void krom_set_mouse_up_callback(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	mouseUpFunction.Reset(env->isolate(), Local<Function>::Cast(args[0]));
}

static void krom_set_mouse_move_callback(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	mouseMoveFunction.Reset(env->isolate(), Local<Function>::Cast(args[0]));
}

static void krom_set_mouse_wheel_callback(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	mouseWheelFunction.Reset(env->isolate(), Local<Function>::Cast(args[0]));
}

static void krom_set_pen_down_callback(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	penDownFunction.Reset(env->isolate(), Local<Function>::Cast(args[0]));
}

static void krom_set_pen_up_callback(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	penUpFunction.Reset(env->isolate(), Local<Function>::Cast(args[0]));
}

static void krom_set_pen_move_callback(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	penMoveFunction.Reset(env->isolate(), Local<Function>::Cast(args[0]));
}

static void krom_set_gamepad_axis_callback(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	gamepadAxisFunction.Reset(env->isolate(), Local<Function>::Cast(args[0]));
}

static void krom_set_gamepad_button_callback(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	gamepadButtonFunction.Reset(env->isolate(), Local<Function>::Cast(args[0]));
}

static void krom_lock_mouse(const FunctionCallbackInfo<Value> &args) {
	kinc_mouse_lock(0);
}

static void krom_unlock_mouse(const FunctionCallbackInfo<Value> &args) {
	kinc_mouse_unlock();
}

static void krom_can_lock_mouse(const FunctionCallbackInfo<Value> &args) {
	args.GetReturnValue().Set(kinc_mouse_can_lock());
}

static void krom_is_mouse_locked(const FunctionCallbackInfo<Value> &args) {
	args.GetReturnValue().Set(kinc_mouse_is_locked());
}

static void krom_set_mouse_position(const FunctionCallbackInfo<Value> &args) {
	int windowId = args[0].As<Int32>()->Value();
	int x = args[1].As<Int32>()->Value();
	int y = args[2].As<Int32>()->Value();
	kinc_mouse_set_position(windowId, x, y);
}

static void krom_show_mouse(const FunctionCallbackInfo<Value> &args) {
	if (args[0].As<Boolean>()->Value()) {
		kinc_mouse_show();
	}
	else {
		kinc_mouse_hide();
	}
}

static void krom_set_audio_callback(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	audioFunction.Reset(env->isolate(), Local<Function>::Cast(args[0]));
}

static void krom_create_indexbuffer(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	kinc_g4_index_buffer_t *buffer = (kinc_g4_index_buffer_t *)malloc(sizeof(kinc_g4_index_buffer_t));
	kinc_g4_index_buffer_init(buffer, args[0].As<Int32>()->Value(), KINC_G4_INDEX_BUFFER_FORMAT_32BIT, KINC_G4_USAGE_STATIC);

	Local<Object> obj = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	obj->SetInternalField(0, External::New(env->isolate(), buffer));
	args.GetReturnValue().Set(obj);
}

static void krom_delete_indexbuffer(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_index_buffer_t *buffer = (kinc_g4_index_buffer_t *)field->Value();
	kinc_g4_index_buffer_destroy(buffer);
	free(buffer);
}

static void do_not_actually_delete(void *data, size_t length, void *deleter_data) {}

static void krom_lock_index_buffer(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_index_buffer_t *buffer = (kinc_g4_index_buffer_t *)field->Value();
	int *indices = kinc_g4_index_buffer_lock(buffer);

	std::shared_ptr<v8::BackingStore> store =
	    v8::ArrayBuffer::NewBackingStore(indices, kinc_g4_index_buffer_count(buffer) * sizeof(int), do_not_actually_delete, nullptr);
	Local<ArrayBuffer> abuffer = ArrayBuffer::New(env->isolate(), store);

	args.GetReturnValue().Set(Uint32Array::New(abuffer, 0, kinc_g4_index_buffer_count(buffer)));
}

static void krom_unlock_index_buffer(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));

	kinc_g4_index_buffer_t *buffer = (kinc_g4_index_buffer_t *)field->Value();
	kinc_g4_index_buffer_unlock(buffer);
}

static void krom_set_indexbuffer(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));

	kinc_g4_index_buffer_t *buffer = (kinc_g4_index_buffer_t *)field->Value();
	kinc_g4_set_index_buffer(buffer);
}

static kinc_g4_vertex_data_t convert_vertex_data(int kha_vertex_data) {
	switch (kha_vertex_data) {
	case 0: // Float32_1X
		return KINC_G4_VERTEX_DATA_F32_1X;
	case 1: // Float32_2X
		return KINC_G4_VERTEX_DATA_F32_2X;
	case 2: // Float32_3X
		return KINC_G4_VERTEX_DATA_F32_3X;
	case 3: // Float32_4X
		return KINC_G4_VERTEX_DATA_F32_4X;
	case 4: // Float32_4X4
		return KINC_G4_VERTEX_DATA_F32_4X4;
	case 5: // Int8_1X
		return KINC_G4_VERTEX_DATA_I8_1X;
	case 6: // UInt8_1X
		return KINC_G4_VERTEX_DATA_U8_1X;
	case 7: // Int8_1X_Normalized
		return KINC_G4_VERTEX_DATA_I8_1X_NORMALIZED;
	case 8: // UInt8_1X_Normalized
		return KINC_G4_VERTEX_DATA_U8_1X_NORMALIZED;
	case 9: // Int8_2X
		return KINC_G4_VERTEX_DATA_I8_2X;
	case 10: // UInt8_2X
		return KINC_G4_VERTEX_DATA_U8_2X;
	case 11: // Int8_2X_Normalized
		return KINC_G4_VERTEX_DATA_I8_2X_NORMALIZED;
	case 12: // UInt8_2X_Normalized
		return KINC_G4_VERTEX_DATA_U8_2X_NORMALIZED;
	case 13: // Int8_4X
		return KINC_G4_VERTEX_DATA_I8_4X;
	case 14: // UInt8_4X
		return KINC_G4_VERTEX_DATA_U8_4X;
	case 15: // Int8_4X_Normalized
		return KINC_G4_VERTEX_DATA_I8_4X_NORMALIZED;
	case 16: // UInt8_4X_Normalized
		return KINC_G4_VERTEX_DATA_U8_4X_NORMALIZED;
	case 17: // Int16_1X
		return KINC_G4_VERTEX_DATA_I16_1X;
	case 18: // UInt16_1X
		return KINC_G4_VERTEX_DATA_U16_1X;
	case 19: // Int16_1X_Normalized
		return KINC_G4_VERTEX_DATA_I16_1X_NORMALIZED;
	case 20: // UInt16_1X_Normalized
		return KINC_G4_VERTEX_DATA_U16_1X_NORMALIZED;
	case 21: // Int16_2X
		return KINC_G4_VERTEX_DATA_I16_2X;
	case 22: // UInt16_2X
		return KINC_G4_VERTEX_DATA_U16_2X;
	case 23: // Int16_2X_Normalized
		return KINC_G4_VERTEX_DATA_I16_2X_NORMALIZED;
	case 24: // UInt16_2X_Normalized
		return KINC_G4_VERTEX_DATA_U16_2X_NORMALIZED;
	case 25: // Int16_4X
		return KINC_G4_VERTEX_DATA_I16_4X;
	case 26: // UInt16_4X
		return KINC_G4_VERTEX_DATA_U16_4X;
	case 27: // Int16_4X_Normalized
		return KINC_G4_VERTEX_DATA_I16_4X_NORMALIZED;
	case 28: // UInt16_4X_Normalized
		return KINC_G4_VERTEX_DATA_U16_4X_NORMALIZED;
	case 29: // Int32_1X
		return KINC_G4_VERTEX_DATA_I32_1X;
	case 30: // UInt32_1X
		return KINC_G4_VERTEX_DATA_U32_1X;
	case 31: // Int32_2X
		return KINC_G4_VERTEX_DATA_I32_2X;
	case 32: // UInt32_2X
		return KINC_G4_VERTEX_DATA_U32_2X;
	case 33: // Int32_3X
		return KINC_G4_VERTEX_DATA_I32_3X;
	case 34: // UInt32_3X
		return KINC_G4_VERTEX_DATA_U32_3X;
	case 35: // Int32_4X
		return KINC_G4_VERTEX_DATA_I32_4X;
	case 36: // UInt32_4X
		return KINC_G4_VERTEX_DATA_U32_4X;
	default:
		assert(false);
		return KINC_G4_VERTEX_DATA_NONE;
	}
}

static void krom_create_vertexbuffer(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> obj = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();

	Local<Object> jsstructure = args[1].As<Object>();
	int32_t length = jsstructure->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "length").ToLocalChecked())
	                     .ToLocalChecked()
	                     .As<Int32>()
	                     ->Value();
	kinc_g4_vertex_structure_t structure;
	kinc_g4_vertex_structure_init(&structure);
	for (int32_t i = 0; i < length; ++i) {
		Local<Object> element = jsstructure->Get(env->isolate()->GetCurrentContext(), i).ToLocalChecked().As<Object>();
		Local<Value> str = element->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "name").ToLocalChecked()).ToLocalChecked();
		String::Utf8Value utf8_value(env->isolate(), str);
		int32_t data = element->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "data").ToLocalChecked())
		                   .ToLocalChecked()
		                   .As<Int32>()
		                   ->Value();
		char *name = new char[256]; // TODO
		strcpy(name, *utf8_value);
		kinc_g4_vertex_structure_add(&structure, name, convert_vertex_data(data));
	}

	kinc_g4_vertex_buffer_t *buffer = (kinc_g4_vertex_buffer_t *)malloc(sizeof(kinc_g4_vertex_buffer_t));
	kinc_g4_vertex_buffer_init(buffer, args[0].As<Int32>()->Value(), &structure, (kinc_g4_usage_t)args[2].As<Int32>()->Value(), args[3].As<Int32>()->Value());
	obj->SetInternalField(0, External::New(env->isolate(), buffer));
	args.GetReturnValue().Set(obj);
}

static void krom_delete_vertexbuffer(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_vertex_buffer_t *buffer = (kinc_g4_vertex_buffer_t *)field->Value();
	kinc_g4_vertex_buffer_destroy(buffer);
	free(buffer);
}

static void krom_lock_vertex_buffer(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_vertex_buffer_t *buffer = (kinc_g4_vertex_buffer_t *)field->Value();
	int start = args[1].As<Int32>()->Value();
	int count = args[2].As<Int32>()->Value();
	float *vertices = kinc_g4_vertex_buffer_lock(buffer, start, count);

	std::shared_ptr<v8::BackingStore> store =
	    v8::ArrayBuffer::NewBackingStore(vertices, count * kinc_g4_vertex_buffer_stride(buffer), do_not_actually_delete, nullptr);
	Local<ArrayBuffer> abuffer = ArrayBuffer::New(env->isolate(), store);

	args.GetReturnValue().Set(abuffer);
}

static void krom_unlock_vertex_buffer(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	int count = args[1].As<Int32>()->Value();
	kinc_g4_vertex_buffer_t *buffer = (kinc_g4_vertex_buffer_t *)field->Value();
	kinc_g4_vertex_buffer_unlock(buffer, count);
}

static void krom_set_vertexbuffer(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_vertex_buffer_t *buffer = (kinc_g4_vertex_buffer_t *)field->Value();
	kinc_g4_set_vertex_buffer(buffer);
}

static void krom_set_vertexbuffers(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	kinc_g4_vertex_buffer_t *vertexBuffers[8] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
	Local<Object> jsarray = args[0].As<Object>();
	int32_t length =
	    jsarray->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "length").ToLocalChecked()).ToLocalChecked().As<Int32>()->Value();
	for (int i = 0; i < length; ++i) {
		Local<Object> bufferobj = jsarray->Get(env->isolate()->GetCurrentContext(), i)
		                              .ToLocalChecked()
		                              .As<Object>()
		                              ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "buffer").ToLocalChecked())
		                              .ToLocalChecked()
		                              .As<Object>();
		Local<External> bufferfield = Local<External>::Cast(bufferobj->GetInternalField(0));
		vertexBuffers[i] = (kinc_g4_vertex_buffer_t *)bufferfield->Value();
	}
	kinc_g4_set_vertex_buffers(vertexBuffers, length);
}

static void krom_draw_indexed_vertices(const FunctionCallbackInfo<Value> &args) {
	int start = args[0].As<Int32>()->Value();
	int count = args[1].As<Int32>()->Value();
	if (count < 0) {
		kinc_g4_draw_indexed_vertices();
	}
	else {
		kinc_g4_draw_indexed_vertices_from_to(start, count);
	}
}

static void krom_draw_indexed_vertices_instanced(const FunctionCallbackInfo<Value> &args) {
	int instanceCount = args[0].As<Int32>()->Value();
	int start = args[1].As<Int32>()->Value();
	int count = args[2].As<Int32>()->Value();
	if (count < 0) {
		kinc_g4_draw_indexed_vertices_instanced(instanceCount);
	}
	else {
		kinc_g4_draw_indexed_vertices_instanced_from_to(instanceCount, start, count);
	}
}

static std::string replace(std::string str, char a, char b) {
	for (size_t i = 0; i < str.size(); ++i) {
		if (str[i] == a) str[i] = b;
	}
	return str;
}

static void krom_create_vertex_shader(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
	auto store = buffer->GetBackingStore();
	kinc_g4_shader_t *shader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
	kinc_g4_shader_init(shader, store->Data(), store->ByteLength(), KINC_G4_SHADER_TYPE_VERTEX);

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> obj = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	obj->SetInternalField(0, External::New(env->isolate(), shader));
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "name").ToLocalChecked(), args[1]);
	args.GetReturnValue().Set(obj);
}

static void krom_create_vertex_shader_from_source(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	String::Utf8Value utf8_value(env->isolate(), args[0]);
	char *source = new char[strlen(*utf8_value) + 1];
	strcpy(source, *utf8_value);
	kinc_g4_shader_t *shader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
	kinc_g4_shader_init_from_source(shader, source, KINC_G4_SHADER_TYPE_VERTEX);

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> obj = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	obj->SetInternalField(0, External::New(env->isolate(), shader));
	Local<String> name = String::NewFromUtf8(env->isolate(), "").ToLocalChecked();
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "name").ToLocalChecked(), name);
	args.GetReturnValue().Set(obj);
}

static void krom_create_fragment_shader(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
	auto store = buffer->GetBackingStore();
	kinc_g4_shader_t *shader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
	kinc_g4_shader_init(shader, store->Data(), store->ByteLength(), KINC_G4_SHADER_TYPE_FRAGMENT);

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> obj = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	obj->SetInternalField(0, External::New(env->isolate(), shader));
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "name").ToLocalChecked(), args[1]);
	args.GetReturnValue().Set(obj);
}

static void krom_create_fragment_shader_from_source(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	String::Utf8Value utf8_value(env->isolate(), args[0]);
	char *source = new char[strlen(*utf8_value) + 1];
	strcpy(source, *utf8_value);
	kinc_g4_shader_t *shader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
	kinc_g4_shader_init_from_source(shader, source, KINC_G4_SHADER_TYPE_FRAGMENT);

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> obj = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	obj->SetInternalField(0, External::New(env->isolate(), shader));
	Local<String> name = String::NewFromUtf8(env->isolate(), "").ToLocalChecked();
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "name").ToLocalChecked(), name);
	args.GetReturnValue().Set(obj);
}

static void krom_create_geometry_shader(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
	auto store = buffer->GetBackingStore();
	kinc_g4_shader_t *shader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
	kinc_g4_shader_init(shader, store->Data(), store->ByteLength(), KINC_G4_SHADER_TYPE_GEOMETRY);

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> obj = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	obj->SetInternalField(0, External::New(env->isolate(), shader));
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "name").ToLocalChecked(), args[1]);
	args.GetReturnValue().Set(obj);
}

static void krom_create_tessellation_control_shader(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
	auto store = buffer->GetBackingStore();
	kinc_g4_shader_t *shader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
	kinc_g4_shader_init(shader, store->Data(), store->ByteLength(), KINC_G4_SHADER_TYPE_TESSELLATION_CONTROL);

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> obj = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	obj->SetInternalField(0, External::New(env->isolate(), shader));
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "name").ToLocalChecked(), args[1]);
	args.GetReturnValue().Set(obj);
}

static void krom_create_tessellation_evaluation_shader(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
	auto store = buffer->GetBackingStore();
	kinc_g4_shader_t *shader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
	kinc_g4_shader_init(shader, store->Data(), store->ByteLength(), KINC_G4_SHADER_TYPE_TESSELLATION_EVALUATION);

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> obj = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	obj->SetInternalField(0, External::New(env->isolate(), shader));
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "name").ToLocalChecked(), args[1]);
	args.GetReturnValue().Set(obj);
}

static void krom_delete_shader(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_shader_t *shader = (kinc_g4_shader_t *)field->Value();
	kinc_g4_shader_destroy(shader);
	free(shader);
}

static void krom_create_pipeline(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	kinc_g4_pipeline_t *pipeline = (kinc_g4_pipeline_t *)malloc(sizeof(kinc_g4_pipeline_t));
	kinc_g4_pipeline_init(pipeline);

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(8);

	Local<Object> obj = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	obj->SetInternalField(0, External::New(env->isolate(), pipeline));
	args.GetReturnValue().Set(obj);
}

static void krom_delete_pipeline(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_pipeline_t *pipeline = (kinc_g4_pipeline_t *)field->Value();
	kinc_g4_pipeline_destroy(pipeline);
	free(pipeline);
}

static void recompilePipeline(const FunctionCallbackInfo<Value> &args, Local<Object> projobj) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<External> structsfield = Local<External>::Cast(projobj->GetInternalField(1));
	kinc_g4_vertex_structure_t **structures = (kinc_g4_vertex_structure_t **)structsfield->Value();

	Local<External> sizefield = Local<External>::Cast(projobj->GetInternalField(2));
	int32_t size = sizefield.As<Int32>()->Value();

	Local<External> vsfield = Local<External>::Cast(projobj->GetInternalField(3));
	kinc_g4_shader_t *vs = (kinc_g4_shader_t *)vsfield->Value();

	Local<External> fsfield = Local<External>::Cast(projobj->GetInternalField(4));
	kinc_g4_shader_t *fs = (kinc_g4_shader_t *)fsfield->Value();

	kinc_g4_pipeline_t *pipeline = (kinc_g4_pipeline_t *)malloc(sizeof(kinc_g4_pipeline_t));
	kinc_g4_pipeline_init(pipeline);
	pipeline->vertex_shader = vs;
	pipeline->fragment_shader = fs;

	Local<External> gsfield = Local<External>::Cast(projobj->GetInternalField(5));
	if (!gsfield->IsNull() && !gsfield->IsUndefined()) {
		kinc_g4_shader_t *gs = (kinc_g4_shader_t *)gsfield->Value();
		pipeline->geometry_shader = gs;
	}

	Local<External> tcsfield = Local<External>::Cast(projobj->GetInternalField(6));
	if (!tcsfield->IsNull() && !tcsfield->IsUndefined()) {
		kinc_g4_shader_t *tcs = (kinc_g4_shader_t *)tcsfield->Value();
		pipeline->tessellation_control_shader = tcs;
	}

	Local<External> tesfield = Local<External>::Cast(projobj->GetInternalField(7));
	if (!tesfield->IsNull() && !tesfield->IsUndefined()) {
		kinc_g4_shader_t *tes = (kinc_g4_shader_t *)tesfield->Value();
		pipeline->tessellation_evaluation_shader = tes;
	}

	for (int i = 0; i < size; ++i) {
		pipeline->input_layout[i] = structures[i];
	}
	pipeline->input_layout[size] = nullptr;

	kinc_g4_pipeline_compile(pipeline);

	projobj->SetInternalField(0, External::New(env->isolate(), pipeline));
}

static void krom_compile_pipeline(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<Object> progobj = args[0].As<Object>();

	Local<External> progfield = Local<External>::Cast(progobj->GetInternalField(0));
	kinc_g4_pipeline_t *pipeline = (kinc_g4_pipeline_t *)progfield->Value();

	kinc_g4_vertex_structure_t s0, s1, s2, s3;
	kinc_g4_vertex_structure_init(&s0);
	kinc_g4_vertex_structure_init(&s1);
	kinc_g4_vertex_structure_init(&s2);
	kinc_g4_vertex_structure_init(&s3);
	kinc_g4_vertex_structure_t *structures[4] = {&s0, &s1, &s2, &s3};

	int32_t size = args[5].As<Int32>()->Value();
	for (int32_t i1 = 0; i1 < size; ++i1) {
		Local<Object> jsstructure = args[i1 + 1].As<Object>();
		structures[i1]->instanced = jsstructure->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "instanced").ToLocalChecked())
		                                .ToLocalChecked()
		                                .As<Boolean>()
		                                ->Value();
		Local<Object> elements = jsstructure->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "elements").ToLocalChecked())
		                             .ToLocalChecked()
		                             .As<Object>();
		int32_t length = elements->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "length").ToLocalChecked())
		                     .ToLocalChecked()
		                     .As<Int32>()
		                     ->Value();
		for (int32_t i2 = 0; i2 < length; ++i2) {
			Local<Object> element = elements->Get(env->isolate()->GetCurrentContext(), i2).ToLocalChecked().As<Object>();
			Local<Value> str = element->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "name").ToLocalChecked()).ToLocalChecked();
			String::Utf8Value utf8_value(env->isolate(), str);
			int32_t data = element->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "data").ToLocalChecked())
			                   .ToLocalChecked()
			                   .As<Int32>()
			                   ->Value();
			char *name = new char[256]; // TODO
			strcpy(name, *utf8_value);
			kinc_g4_vertex_structure_add(structures[i1], name, (kinc_g4_vertex_data_t)data);
		}
	}

	progobj->SetInternalField(1, External::New(env->isolate(), structures));
	progobj->SetInternalField(2, External::New(env->isolate(), &size));

	Local<External> vsfield = Local<External>::Cast(args[6].As<Object>()->GetInternalField(0));
	kinc_g4_shader_t *vertexShader = (kinc_g4_shader_t *)vsfield->Value();
	progobj->SetInternalField(3, External::New(env->isolate(), vertexShader));
	progobj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "vsname").ToLocalChecked(),
	             args[6].As<Object>()->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "name").ToLocalChecked()).ToLocalChecked());

	Local<External> fsfield = Local<External>::Cast(args[7].As<Object>()->GetInternalField(0));
	kinc_g4_shader_t *fragmentShader = (kinc_g4_shader_t *)fsfield->Value();
	progobj->SetInternalField(4, External::New(env->isolate(), fragmentShader));
	progobj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "fsname").ToLocalChecked(),
	             args[7].As<Object>()->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "name").ToLocalChecked()).ToLocalChecked());

	pipeline->vertex_shader = vertexShader;
	pipeline->fragment_shader = fragmentShader;

	if (!args[8]->IsNull() && !args[8]->IsUndefined()) {
		Local<External> gsfield = Local<External>::Cast(args[8].As<Object>()->GetInternalField(0));
		kinc_g4_shader_t *geometryShader = (kinc_g4_shader_t *)gsfield->Value();
		progobj->SetInternalField(5, External::New(env->isolate(), geometryShader));
		progobj->Set(
		    env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "gsname").ToLocalChecked(),
		    args[8].As<Object>()->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "name").ToLocalChecked()).ToLocalChecked());
		pipeline->geometry_shader = geometryShader;
	}

	if (!args[9]->IsNull() && !args[9]->IsUndefined()) {
		Local<External> tcsfield = Local<External>::Cast(args[9].As<Object>()->GetInternalField(0));
		kinc_g4_shader_t *tessellationControlShader = (kinc_g4_shader_t *)tcsfield->Value();
		progobj->SetInternalField(6, External::New(env->isolate(), tessellationControlShader));
		progobj->Set(
		    env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "tcsname").ToLocalChecked(),
		    args[9].As<Object>()->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "name").ToLocalChecked()).ToLocalChecked());
		pipeline->tessellation_control_shader = tessellationControlShader;
	}

	if (!args[10]->IsNull() && !args[10]->IsUndefined()) {
		Local<External> tesfield = Local<External>::Cast(args[10].As<Object>()->GetInternalField(0));
		kinc_g4_shader_t *tessellationEvaluationShader = (kinc_g4_shader_t *)tesfield->Value();
		progobj->SetInternalField(7, External::New(env->isolate(), tessellationEvaluationShader));
		progobj->Set(
		    env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "tesname").ToLocalChecked(),
		    args[10].As<Object>()->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "name").ToLocalChecked()).ToLocalChecked());
		pipeline->tessellation_evaluation_shader = tessellationEvaluationShader;
	}

	for (int i = 0; i < size; ++i) {
		pipeline->input_layout[i] = structures[i];
	}
	pipeline->input_layout[size] = nullptr;

	pipeline->cull_mode = (kinc_g4_cull_mode_t)args[11]
	                          .As<Object>()
	                          ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "cullMode").ToLocalChecked())
	                          .ToLocalChecked()
	                          .As<Int32>()
	                          ->Value();

	pipeline->depth_write = args[11]
	                            .As<Object>()
	                            ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "depthWrite").ToLocalChecked())
	                            .ToLocalChecked()
	                            .As<Boolean>()
	                            ->Value();
	pipeline->depth_mode = (kinc_g4_compare_mode_t)args[11]
	                           .As<Object>()
	                           ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "depthMode").ToLocalChecked())
	                           .ToLocalChecked()
	                           .As<Int32>()
	                           ->Value();

	pipeline->stencil_mode = (kinc_g4_compare_mode_t)args[11]
	                             .As<Object>()
	                             ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "stencilMode").ToLocalChecked())
	                             .ToLocalChecked()
	                             .As<Int32>()
	                             ->Value();
	pipeline->stencil_both_pass = (kinc_g4_stencil_action_t)args[11]
	                                  .As<Object>()
	                                  ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "stencilBothPass").ToLocalChecked())
	                                  .ToLocalChecked()
	                                  .As<Int32>()
	                                  ->Value();
	pipeline->stencil_depth_fail = (kinc_g4_stencil_action_t)args[11]
	                                   .As<Object>()
	                                   ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "stencilDepthFail").ToLocalChecked())
	                                   .ToLocalChecked()
	                                   .As<Int32>()
	                                   ->Value();
	pipeline->stencil_fail = (kinc_g4_stencil_action_t)args[11]
	                             .As<Object>()
	                             ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "stencilFail").ToLocalChecked())
	                             .ToLocalChecked()
	                             .As<Int32>()
	                             ->Value();
	pipeline->stencil_reference_value =
	    args[11]
	        .As<Object>()
	        ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "stencilReferenceValue").ToLocalChecked())
	        .ToLocalChecked()
	        .As<Int32>()
	        ->Value();
	pipeline->stencil_read_mask = args[11]
	                                  .As<Object>()
	                                  ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "stencilReadMask").ToLocalChecked())
	                                  .ToLocalChecked()
	                                  .As<Int32>()
	                                  ->Value();
	pipeline->stencil_write_mask = args[11]
	                                   .As<Object>()
	                                   ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "stencilWriteMask").ToLocalChecked())
	                                   .ToLocalChecked()
	                                   .As<Int32>()
	                                   ->Value();

	pipeline->blend_source = (kinc_g4_blending_factor_t)args[11]
	                             .As<Object>()
	                             ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "blendSource").ToLocalChecked())
	                             .ToLocalChecked()
	                             .As<Int32>()
	                             ->Value();
	pipeline->blend_destination = (kinc_g4_blending_factor_t)args[11]
	                                  .As<Object>()
	                                  ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "blendDestination").ToLocalChecked())
	                                  .ToLocalChecked()
	                                  .As<Int32>()
	                                  ->Value();
	pipeline->blend_operation = KINC_G4_BLENDOP_ADD;
	pipeline->alpha_blend_source = (kinc_g4_blending_factor_t)args[11]
	                                   .As<Object>()
	                                   ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "alphaBlendSource").ToLocalChecked())
	                                   .ToLocalChecked()
	                                   .As<Int32>()
	                                   ->Value();
	pipeline->alpha_blend_destination =
	    (kinc_g4_blending_factor_t)args[11]
	        .As<Object>()
	        ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "alphaBlendDestination").ToLocalChecked())
	        .ToLocalChecked()
	        .As<Int32>()
	        ->Value();
	pipeline->alpha_blend_operation = KINC_G4_BLENDOP_ADD;

	Local<Object> maskRedArray = args[11]
	                                 .As<Object>()
	                                 ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "colorWriteMaskRed").ToLocalChecked())
	                                 .ToLocalChecked()
	                                 .As<Object>();
	Local<Object> maskGreenArray = args[11]
	                                   .As<Object>()
	                                   ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "colorWriteMaskGreen").ToLocalChecked())
	                                   .ToLocalChecked()
	                                   .As<Object>();
	Local<Object> maskBlueArray = args[11]
	                                  .As<Object>()
	                                  ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "colorWriteMaskBlue").ToLocalChecked())
	                                  .ToLocalChecked()
	                                  .As<Object>();
	Local<Object> maskAlphaArray = args[11]
	                                   .As<Object>()
	                                   ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "colorWriteMaskAlpha").ToLocalChecked())
	                                   .ToLocalChecked()
	                                   .As<Object>();

	for (int i = 0; i < 8; ++i) {
		pipeline->color_write_mask_red[i] = maskRedArray->Get(env->isolate()->GetCurrentContext(), i).ToLocalChecked().As<Boolean>()->Value();
		pipeline->color_write_mask_green[i] = maskGreenArray->Get(env->isolate()->GetCurrentContext(), i).ToLocalChecked().As<Boolean>()->Value();
		pipeline->color_write_mask_blue[i] = maskBlueArray->Get(env->isolate()->GetCurrentContext(), i).ToLocalChecked().As<Boolean>()->Value();
		pipeline->color_write_mask_alpha[i] = maskAlphaArray->Get(env->isolate()->GetCurrentContext(), i).ToLocalChecked().As<Boolean>()->Value();
	}

	pipeline->conservative_rasterization =
	    args[11]
	        .As<Object>()
	        ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "conservativeRasterization").ToLocalChecked())
	        .ToLocalChecked()
	        .As<Boolean>()
	        ->Value();

	kinc_g4_pipeline_compile(pipeline);
}

std::string shadersdir;

static void krom_set_pipeline(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<Object> progobj = args[0].As<Object>();
	Local<External> progfield = Local<External>::Cast(progobj->GetInternalField(0));
	kinc_g4_pipeline_t *pipeline = (kinc_g4_pipeline_t *)progfield->Value();

	if (debugMode) {
		Local<Value> vsnameobj =
		    progobj->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "vsname").ToLocalChecked()).ToLocalChecked();
		String::Utf8Value vsname(env->isolate(), vsnameobj);

		Local<Value> fsnameobj =
		    progobj->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "fsname").ToLocalChecked()).ToLocalChecked();
		String::Utf8Value fsname(env->isolate(), fsnameobj);

		bool shaderChanged = false;

		if (shaderChanges[*vsname]) {
			shaderChanged = true;
			sendLogMessage("Reloading shader %s.", *vsname);
			std::string filename = shaderFileNames[*vsname];
			std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary);
			std::vector<char> buffer((std::istreambuf_iterator<char>(input)), (std::istreambuf_iterator<char>()));
			kinc_g4_shader_t *vertexShader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
			kinc_g4_shader_init(vertexShader, buffer.data(), (int)buffer.size(), KINC_G4_SHADER_TYPE_VERTEX);
			progobj->SetInternalField(3, External::New(env->isolate(), vertexShader));
			shaderChanges[*vsname] = false;
		}

		if (shaderChanges[*fsname]) {
			shaderChanged = true;
			sendLogMessage("Reloading shader %s.", *fsname);
			std::string filename = shaderFileNames[*fsname];
			std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary);
			std::vector<char> buffer((std::istreambuf_iterator<char>(input)), (std::istreambuf_iterator<char>()));
			kinc_g4_shader_t *fragmentShader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
			kinc_g4_shader_init(fragmentShader, buffer.data(), (int)buffer.size(), KINC_G4_SHADER_TYPE_FRAGMENT);
			progobj->SetInternalField(4, External::New(env->isolate(), fragmentShader));
			shaderChanges[*fsname] = false;
		}

		Local<Value> gsnameobj =
		    progobj->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "gsname").ToLocalChecked()).ToLocalChecked();
		if (!gsnameobj->IsNull() && !gsnameobj->IsUndefined()) {
			String::Utf8Value gsname(env->isolate(), gsnameobj);
			if (shaderChanges[*gsname]) {
				shaderChanged = true;
				sendLogMessage("Reloading shader %s.", *gsname);
				std::string filename = shaderFileNames[*gsname];
				std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary);
				std::vector<char> buffer((std::istreambuf_iterator<char>(input)), (std::istreambuf_iterator<char>()));
				kinc_g4_shader_t *geometryShader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
				kinc_g4_shader_init(geometryShader, buffer.data(), (int)buffer.size(), KINC_G4_SHADER_TYPE_GEOMETRY);
				progobj->SetInternalField(5, External::New(env->isolate(), geometryShader));
				shaderChanges[*gsname] = false;
			}
		}

		Local<Value> tcsnameobj =
		    progobj->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "tcsname").ToLocalChecked()).ToLocalChecked();
		if (!tcsnameobj->IsNull() && !tcsnameobj->IsUndefined()) {
			String::Utf8Value tcsname(env->isolate(), tcsnameobj);
			if (shaderChanges[*tcsname]) {
				shaderChanged = true;
				sendLogMessage("Reloading shader %s.", *tcsname);
				std::string filename = shaderFileNames[*tcsname];
				std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary);
				std::vector<char> buffer((std::istreambuf_iterator<char>(input)), (std::istreambuf_iterator<char>()));
				kinc_g4_shader_t *tessellationControlShader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
				kinc_g4_shader_init(tessellationControlShader, buffer.data(), (int)buffer.size(), KINC_G4_SHADER_TYPE_TESSELLATION_CONTROL);
				progobj->SetInternalField(6, External::New(env->isolate(), tessellationControlShader));
				shaderChanges[*tcsname] = false;
			}
		}

		Local<Value> tesnameobj =
		    progobj->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "tesname").ToLocalChecked()).ToLocalChecked();
		if (!tesnameobj->IsNull() && !tesnameobj->IsUndefined()) {
			String::Utf8Value tesname(env->isolate(), tesnameobj);
			if (shaderChanges[*tesname]) {
				shaderChanged = true;
				sendLogMessage("Reloading shader %s.", *tesname);
				std::string filename = shaderFileNames[*tesname];
				std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary);
				std::vector<char> buffer((std::istreambuf_iterator<char>(input)), (std::istreambuf_iterator<char>()));
				kinc_g4_shader_t *tessellationEvaluationShader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
				kinc_g4_shader_init(tessellationEvaluationShader, buffer.data(), (int)buffer.size(), KINC_G4_SHADER_TYPE_TESSELLATION_EVALUATION);
				progobj->SetInternalField(7, External::New(env->isolate(), tessellationEvaluationShader));
				shaderChanges[*tesname] = false;
			}
		}

		if (shaderChanged) {
			recompilePipeline(args, progobj);
			Local<External> progfield = Local<External>::Cast(progobj->GetInternalField(0));
			pipeline = (kinc_g4_pipeline_t *)progfield->Value();
		}
	}

	kinc_g4_set_pipeline(pipeline);
}

static void krom_load_image(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	String::Utf8Value filename(env->isolate(), args[0]);
	bool readable = args[1].As<Boolean>()->Value();

	kinc_image_t image;
	size_t size = kinc_image_size_from_file(*filename);
	void *memory = malloc(size);
	kinc_image_init_from_file(&image, memory, *filename);

	kinc_g4_texture_t *texture = (kinc_g4_texture_t *)malloc(sizeof(kinc_g4_texture_t));
	kinc_g4_texture_init_from_image(texture, &image);

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> obj = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	obj->SetInternalField(0, External::New(env->isolate(), texture));
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "width").ToLocalChecked(), Int32::New(env->isolate(), image.width));
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "height").ToLocalChecked(), Int32::New(env->isolate(), image.height));
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "realWidth").ToLocalChecked(),
	         Int32::New(env->isolate(), texture->tex_width));
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "realHeight").ToLocalChecked(),
	         Int32::New(env->isolate(), texture->tex_height));
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "filename").ToLocalChecked(), args[0]);

	if (readable) {
		kinc_image_t *imagePtr = (kinc_image_t *)malloc(sizeof(kinc_image_t));
		memcpy(imagePtr, &image, sizeof(image));

		Local<Object> imageObject = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(env->isolate(), imagePtr));

		obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "image").ToLocalChecked(), imageObject);
	}
	else {
		kinc_image_destroy(&image);
		free(memory);
	}

	args.GetReturnValue().Set(obj);
}

static void krom_unload_image(const FunctionCallbackInfo<Value> &args) {
	if (args[0]->IsNull() || args[0]->IsUndefined()) return;

	node::Environment *env = node::Environment::GetCurrent(args);

	Local<Object> image = args[0].As<Object>();
	Local<Value> tex = image->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "texture_").ToLocalChecked()).ToLocalChecked();
	Local<Value> rt = image->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "renderTarget_").ToLocalChecked()).ToLocalChecked();

	if (tex->IsObject()) {
		Local<External> texfield = Local<External>::Cast(tex.As<Object>()->GetInternalField(0));
		kinc_g4_texture_t *texture = (kinc_g4_texture_t *)texfield->Value();
		kinc_g4_texture_destroy(texture);
		free(texture);

		Local<Value> imageObj =
		    tex.As<Object>()->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "image").ToLocalChecked()).ToLocalChecked();
		if (imageObj->IsNull() || imageObj->IsUndefined()) {
			Local<External> field = Local<External>::Cast(imageObj.As<Object>()->GetInternalField(0));
			kinc_image_t *image = (kinc_image_t *)field->Value();
			free(image->data);
			kinc_image_destroy(image);
			free(image);
		}
	}
	else if (rt->IsObject()) {
		Local<External> rtfield = Local<External>::Cast(rt.As<Object>()->GetInternalField(0));
		kinc_g4_render_target_t *renderTarget = (kinc_g4_render_target_t *)rtfield->Value();
		kinc_g4_render_target_destroy(renderTarget);
		free(renderTarget);
	}
}

static void krom_load_sound(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	String::Utf8Value utf8_value(env->isolate(), args[0]);

	kinc_a1_sound_t *sound = kinc_a1_sound_create(*utf8_value);

	std::shared_ptr<v8::BackingStore> store = v8::ArrayBuffer::NewBackingStore(env->isolate(), sound->size * 2 * sizeof(float));

	float *to = (float *)store->Data();

	Kore::s16 *left = (Kore::s16 *)&sound->left[0];
	Kore::s16 *right = (Kore::s16 *)&sound->right[0];
	for (int i = 0; i < sound->size; i += 1) {
		to[i * 2 + 0] = (float)(left[i] / 32767.0);
		to[i * 2 + 1] = (float)(right[i] / 32767.0);
	}

	kinc_a1_sound_destroy(sound);

	Local<ArrayBuffer> buffer = ArrayBuffer::New(env->isolate(), store);
	args.GetReturnValue().Set(buffer);
}

static void krom_write_audio_buffer(const FunctionCallbackInfo<Value> &args) {
	/*uint8_t *buffer;
	unsigned bufferLength;
	JsGetArrayBufferStorage(arguments[1], &buffer, &bufferLength);

	int samples;
	JsNumberToInt(arguments[2], &samples);

	for (int i = 0; i < samples; ++i) {
	    float value = *(float *)&buffer[audioReadLocation];
	    audioReadLocation += 4;
	    if (audioReadLocation >= bufferLength) audioReadLocation = 0;

	    // TODO: This is madness
	    // *(float *)&Kore::Audio2::buffer.data[Kore::Audio2::buffer.writeLocation]
	    // = value; Kore::Audio2::buffer.writeLocation += 4; if
	    // (Kore::Audio2::buffer.writeLocation >= Kore::Audio2::buffer.dataSize)
	    // Kore::Audio2::buffer.writeLocation = 0;
	}*/
}

static void krom_load_blob(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	String::Utf8Value filename(env->isolate(), args[0]);

	kinc_file_reader_t reader;
	if (!kinc_file_reader_open(&reader, *filename, KINC_FILE_TYPE_ASSET)) {
		return;
	}

	std::shared_ptr<v8::BackingStore> store = v8::ArrayBuffer::NewBackingStore(env->isolate(), kinc_file_reader_size(&reader));

	kinc_file_reader_read(&reader, store->Data(), kinc_file_reader_size(&reader));

	kinc_file_reader_close(&reader);

	Local<ArrayBuffer> buffer = ArrayBuffer::New(env->isolate(), store);
	args.GetReturnValue().Set(buffer);
}

static void krom_get_constant_location(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<External> progfield = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_pipeline_t *pipeline = (kinc_g4_pipeline_t *)progfield->Value();

	String::Utf8Value utf8_value(env->isolate(), args[1]);
	kinc_g4_constant_location_t location = kinc_g4_pipeline_get_constant_location(pipeline, *utf8_value);
	kinc_g4_constant_location_t *locationPtr = (kinc_g4_constant_location_t *)malloc(sizeof(kinc_g4_constant_location_t));
	memcpy(locationPtr, &location, sizeof(location));

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> obj = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	obj->SetInternalField(0, External::New(env->isolate(), locationPtr));
	args.GetReturnValue().Set(obj);
}

static void krom_get_texture_unit(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<External> progfield = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_pipeline_t *pipeline = (kinc_g4_pipeline_t *)progfield->Value();

	String::Utf8Value utf8_value(env->isolate(), args[1]);
	kinc_g4_texture_unit_t unit = kinc_g4_pipeline_get_texture_unit(pipeline, *utf8_value);
	kinc_g4_texture_unit_t *unitPtr = (kinc_g4_texture_unit_t *)malloc(sizeof(kinc_g4_texture_unit_t));
	memcpy(unitPtr, &unit, sizeof(unit));

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> obj = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	obj->SetInternalField(0, External::New(env->isolate(), unitPtr));
	args.GetReturnValue().Set(obj);
}

static void krom_set_texture(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<External> unitfield = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_texture_unit_t *unit = (kinc_g4_texture_unit_t *)unitfield->Value();

	kinc_g4_texture_t *texture;
	bool imageChanged = false;
	if (debugMode) {
		String::Utf8Value filename(
		    env->isolate(),
		    args[1].As<Object>()->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "filename").ToLocalChecked()).ToLocalChecked());
		if (imageChanges[*filename]) {
			imageChanges[*filename] = false;
			sendLogMessage("Image %s changed.", *filename);

			// TODO: Set all texture properties and free previous texture/image

			kinc_image_t image;
			size_t size = kinc_image_size_from_file(*filename);
			void *memory = malloc(size);
			kinc_image_init_from_file(&image, memory, *filename);

			texture = (kinc_g4_texture_t *)malloc(sizeof(kinc_g4_texture_t));
			kinc_g4_texture_init_from_image(texture, &image);

			args[1].As<Object>()->SetInternalField(0, External::New(env->isolate(), texture));
			imageChanged = true;
		}
	}
	if (!imageChanged) {
		Local<External> texfield = Local<External>::Cast(args[1].As<Object>()->GetInternalField(0));
		texture = (kinc_g4_texture_t *)texfield->Value();
	}
	kinc_g4_set_texture(*unit, texture);
}

static void krom_set_render_target(const FunctionCallbackInfo<Value> &args) {
	Local<External> unitfield = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_texture_unit_t *unit = (kinc_g4_texture_unit_t *)unitfield->Value();

	Local<External> rtfield = Local<External>::Cast(args[1].As<Object>()->GetInternalField(0));
	kinc_g4_render_target_t *renderTarget = (kinc_g4_render_target_t *)rtfield->Value();
	kinc_g4_render_target_use_color_as_texture(renderTarget, *unit);
}

static void krom_set_texture_depth(const FunctionCallbackInfo<Value> &args) {
	Local<External> unitfield = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_texture_unit_t *unit = (kinc_g4_texture_unit_t *)unitfield->Value();

	Local<External> rtfield = Local<External>::Cast(args[1].As<Object>()->GetInternalField(0));
	kinc_g4_render_target_t *renderTarget = (kinc_g4_render_target_t *)rtfield->Value();
	kinc_g4_render_target_use_depth_as_texture(renderTarget, *unit);
}

static void krom_set_image_texture(const FunctionCallbackInfo<Value> &args) {
	Local<External> unitfield = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_texture_unit_t *unit = (kinc_g4_texture_unit_t *)unitfield->Value();

	Local<External> texfield = Local<External>::Cast(args[1].As<Object>()->GetInternalField(0));
	kinc_g4_texture_t *texture = (kinc_g4_texture_t *)texfield->Value();
	kinc_g4_set_image_texture(*unit, texture);
}

static void krom_set_texture_parameters(const FunctionCallbackInfo<Value> &args) {
	Local<External> unitfield = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_texture_unit_t *unit = (kinc_g4_texture_unit_t *)unitfield->Value();

	int u = args[1].As<Int32>()->Value();
	int v = args[2].As<Int32>()->Value();
	int min = args[3].As<Int32>()->Value();
	int max = args[4].As<Int32>()->Value();
	int mip = args[5].As<Int32>()->Value();

	kinc_g4_set_texture_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_U, (kinc_g4_texture_addressing_t)u);
	kinc_g4_set_texture_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_V, (kinc_g4_texture_addressing_t)v);
	kinc_g4_set_texture_minification_filter(*unit, (kinc_g4_texture_filter_t)min);
	kinc_g4_set_texture_magnification_filter(*unit, (kinc_g4_texture_filter_t)max);
	kinc_g4_set_texture_mipmap_filter(*unit, (kinc_g4_mipmap_filter_t)mip);
}

static void krom_set_texture_3d_parameters(const FunctionCallbackInfo<Value> &args) {
	Local<External> unitfield = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_texture_unit_t *unit = (kinc_g4_texture_unit_t *)unitfield->Value();

	int u = args[1].As<Int32>()->Value();
	int v = args[2].As<Int32>()->Value();
	int w = args[3].As<Int32>()->Value();
	int min = args[4].As<Int32>()->Value();
	int max = args[5].As<Int32>()->Value();
	int mip = args[6].As<Int32>()->Value();

	kinc_g4_set_texture3d_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_U, (kinc_g4_texture_addressing_t)u);
	kinc_g4_set_texture3d_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_V, (kinc_g4_texture_addressing_t)v);
	kinc_g4_set_texture3d_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_W, (kinc_g4_texture_addressing_t)w);
	kinc_g4_set_texture3d_minification_filter(*unit, (kinc_g4_texture_filter_t)min);
	kinc_g4_set_texture3d_magnification_filter(*unit, (kinc_g4_texture_filter_t)max);
	kinc_g4_set_texture3d_mipmap_filter(*unit, (kinc_g4_mipmap_filter_t)mip);
}

static void krom_set_texture_compare_mode(const FunctionCallbackInfo<Value> &args) {
	Local<External> unitfield = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_texture_unit_t *unit = (kinc_g4_texture_unit_t *)unitfield->Value();

	bool enabled = args[1].As<Boolean>()->Value();

	kinc_g4_set_texture_compare_mode(*unit, enabled);
}

static void krom_set_cube_map_compare_mode(const FunctionCallbackInfo<Value> &args) {
	Local<External> unitfield = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_texture_unit_t *unit = (kinc_g4_texture_unit_t *)unitfield->Value();

	bool enabled = args[1].As<Boolean>()->Value();

	kinc_g4_set_cubemap_compare_mode(*unit, enabled);
}

static void krom_set_bool(const FunctionCallbackInfo<Value> &args) {
	Local<External> locfield = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_constant_location_t *location = (kinc_g4_constant_location_t *)locfield->Value();

	bool value = args[1].As<Boolean>()->Value();
	kinc_g4_set_bool(*location, value);
}

static void krom_set_int(const FunctionCallbackInfo<Value> &args) {
	Local<External> locfield = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_constant_location_t *location = (kinc_g4_constant_location_t *)locfield->Value();

	int value = args[1].As<Int32>()->Value();
	kinc_g4_set_int(*location, value);
}

static void krom_set_float(const FunctionCallbackInfo<Value> &args) {
	Local<External> locfield = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_constant_location_t *location = (kinc_g4_constant_location_t *)locfield->Value();

	float value = (float)args[1].As<Number>()->Value();
	kinc_g4_set_float(*location, value);
}

static void krom_set_float2(const FunctionCallbackInfo<Value> &args) {
	Local<External> locfield = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_constant_location_t *location = (kinc_g4_constant_location_t *)locfield->Value();

	float value1 = (float)args[1].As<Number>()->Value();
	float value2 = (float)args[2].As<Number>()->Value();
	kinc_g4_set_float2(*location, value1, value2);
}

static void krom_set_float3(const FunctionCallbackInfo<Value> &args) {
	Local<External> locfield = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_constant_location_t *location = (kinc_g4_constant_location_t *)locfield->Value();

	float value1 = (float)args[1].As<Number>()->Value();
	float value2 = (float)args[2].As<Number>()->Value();
	float value3 = (float)args[3].As<Number>()->Value();
	kinc_g4_set_float3(*location, value1, value2, value3);
}

static void krom_set_float4(const FunctionCallbackInfo<Value> &args) {
	Local<External> locfield = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_constant_location_t *location = (kinc_g4_constant_location_t *)locfield->Value();

	float value1 = (float)args[1].As<Number>()->Value();
	float value2 = (float)args[2].As<Number>()->Value();
	float value3 = (float)args[3].As<Number>()->Value();
	float value4 = (float)args[4].As<Number>()->Value();
	kinc_g4_set_float4(*location, value1, value2, value3, value4);
}

static void krom_set_floats(const FunctionCallbackInfo<Value> &args) {
	Local<External> locfield = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_constant_location_t *location = (kinc_g4_constant_location_t *)locfield->Value();

	Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[1]);
	auto store = buffer->GetBackingStore();

	float *from = (float *)store->Data();

	kinc_g4_set_floats(*location, from, int(store->ByteLength() / 4));
}

static void krom_set_matrix(const FunctionCallbackInfo<Value> &args) {
	Local<External> locfield = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_constant_location_t *location = (kinc_g4_constant_location_t *)locfield->Value();

	Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[1]);
	auto store = buffer->GetBackingStore();

	float *from = (float *)store->Data();
	kinc_matrix4x4_t m;
	kinc_matrix4x4_set(&m, 0, 0, from[0]);
	kinc_matrix4x4_set(&m, 1, 0, from[1]);
	kinc_matrix4x4_set(&m, 2, 0, from[2]);
	kinc_matrix4x4_set(&m, 3, 0, from[3]);
	kinc_matrix4x4_set(&m, 0, 1, from[4]);
	kinc_matrix4x4_set(&m, 1, 1, from[5]);
	kinc_matrix4x4_set(&m, 2, 1, from[6]);
	kinc_matrix4x4_set(&m, 3, 1, from[7]);
	kinc_matrix4x4_set(&m, 0, 2, from[8]);
	kinc_matrix4x4_set(&m, 1, 2, from[9]);
	kinc_matrix4x4_set(&m, 2, 2, from[10]);
	kinc_matrix4x4_set(&m, 3, 2, from[11]);
	kinc_matrix4x4_set(&m, 0, 3, from[12]);
	kinc_matrix4x4_set(&m, 1, 3, from[13]);
	kinc_matrix4x4_set(&m, 2, 3, from[14]);
	kinc_matrix4x4_set(&m, 3, 3, from[15]);

	kinc_g4_set_matrix4(*location, &m);
}

static void krom_set_matrix3(const FunctionCallbackInfo<Value> &args) {
	Local<External> locfield = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_constant_location_t *location = (kinc_g4_constant_location_t *)locfield->Value();

	Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[1]);
	auto store = buffer->GetBackingStore();

	float *from = (float *)store->Data();
	kinc_matrix3x3_t m;
	kinc_matrix3x3_set(&m, 0, 0, from[0]);
	kinc_matrix3x3_set(&m, 1, 0, from[1]);
	kinc_matrix3x3_set(&m, 2, 0, from[2]);
	kinc_matrix3x3_set(&m, 0, 1, from[3]);
	kinc_matrix3x3_set(&m, 1, 1, from[4]);
	kinc_matrix3x3_set(&m, 2, 1, from[5]);
	kinc_matrix3x3_set(&m, 0, 2, from[6]);
	kinc_matrix3x3_set(&m, 1, 2, from[7]);
	kinc_matrix3x3_set(&m, 2, 2, from[8]);

	kinc_g4_set_matrix3(*location, &m);
}

static void krom_get_time(const FunctionCallbackInfo<Value> &args) {
	args.GetReturnValue().Set(kinc_time());
}

static void krom_window_width(const FunctionCallbackInfo<Value> &args) {
	int windowId = args[0].As<Int32>()->Value();
	args.GetReturnValue().Set(kinc_window_width(windowId));
}

static void krom_window_height(const FunctionCallbackInfo<Value> &args) {
	int windowId = args[0].As<Int32>()->Value();
	args.GetReturnValue().Set(kinc_window_height(windowId));
}

static void krom_set_window_title(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	int windowId = args[0].As<Int32>()->Value();
	node::Utf8Value title(env->isolate(), args[1]);
	kinc_window_set_title(windowId, *title);
}

static void krom_screen_dpi(const FunctionCallbackInfo<Value> &args) {
	args.GetReturnValue().Set(kinc_primary_display());
}

static void krom_system_id(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	args.GetReturnValue().Set(String::NewFromUtf8(env->isolate(), kinc_system_id()).ToLocalChecked());
}

static void krom_request_shutdown(const FunctionCallbackInfo<Value> &args) {
	kinc_stop();
}

static void krom_display_count(const FunctionCallbackInfo<Value> &args) {
	args.GetReturnValue().Set(kinc_count_displays());
}

static void krom_display_width(const FunctionCallbackInfo<Value> &args) {
	args.GetReturnValue().Set(kinc_display_current_mode(args[0].As<Int32>()->Value()).width);
}

static void krom_display_height(const FunctionCallbackInfo<Value> &args) {
	args.GetReturnValue().Set(kinc_display_current_mode(args[0].As<Int32>()->Value()).height);
}

static void krom_display_x(const FunctionCallbackInfo<Value> &args) {
	args.GetReturnValue().Set(kinc_display_current_mode(args[0].As<Int32>()->Value()).x);
}

static void krom_display_y(const FunctionCallbackInfo<Value> &args) {
	args.GetReturnValue().Set(kinc_display_current_mode(args[0].As<Int32>()->Value()).y);
}

static void krom_display_frequency(const FunctionCallbackInfo<Value> &args) {
	args.GetReturnValue().Set(kinc_display_current_mode(args[0].As<Int32>()->Value()).frequency);
}

static void krom_display_is_primary(const FunctionCallbackInfo<Value> &args) {
	args.GetReturnValue().Set(args[0].As<Int32>()->Value() == kinc_primary_display());
}

static void krom_write_storage(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	node::Utf8Value filename(env->isolate(), args[0]);

	Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[1]);
	auto store = buffer->GetBackingStore();

	kinc_file_writer_t writer;
	if (!kinc_file_writer_open(&writer, *filename)) return;
	kinc_file_writer_write(&writer, store->Data(), (int)store->ByteLength());
	kinc_file_writer_close(&writer);
}

static void krom_read_storage(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	node::Utf8Value filename(env->isolate(), args[0]);

	kinc_file_reader_t reader;
	if (!kinc_file_reader_open(&reader, *filename, KINC_FILE_TYPE_SAVE)) return;

	void *buffer = malloc(kinc_file_reader_size(&reader));

	kinc_file_reader_read(&reader, buffer, kinc_file_reader_size(&reader));
	kinc_file_reader_close(&reader);

	std::shared_ptr<v8::BackingStore> store = v8::ArrayBuffer::NewBackingStore(buffer, kinc_file_reader_size(&reader), do_not_actually_delete, nullptr);
	Local<ArrayBuffer> abuffer = ArrayBuffer::New(env->isolate(), store);

	args.GetReturnValue().Set(abuffer);
}

static void krom_create_render_target(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	int value1 = args[0].As<Int32>()->Value();
	int value2 = args[1].As<Int32>()->Value();
	int value3 = args[2].As<Int32>()->Value();
	int value4 = args[3].As<Int32>()->Value();
	int value5 = args[4].As<Int32>()->Value();

	kinc_g4_render_target_t *renderTarget = (kinc_g4_render_target_t *)malloc(sizeof(kinc_g4_render_target_t));
	kinc_g4_render_target_init(renderTarget, value1, value2, value3, false, (kinc_g4_render_target_format_t)value4, value5, 0);

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> obj = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	obj->SetInternalField(0, External::New(env->isolate(), renderTarget));

	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "width").ToLocalChecked(),
	         Int32::New(env->isolate(), renderTarget->width));
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "height").ToLocalChecked(),
	         Int32::New(env->isolate(), renderTarget->height));

	args.GetReturnValue().Set(obj);
}

static void krom_create_render_target_cube_map(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	int value1 = args[0].As<Int32>()->Value();
	int value2 = args[1].As<Int32>()->Value();
	int value3 = args[2].As<Int32>()->Value();
	int value4 = args[3].As<Int32>()->Value();

	kinc_g4_render_target_t *renderTarget = (kinc_g4_render_target_t *)malloc(sizeof(kinc_g4_render_target_t));
	kinc_g4_render_target_init_cube(renderTarget, value1, value2, false, (kinc_g4_render_target_format_t)value3, value4, 0);

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> obj = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	obj->SetInternalField(0, External::New(env->isolate(), renderTarget));

	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "width").ToLocalChecked(),
	         Int32::New(env->isolate(), renderTarget->width));
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "height").ToLocalChecked(),
	         Int32::New(env->isolate(), renderTarget->height));

	args.GetReturnValue().Set(obj);
}

static void krom_create_texture(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	int value1 = args[0].As<Int32>()->Value();
	int value2 = args[1].As<Int32>()->Value();
	int value3 = args[2].As<Int32>()->Value();

	kinc_g4_texture_t *texture = (kinc_g4_texture_t *)malloc(sizeof(kinc_g4_texture_t));
	kinc_g4_texture_init(texture, value1, value2, (kinc_image_format_t)value3);

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> obj = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	obj->SetInternalField(0, External::New(env->isolate(), texture));

	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "width").ToLocalChecked(), args[0]);
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "height").ToLocalChecked(), args[1]);
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "realWidth").ToLocalChecked(),
	         Int32::New(env->isolate(), texture->tex_width));
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "realHeight").ToLocalChecked(),
	         Int32::New(env->isolate(), texture->tex_height));

	args.GetReturnValue().Set(obj);
}

static void krom_create_texture_3d(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	int value1 = args[0].As<Int32>()->Value();
	int value2 = args[1].As<Int32>()->Value();
	int value3 = args[2].As<Int32>()->Value();
	int value4 = args[3].As<Int32>()->Value();

	kinc_g4_texture_t *texture = (kinc_g4_texture_t *)malloc(sizeof(kinc_g4_texture_t));
	kinc_g4_texture_init3d(texture, value1, value2, value3, (kinc_image_format_t)value4);

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> obj = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	obj->SetInternalField(0, External::New(env->isolate(), texture));

	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "width").ToLocalChecked(), args[0]);
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "height").ToLocalChecked(), args[1]);
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "depth").ToLocalChecked(), args[2]);
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "realWidth").ToLocalChecked(),
	         Int32::New(env->isolate(), texture->tex_width));
	obj->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "realHeight").ToLocalChecked(),
	         Int32::New(env->isolate(), texture->tex_height));

	args.GetReturnValue().Set(obj);
}

static void krom_create_texture_from_bytes(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
	auto store = buffer->GetBackingStore();

	int value2 = args[1].As<Int32>()->Value();
	int value3 = args[2].As<Int32>()->Value();
	int value4 = args[3].As<Int32>()->Value();
	bool readable = args[4].As<Boolean>()->Value();

	void *data = malloc(store->ByteLength());
	memcpy(data, store->Data(), store->ByteLength());

	kinc_image_t image;
	kinc_image_init_from_bytes(&image, data, value2, value3, (kinc_image_format_t)value4);

	kinc_g4_texture_t *texture = (kinc_g4_texture_t *)malloc(sizeof(kinc_g4_texture_t));
	kinc_g4_texture_init_from_image(texture, &image);

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> value = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	value->SetInternalField(0, External::New(env->isolate(), texture));

	value->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "width").ToLocalChecked(), Int32::New(env->isolate(), image.width));
	value->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "height").ToLocalChecked(), Int32::New(env->isolate(), image.height));
	value->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "realWidth").ToLocalChecked(),
	           Int32::New(env->isolate(), texture->tex_width));
	value->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "realHeight").ToLocalChecked(),
	           Int32::New(env->isolate(), texture->tex_height));

	if (readable) {
		kinc_image_t *imagePtr = (kinc_image_t *)malloc(sizeof(kinc_image_t));
		memcpy(imagePtr, &image, sizeof(image));

		Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
		templ->SetInternalFieldCount(1);

		Local<Object> imageObject = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
		imageObject->SetInternalField(0, External::New(env->isolate(), imagePtr));

		value->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "image").ToLocalChecked(), imageObject);
	}
	else {
		kinc_image_destroy(&image);
	}

	args.GetReturnValue().Set(value);
}

static void krom_create_texture_from_bytes_3d(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
	auto store = buffer->GetBackingStore();

	int value2 = args[1].As<Int32>()->Value();
	int value3 = args[2].As<Int32>()->Value();
	int value4 = args[3].As<Int32>()->Value();
	int value5 = args[4].As<Int32>()->Value();
	bool readable = args[5].As<Boolean>()->Value();

	void *data = malloc(store->ByteLength());
	memcpy(data, store->Data(), store->ByteLength());

	kinc_image_t image;
	kinc_image_init_from_bytes3d(&image, data, value2, value3, value4, (kinc_image_format_t)value5);

	kinc_g4_texture_t *texture = (kinc_g4_texture_t *)malloc(sizeof(kinc_g4_texture_t));
	kinc_g4_texture_init_from_image3d(texture, &image);

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> value = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	value->SetInternalField(0, External::New(env->isolate(), texture));

	value->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "width").ToLocalChecked(), Int32::New(env->isolate(), image.width));
	value->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "height").ToLocalChecked(), Int32::New(env->isolate(), image.height));
	value->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "depth").ToLocalChecked(), Int32::New(env->isolate(), image.depth));
	value->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "realWidth").ToLocalChecked(),
	           Int32::New(env->isolate(), texture->tex_width));
	value->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "realHeight").ToLocalChecked(),
	           Int32::New(env->isolate(), texture->tex_height));

	if (readable) {
		kinc_image_t *imagePtr = (kinc_image_t *)malloc(sizeof(kinc_image_t));
		memcpy(imagePtr, &image, sizeof(image));

		Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
		templ->SetInternalFieldCount(1);

		Local<Object> imageObject = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
		imageObject->SetInternalField(0, External::New(env->isolate(), imagePtr));

		value->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "image").ToLocalChecked(), imageObject);
	}
	else {
		kinc_image_destroy(&image);
	}

	args.GetReturnValue().Set(value);
}

static void krom_create_texture_from_encoded_bytes(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
	auto store = buffer->GetBackingStore();

	node::Utf8Value format(env->isolate(), args[1]);
	bool readable = args[2].As<Boolean>()->Value();

	size_t size = kinc_image_size_from_encoded_bytes(store->Data(), store->ByteLength(), *format);
	void *memory = malloc(size);
	kinc_image_t image;
	kinc_image_init_from_encoded_bytes(&image, memory, store->Data(), store->ByteLength(), *format);

	kinc_g4_texture_t *texture = (kinc_g4_texture_t *)malloc(sizeof(kinc_g4_texture_t));
	kinc_g4_texture_init_from_image(texture, &image);

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> value = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	value->SetInternalField(0, External::New(env->isolate(), texture));

	value->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "width").ToLocalChecked(), Int32::New(env->isolate(), image.width));
	value->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "height").ToLocalChecked(), Int32::New(env->isolate(), image.height));
	value->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "realWidth").ToLocalChecked(),
	           Int32::New(env->isolate(), texture->tex_width));
	value->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "realHeight").ToLocalChecked(),
	           Int32::New(env->isolate(), texture->tex_height));

	if (readable) {
		kinc_image_t *imagePtr = (kinc_image_t *)malloc(sizeof(kinc_image_t));
		memcpy(imagePtr, &image, sizeof(image));

		Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
		templ->SetInternalFieldCount(1);

		Local<Object> imageObject = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
		imageObject->SetInternalField(0, External::New(env->isolate(), imagePtr));

		value->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "image").ToLocalChecked(), imageObject);
	}
	else {
		kinc_image_destroy(&image);
		free(memory);
	}

	args.GetReturnValue().Set(value);
}

int formatByteSize(kinc_image_format_t format) {
	switch (format) {
	case KINC_IMAGE_FORMAT_RGBA128:
		return 16;
	case KINC_IMAGE_FORMAT_RGBA64:
		return 8;
	case KINC_IMAGE_FORMAT_RGB24:
		return 4;
	case KINC_IMAGE_FORMAT_A32:
		return 4;
	case KINC_IMAGE_FORMAT_A16:
		return 2;
	case KINC_IMAGE_FORMAT_GREY8:
		return 1;
	case KINC_IMAGE_FORMAT_RGBA32:
	case KINC_IMAGE_FORMAT_BGRA32:
	default:
		return 4;
	}
}

static void krom_get_texture_pixels(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_texture *texture = (kinc_g4_texture *)field->Value();

	Local<Object> imageObj = args[0]
	                             .As<Object>()
	                             ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "image").ToLocalChecked())
	                             .ToLocalChecked()
	                             .As<Object>();

	if (args[0]->IsNull() || args[0]->IsUndefined()) {
		return;
	}
	else {
		Local<External> field = Local<External>::Cast(imageObj->GetInternalField(0));
		kinc_image_t *image = (kinc_image_t *)field->Value();

		uint8_t *data = kinc_image_get_pixels(image);
		int byteLength = formatByteSize(texture->format) * texture->tex_width * texture->tex_height * texture->tex_depth;

		std::shared_ptr<v8::BackingStore> store = v8::ArrayBuffer::NewBackingStore(data, byteLength, do_not_actually_delete, nullptr);
		Local<ArrayBuffer> value = ArrayBuffer::New(env->isolate(), store);

		args.GetReturnValue().Set(value);
	}
}

static void krom_get_render_target_pixels(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_render_target_t *rt = (kinc_g4_render_target_t *)field->Value();

	Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[1]);
	auto store = buffer->GetBackingStore();

	kinc_g4_render_target_get_pixels(rt, (uint8_t *)store->Data()); // TODO: Create and return new array-buffer instead
}

static void krom_lock_texture(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_texture_t *texture = (kinc_g4_texture_t *)field->Value();

	uint8_t *tex = kinc_g4_texture_lock(texture);

	args[0].As<Object>()->Set(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "stride").ToLocalChecked(),
	                          Int32::New(env->isolate(), kinc_g4_texture_stride(texture)));

	std::shared_ptr<v8::BackingStore> store =
	    v8::ArrayBuffer::NewBackingStore(tex, kinc_g4_texture_stride(texture) * texture->tex_height * texture->tex_depth, do_not_actually_delete, nullptr);
	Local<ArrayBuffer> value = ArrayBuffer::New(env->isolate(), store);

	args.GetReturnValue().Set(value);
}

static void krom_unlock_texture(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_texture_t *texture = (kinc_g4_texture_t *)field->Value();
	kinc_g4_texture_unlock(texture);
}

static void krom_clear_texture(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_texture_t *texture = (kinc_g4_texture_t *)field->Value();

	int x = args[1].As<Int32>()->Value();
	int y = args[2].As<Int32>()->Value();
	int z = args[3].As<Int32>()->Value();
	int width = args[4].As<Int32>()->Value();
	int height = args[5].As<Int32>()->Value();
	int depth = args[6].As<Int32>()->Value();
	int color = args[7].As<Int32>()->Value();

	kinc_g4_texture_clear(texture, x, y, z, width, height, depth, color);
}

static void krom_generate_texture_mipmaps(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_texture_t *texture = (kinc_g4_texture_t *)field->Value();

	int levels = args[1].As<Int32>()->Value();
	kinc_g4_texture_generate_mipmaps(texture, levels);
}

static void krom_generate_render_target_mipmaps(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_render_target_t *rt = (kinc_g4_render_target_t *)field->Value();

	int levels = args[1].As<Int32>()->Value();
	kinc_g4_render_target_generate_mipmaps(rt, levels);
}

static void krom_set_mipmaps(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_texture_t *texture = (kinc_g4_texture_t *)field->Value();

	int length = args[1]
	                 .As<Object>()
	                 ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "length").ToLocalChecked())
	                 .ToLocalChecked()
	                 .As<Int32>()
	                 ->Value();

	for (int i = 0; i < length; ++i) {
		Local<Value> element = args[1].As<Object>()->Get(env->isolate()->GetCurrentContext(), i).ToLocalChecked();

		Local<Value> obj =
		    element.As<Object>()->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "texture_").ToLocalChecked()).ToLocalChecked();

		Local<External> field = Local<External>::Cast(obj.As<Object>()->GetInternalField(0));
		kinc_g4_texture_t *mipmap = (kinc_g4_texture_t *)field->Value();

		Local<Value> imageObj =
		    obj.As<Object>()->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "image").ToLocalChecked()).ToLocalChecked();

		if (!imageObj->IsNull() && !imageObj->IsUndefined()) {
			Local<External> field = Local<External>::Cast(imageObj.As<Object>()->GetInternalField(0));
			kinc_image_t *image = (kinc_image_t *)field->Value();
			kinc_g4_texture_set_mipmap(texture, image, i + 1);
		}
	}
}

static void krom_set_depth_stencil_from(const FunctionCallbackInfo<Value> &args) {
	Local<External> field1 = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_render_target_t *renderTarget = (kinc_g4_render_target_t *)field1->Value();

	Local<External> field2 = Local<External>::Cast(args[1].As<Object>()->GetInternalField(0));
	kinc_g4_render_target_t *sourceTarget = (kinc_g4_render_target_t *)field2->Value();

	kinc_g4_render_target_set_depth_stencil_from(renderTarget, sourceTarget);
}

static void krom_viewport(const FunctionCallbackInfo<Value> &args) {
	int x = args[0].As<Int32>()->Value();
	int y = args[1].As<Int32>()->Value();
	int w = args[2].As<Int32>()->Value();
	int h = args[3].As<Int32>()->Value();

	kinc_g4_viewport(x, y, w, h);
}

static void krom_scissor(const FunctionCallbackInfo<Value> &args) {
	int x = args[0].As<Int32>()->Value();
	int y = args[1].As<Int32>()->Value();
	int w = args[2].As<Int32>()->Value();
	int h = args[3].As<Int32>()->Value();

	kinc_g4_scissor(x, y, w, h);
}

static void krom_disable_scissor(const FunctionCallbackInfo<Value> &args) {
	kinc_g4_disable_scissor();
}

static void krom_render_targets_inverted_y(const FunctionCallbackInfo<Value> &args) {
	args.GetReturnValue().Set(kinc_g4_render_targets_inverted_y());
}

static void krom_begin(const FunctionCallbackInfo<Value> &args) {
	if (args[0]->IsNull() || args[0]->IsUndefined()) {
		kinc_g4_restore_render_target();
	}
	else {
		node::Environment *env = node::Environment::GetCurrent(args);

		Local<Value> rt = args[0]
		                      .As<Object>()
		                      ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "renderTarget_").ToLocalChecked())
		                      .ToLocalChecked();

		Local<External> field = Local<External>::Cast(rt.As<Object>()->GetInternalField(0));
		kinc_g4_render_target_t *renderTarget = (kinc_g4_render_target_t *)field->Value();

		if (args[1]->IsNull() || args[1]->IsUndefined()) {
			kinc_g4_set_render_targets(&renderTarget, 1);
		}
		else {
			kinc_g4_render_target_t *renderTargets[8] = {renderTarget, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

			int length = args[1]
			                 .As<Object>()
			                 ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "length").ToLocalChecked())
			                 .ToLocalChecked()
			                 .As<Int32>()
			                 ->Value();

			if (length > 7) {
				length = 7;
			}

			for (int i = 0; i < length; ++i) {
				Local<Value> element = args[1].As<Object>()->Get(env->isolate()->GetCurrentContext(), i).ToLocalChecked();

				Local<Value> obj = element.As<Object>()
				                       ->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "renderTarget_").ToLocalChecked())
				                       .ToLocalChecked();

				Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
				kinc_g4_render_target_t *art = (kinc_g4_render_target_t *)field->Value();

				renderTargets[i + 1] = art;
			}
			kinc_g4_set_render_targets(renderTargets, length + 1);
		}
	}
}

static void krom_begin_face(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<Value> rt =
	    args[0].As<Object>()->Get(env->isolate()->GetCurrentContext(), String::NewFromUtf8(env->isolate(), "renderTarget_").ToLocalChecked()).ToLocalChecked();

	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_g4_render_target_t *renderTarget = (kinc_g4_render_target_t *)field->Value();

	int face = args[1].As<Int32>()->Value();
	kinc_g4_set_render_target_face(renderTarget, face);
}

static void krom_end(const FunctionCallbackInfo<Value> &args) {}

static void krom_file_save_bytes(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	node::Utf8Value filename(env->isolate(), args[0]);

	Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[1]);
	auto store = buffer->GetBackingStore();

	FILE *file = fopen(*filename, "wb");
	if (file == nullptr) {
		return;
	}
	fwrite(store->Data(), 1, (int)store->ByteLength(), file);
	fclose(file);
}

static void krom_sys_command(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	node::Utf8Value command(env->isolate(), args[0]);
	int result = system(*command);
	args.GetReturnValue().Set(result);
}

// TODO: Remove this if possible
static void krom_save_path(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	args.GetReturnValue().Set(String::NewFromUtf8(env->isolate(), kinc_internal_save_path()).ToLocalChecked());
}

static void krom_get_arg_count(const FunctionCallbackInfo<Value> &args) {
	args.GetReturnValue().Set(_argc);
}

static void krom_get_arg(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	int index = args[0].As<Int32>()->Value();
	args.GetReturnValue().Set(String::NewFromUtf8(env->isolate(), _argv[index]).ToLocalChecked());
}

static void krom_get_files_location(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	args.GetReturnValue().Set(String::NewFromUtf8(env->isolate(), kinc_internal_get_files_location()).ToLocalChecked());
}

static void krom_set_bool_compute(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_constant_location_t *location = (kinc_compute_constant_location_t *)field->Value();
	kinc_compute_set_bool(*location, args[1].As<Int32>()->Value() != 0);
}

static void krom_set_int_compute(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_constant_location_t *location = (kinc_compute_constant_location_t *)field->Value();
	kinc_compute_set_int(*location, args[1].As<Int32>()->Value());
}

static void krom_set_float_compute(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_constant_location_t *location = (kinc_compute_constant_location_t *)field->Value();
	kinc_compute_set_float(*location, (float)args[1].As<Number>()->Value());
}

static void krom_set_float2_compute(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_constant_location_t *location = (kinc_compute_constant_location_t *)field->Value();
	kinc_compute_set_float2(*location, (float)args[1].As<Number>()->Value(), (float)args[2].As<Number>()->Value());
}

static void krom_set_float3_compute(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_constant_location_t *location = (kinc_compute_constant_location_t *)field->Value();
	kinc_compute_set_float3(*location, (float)args[1].As<Number>()->Value(), (float)args[2].As<Number>()->Value(), (float)args[3].As<Number>()->Value());
}

static void krom_set_float4_compute(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_constant_location_t *location = (kinc_compute_constant_location_t *)field->Value();
	kinc_compute_set_float4(*location, (float)args[1].As<Number>()->Value(), (float)args[2].As<Number>()->Value(), (float)args[3].As<Number>()->Value(),
	                        (float)args[4].As<Number>()->Value());
}

static void krom_set_floats_compute(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_constant_location_t *location = (kinc_compute_constant_location_t *)field->Value();

	Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[1]);
	auto store = buffer->GetBackingStore();

	float *from = (float *)store->Data();

	kinc_compute_set_floats(*location, from, int(store->ByteLength() / 4));
}

static void krom_set_matrix_compute(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_constant_location_t *location = (kinc_compute_constant_location_t *)field->Value();

	Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[1]);
	auto store = buffer->GetBackingStore();

	float *from = (float *)store->Data();
	kinc_matrix4x4_t m;
	kinc_matrix4x4_set(&m, 0, 0, from[0]);
	kinc_matrix4x4_set(&m, 1, 0, from[1]);
	kinc_matrix4x4_set(&m, 2, 0, from[2]);
	kinc_matrix4x4_set(&m, 3, 0, from[3]);
	kinc_matrix4x4_set(&m, 0, 1, from[4]);
	kinc_matrix4x4_set(&m, 1, 1, from[5]);
	kinc_matrix4x4_set(&m, 2, 1, from[6]);
	kinc_matrix4x4_set(&m, 3, 1, from[7]);
	kinc_matrix4x4_set(&m, 0, 2, from[8]);
	kinc_matrix4x4_set(&m, 1, 2, from[9]);
	kinc_matrix4x4_set(&m, 2, 2, from[10]);
	kinc_matrix4x4_set(&m, 3, 2, from[11]);
	kinc_matrix4x4_set(&m, 0, 3, from[12]);
	kinc_matrix4x4_set(&m, 1, 3, from[13]);
	kinc_matrix4x4_set(&m, 2, 3, from[14]);
	kinc_matrix4x4_set(&m, 3, 3, from[15]);

	kinc_compute_set_matrix4(*location, &m);
}

static void krom_set_matrix3_compute(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_constant_location_t *location = (kinc_compute_constant_location_t *)field->Value();

	Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[1]);
	auto store = buffer->GetBackingStore();

	float *from = (float *)store->Data();
	kinc_matrix3x3_t m;
	kinc_matrix3x3_set(&m, 0, 0, from[0]);
	kinc_matrix3x3_set(&m, 1, 0, from[1]);
	kinc_matrix3x3_set(&m, 2, 0, from[2]);
	kinc_matrix3x3_set(&m, 0, 1, from[3]);
	kinc_matrix3x3_set(&m, 1, 1, from[4]);
	kinc_matrix3x3_set(&m, 2, 1, from[5]);
	kinc_matrix3x3_set(&m, 0, 2, from[6]);
	kinc_matrix3x3_set(&m, 1, 2, from[7]);
	kinc_matrix3x3_set(&m, 2, 2, from[8]);

	kinc_compute_set_matrix3(*location, &m);
}

static void krom_set_texture_compute(const FunctionCallbackInfo<Value> &args) {
	Local<External> field1 = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_texture_unit_t *unit = (kinc_compute_texture_unit_t *)field1->Value();

	Local<External> field2 = Local<External>::Cast(args[1].As<Object>()->GetInternalField(0));
	kinc_g4_texture_t *texture = (kinc_g4_texture_t *)field2->Value();

	int access = args[2].As<Int32>()->Value();

	kinc_compute_set_texture(*unit, texture, (kinc_compute_access_t)access);
}

static void krom_set_render_target_compute(const FunctionCallbackInfo<Value> &args) {
	Local<External> field1 = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_texture_unit_t *unit = (kinc_compute_texture_unit_t *)field1->Value();

	Local<External> field2 = Local<External>::Cast(args[1].As<Object>()->GetInternalField(0));
	kinc_g4_render_target_t *renderTarget = (kinc_g4_render_target_t *)field2->Value();

	int access = args[2].As<Int32>()->Value();

	kinc_compute_set_render_target(*unit, renderTarget, (kinc_compute_access_t)access);
}

static void krom_set_sampled_texture_compute(const FunctionCallbackInfo<Value> &args) {
	Local<External> field1 = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_texture_unit_t *unit = (kinc_compute_texture_unit_t *)field1->Value();

	Local<External> field2 = Local<External>::Cast(args[1].As<Object>()->GetInternalField(0));
	kinc_g4_texture_t *texture = (kinc_g4_texture_t *)field2->Value();

	kinc_compute_set_sampled_texture(*unit, texture);
}

static void krom_set_sampled_render_target_compute(const FunctionCallbackInfo<Value> &args) {
	Local<External> field1 = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_texture_unit_t *unit = (kinc_compute_texture_unit_t *)field1->Value();

	Local<External> field2 = Local<External>::Cast(args[1].As<Object>()->GetInternalField(0));
	kinc_g4_render_target_t *renderTarget = (kinc_g4_render_target_t *)field2->Value();

	kinc_compute_set_sampled_render_target(*unit, renderTarget);
}

static void krom_set_sampled_depth_texture_compute(const FunctionCallbackInfo<Value> &args) {
	Local<External> field1 = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_texture_unit_t *unit = (kinc_compute_texture_unit_t *)field1->Value();

	Local<External> field2 = Local<External>::Cast(args[1].As<Object>()->GetInternalField(0));
	kinc_g4_render_target_t *renderTarget = (kinc_g4_render_target_t *)field2->Value();

	kinc_compute_set_sampled_depth_from_render_target(*unit, renderTarget);
}

static void krom_set_texture_parameters_compute(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_texture_unit_t *unit = (kinc_compute_texture_unit_t *)field->Value();

	int u = args[1].As<Int32>()->Value();
	int v = args[2].As<Int32>()->Value();
	int min = args[3].As<Int32>()->Value();
	int max = args[4].As<Int32>()->Value();
	int mip = args[5].As<Int32>()->Value();

	kinc_compute_set_texture_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_U, (kinc_g4_texture_addressing_t)u);
	kinc_compute_set_texture_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_V, (kinc_g4_texture_addressing_t)v);
	kinc_compute_set_texture_minification_filter(*unit, (kinc_g4_texture_filter_t)min);
	kinc_compute_set_texture_magnification_filter(*unit, (kinc_g4_texture_filter_t)max);
	kinc_compute_set_texture_mipmap_filter(*unit, (kinc_g4_mipmap_filter_t)mip);
}

static void krom_set_texture_3d_parameters_compute(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_texture_unit_t *unit = (kinc_compute_texture_unit_t *)field->Value();

	int u = args[1].As<Int32>()->Value();
	int v = args[2].As<Int32>()->Value();
	int w = args[3].As<Int32>()->Value();
	int min = args[4].As<Int32>()->Value();
	int max = args[5].As<Int32>()->Value();
	int mip = args[6].As<Int32>()->Value();

	kinc_compute_set_texture3d_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_U, (kinc_g4_texture_addressing_t)u);
	kinc_compute_set_texture3d_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_V, (kinc_g4_texture_addressing_t)v);
	kinc_compute_set_texture3d_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_W, (kinc_g4_texture_addressing_t)w);
	kinc_compute_set_texture3d_minification_filter(*unit, (kinc_g4_texture_filter_t)min);
	kinc_compute_set_texture3d_magnification_filter(*unit, (kinc_g4_texture_filter_t)max);
	kinc_compute_set_texture3d_mipmap_filter(*unit, (kinc_g4_mipmap_filter_t)mip);
}

static void krom_max_bound_textures(const FunctionCallbackInfo<Value> &args) {
	args.GetReturnValue().Set(kinc_g4_max_bound_textures());
}

static void krom_set_shader_compute(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_shader_t *shader = (kinc_compute_shader_t *)field->Value();
	kinc_compute_set_shader(shader);
}

static void krom_create_shader_compute(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
	auto store = buffer->GetBackingStore();

	kinc_compute_shader_t *shader = (kinc_compute_shader_t *)malloc(sizeof(kinc_compute_shader_t));
	kinc_compute_shader_init(shader, store->Data(), (int)store->ByteLength());

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> obj = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	obj->SetInternalField(0, External::New(env->isolate(), shader));

	args.GetReturnValue().Set(obj);
}

static void krom_delete_shader_compute(const FunctionCallbackInfo<Value> &args) {
	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_shader_t *shader = (kinc_compute_shader_t *)field->Value();
	kinc_compute_shader_destroy(shader);
	free(shader);
}

static void krom_get_constant_location_compute(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_shader_t *shader = (kinc_compute_shader_t *)field->Value();

	node::Utf8Value name(env->isolate(), args[1]);

	kinc_compute_constant_location_t location = kinc_compute_shader_get_constant_location(shader, *name);
	kinc_compute_constant_location_t *heapLocation = (kinc_compute_constant_location_t *)malloc(sizeof(kinc_compute_constant_location_t));
	memcpy(heapLocation, &location, sizeof(kinc_compute_constant_location_t));

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> value = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	value->SetInternalField(0, External::New(env->isolate(), heapLocation));

	args.GetReturnValue().Set(value);
}

static void krom_get_texture_unit_compute(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);

	Local<External> field = Local<External>::Cast(args[0].As<Object>()->GetInternalField(0));
	kinc_compute_shader_t *shader = (kinc_compute_shader_t *)field->Value();

	node::Utf8Value name(env->isolate(), args[1]);

	kinc_compute_texture_unit_t unit = kinc_compute_shader_get_texture_unit(shader, *name);
	kinc_compute_texture_unit_t *heapUnit = (kinc_compute_texture_unit_t *)malloc(sizeof(kinc_compute_texture_unit_t));
	memcpy(heapUnit, &unit, sizeof(kinc_compute_texture_unit_t));

	Local<ObjectTemplate> templ = ObjectTemplate::New(env->isolate());
	templ->SetInternalFieldCount(1);

	Local<Object> value = templ->NewInstance(env->isolate()->GetCurrentContext()).ToLocalChecked();
	value->SetInternalField(0, External::New(env->isolate(), heapUnit));

	args.GetReturnValue().Set(value);
}

static void krom_compute(const FunctionCallbackInfo<Value> &args) {
	int x = args[0].As<Int32>()->Value();
	int y = args[1].As<Int32>()->Value();
	int z = args[2].As<Int32>()->Value();
	kinc_compute(x, y, z);
}

#if 0
JsSourceContext cookie = 1234;
JsValueRef script, source;

void initKrom(char* scriptfile) {
#ifdef KORE_WINDOWS
  AttachProcess(GetModuleHandle(nullptr));
#else
  AttachProcess(nullptr);
#endif

#ifdef NDEBUG
  JsCreateRuntime(JsRuntimeAttributeEnableIdleProcessing, nullptr, &runtime);
#else
  JsCreateRuntime(JsRuntimeAttributeAllowScriptInterrupt, nullptr, &runtime);
#endif

  JsCreateContext(runtime, &context);
  JsAddRef(context, nullptr);

  JsSetCurrentContext(context);

  bindFunctions();
  bindWorkerClass();

  JsCreateExternalArrayBuffer(
      (void*)scriptfile,
      serialized ? serializedLength : (unsigned int)strlen(scriptfile),
      nullptr,
      nullptr,
      &script);
  JsCreateString("krom.js", strlen("krom.js"), &source);
}

void startKrom(char* scriptfile) {
  JsValueRef result;
  if (serialized) {
    JsRunSerialized(
        script,
        [](JsSourceContext sourceContext,
           JsValueRef* scriptBuffer,
           JsParseScriptAttributes* parseAttributes) {
          fprintf(stderr, "krom.bin does not match this Krom version");
          return false;
        },
        cookie,
        source,
        &result);
  } else {
    JsRun(script, cookie, source, JsParseScriptAttributeNone, &result);
  }
}

bool codechanged = false;

void parseCode();

void runJS() {
  if (debugMode) {
    Message message = receiveMessage();
    handleDebugMessage(message, false);
  }

  if (codechanged) {
    parseCode();
    codechanged = false;
  }

  JsValueRef undef;
  JsGetUndefinedValue(&undef);
  JsValueRef result;
  JsCallFunction(updateFunction, &undef, 1, &result);

  handleWorkerMessages();

  bool except;
  JsHasException(&except);
  if (except) {
    JsValueRef meta;
    JsValueRef exceptionObj;
    JsGetAndClearExceptionWithMetadata(&meta);
    JsGetProperty(meta, getId("exception"), &exceptionObj);
    char buf[2048];
    size_t length;

    sendLogMessage("Uncaught exception:");
    JsValueRef sourceObj;
    JsGetProperty(meta, getId("source"), &sourceObj);
    JsCopyString(sourceObj, nullptr, 0, &length);
    if (length < 2048) {
      JsCopyString(sourceObj, buf, 2047, &length);
      buf[length] = 0;
      sendLogMessage("%s", buf);

      JsValueRef columnObj;
      JsGetProperty(meta, getId("column"), &columnObj);
      int column;
      JsNumberToInt(columnObj, &column);
      for (int i = 0; i < column; i++)
        if (buf[i] != '\t') buf[i] = ' ';
      buf[column] = '^';
      buf[column + 1] = 0;
      sendLogMessage("%s", buf);
    }

    JsValueRef stackObj;
    JsGetProperty(exceptionObj, getId("stack"), &stackObj);
    JsCopyString(stackObj, nullptr, 0, &length);
    if (length < 2048) {
      JsCopyString(stackObj, buf, 2047, &length);
      buf[length] = 0;
      sendLogMessage("%s\n", buf);
    }
  }
}

void serializeScript(char* code, char* outpath) {
#ifdef KORE_WINDOWS
  AttachProcess(GetModuleHandle(nullptr));
#else
  AttachProcess(nullptr);
#endif
  JsCreateRuntime(JsRuntimeAttributeNone, nullptr, &runtime);
  JsContextRef context;
  JsCreateContext(runtime, &context);
  JsSetCurrentContext(context);

  JsValueRef codeObj, bufferObj;
  JsCreateExternalArrayBuffer(
      (void*)code, (unsigned int)strlen(code), nullptr, nullptr, &codeObj);
  JsSerialize(codeObj, &bufferObj, JsParseScriptAttributeNone);
  Kore::u8* buffer;
  unsigned bufferLength;
  JsGetArrayBufferStorage(bufferObj, &buffer, &bufferLength);

  FILE* file = fopen(outpath, "wb");
  if (file == nullptr) return;
  fwrite(buffer, 1, (int)bufferLength, file);
  fclose(file);
}
#endif
/*void endKrom() {
    JsSetCurrentContext(JS_INVALID_REFERENCE);
    JsDisposeRuntime(runtime);
}*/

static node::Environment *globalEnv;

void updateAudio(kinc_a2_buffer_t *buffer, int samples) {
	kinc_mutex_lock(&audioMutex);
	audioSamples += samples;
	kinc_mutex_unlock(&audioMutex);
}

static void runV8() {
	/*if (messageLoopPaused)
	    return;

	if (codechanged) {
	    parseCode();
	    codechanged = false;
	}*/

	/*v8::Locker locker{isolate};

	Isolate::Scope isolate_scope(isolate);
	v8::MicrotasksScope microtasks_scope(isolate, v8::MicrotasksScope::kRunMicrotasks);
	HandleScope handle_scope(isolate);
	Local<Context> context = Local<Context>::New(isolate, globalContext);
	Context::Scope context_scope(context);*/

	TryCatch try_catch(globalEnv->isolate());
	Local<v8::Function> func = Local<v8::Function>::New(globalEnv->isolate(), updateFunction);
	Local<Value> result;

	//**if (debugMode) v8inspector->willExecuteScript(context, func->ScriptId());
	Local<Context> context = globalEnv->isolate()->GetCurrentContext();
	if (!func->Call(context, context->Global(), 0, NULL).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
	//**if (debugMode) v8inspector->didExecuteScript(context);
}

void update() {
	/*if (enableSound) {
	    kinc_a2_update();

	    kinc_mutex_lock(&audioMutex);
	    if (audioSamples > 0) {
	        JsValueRef args[2];
	        JsGetUndefinedValue(&args[0]);
	        JsIntToNumber(audioSamples, &args[1]);
	        JsValueRef result;
	        JsCallFunction(audioFunction, args, 2, &result);
	        audioSamples = 0;
	    }
	    kinc_mutex_unlock(&audioMutex);
	}*/

	kinc_g4_begin(0);

	runV8();

	kinc_g4_end(0);
	kinc_g4_swap_buffers();
}

void dropFiles(wchar_t *filePath) {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), dropFilesFunction);
	Local<Value> result;
	const int argc = 1;
	Local<Value> argv[argc];
	if (sizeof(wchar_t) == 2) {
		argv[0] = {String::NewFromTwoByte(globalEnv->isolate(), (const uint16_t *)filePath).ToLocalChecked()};
	}
	else {
		size_t len = wcslen(filePath);
		uint16_t *str = new uint16_t[len + 1];
		for (int i = 0; i < len; i++) str[i] = filePath[i];
		str[len] = 0;
		argv[0] = {String::NewFromTwoByte(globalEnv->isolate(), str).ToLocalChecked()};
		delete str;
	}
	if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
}

char cutCopyString[4096];

char *copy() {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), copyFunction);
	Local<Value> result;
	if (!func->Call(context, context->Global(), 0, NULL).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
	String::Utf8Value cutCopyString(globalEnv->isolate(), result);
	return *cutCopyString;
}

char *cut() {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), cutFunction);
	Local<Value> result;
	if (!func->Call(context, context->Global(), 0, NULL).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
	String::Utf8Value cutCopyString(globalEnv->isolate(), result);
	return *cutCopyString;
}

void paste(char *data) {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), pasteFunction);
	Local<Value> result;
	const int argc = 1;
	Local<Value> argv[argc] = {String::NewFromUtf8(globalEnv->isolate(), data).ToLocalChecked()};
	if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
}

void foreground() {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), foregroundFunction);
	Local<Value> result;
	if (!func->Call(context, context->Global(), 0, NULL).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
}

void resume() {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), resumeFunction);
	Local<Value> result;
	if (!func->Call(context, context->Global(), 0, NULL).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
}

void pause() {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), pauseFunction);
	Local<Value> result;
	if (!func->Call(context, context->Global(), 0, NULL).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
}

void background() {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), backgroundFunction);
	Local<Value> result;
	if (!func->Call(context, context->Global(), 0, NULL).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
}

void shutdown() {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), shutdownFunction);
	Local<Value> result;
	if (!func->Call(context, context->Global(), 0, NULL).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
}

void keyDown(int code) {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), keyboardDownFunction);
	Local<Value> result;
	const int argc = 1;
	Local<Value> argv[argc] = {Int32::New(globalEnv->isolate(), (int)code)};
	if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
}

void keyUp(int code) {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), keyboardUpFunction);
	Local<Value> result;
	const int argc = 1;
	Local<Value> argv[argc] = {Int32::New(globalEnv->isolate(), (int)code)};
	if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
}

void keyPress(unsigned int character) {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), keyboardPressFunction);
	Local<Value> result;
	const int argc = 1;
	Local<Value> argv[argc] = {Int32::New(globalEnv->isolate(), (int)character)};
	if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
}

void mouseMove(int window, int x, int y, int mx, int my) {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), mouseMoveFunction);
	Local<Value> result;
	const int argc = 4;
	Local<Value> argv[argc] = {Int32::New(globalEnv->isolate(), x), Int32::New(globalEnv->isolate(), y), Int32::New(globalEnv->isolate(), mx),
	                           Int32::New(globalEnv->isolate(), my)};
	if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
}

void mouseDown(int window, int button, int x, int y) {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), mouseDownFunction);
	Local<Value> result;
	const int argc = 3;
	Local<Value> argv[argc] = {Int32::New(globalEnv->isolate(), button), Int32::New(globalEnv->isolate(), x), Int32::New(globalEnv->isolate(), y)};
	if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
}

void mouseUp(int window, int button, int x, int y) {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), mouseUpFunction);
	Local<Value> result;
	const int argc = 3;
	Local<Value> argv[argc] = {Int32::New(globalEnv->isolate(), button), Int32::New(globalEnv->isolate(), x), Int32::New(globalEnv->isolate(), y)};
	if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
}

void mouseWheel(int window, int delta) {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), mouseWheelFunction);
	Local<Value> result;
	const int argc = 1;
	Local<Value> argv[argc] = {Int32::New(globalEnv->isolate(), delta)};
	if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
}

void penDown(int window, int x, int y, float pressure) {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), penDownFunction);
	Local<Value> result;
	const int argc = 3;
	Local<Value> argv[argc] = {Int32::New(globalEnv->isolate(), x), Int32::New(globalEnv->isolate(), y), Number::New(globalEnv->isolate(), pressure)};
	if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
}

void penUp(int window, int x, int y, float pressure) {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), penUpFunction);
	Local<Value> result;
	const int argc = 3;
	Local<Value> argv[argc] = {Int32::New(globalEnv->isolate(), x), Int32::New(globalEnv->isolate(), y), Number::New(globalEnv->isolate(), pressure)};
	if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
}

void penMove(int window, int x, int y, float pressure) {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), penMoveFunction);
	Local<Value> result;
	const int argc = 3;
	Local<Value> argv[argc] = {Int32::New(globalEnv->isolate(), x), Int32::New(globalEnv->isolate(), y), Number::New(globalEnv->isolate(), pressure)};
	if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
}

void gamepadAxis(int gamepad, int axis, float value) {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), gamepadAxisFunction);
	Local<Value> result;
	const int argc = 3;
	Local<Value> argv[argc] = {Int32::New(globalEnv->isolate(), gamepad), Int32::New(globalEnv->isolate(), axis), Number::New(globalEnv->isolate(), value)};
	if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
}

void gamepadButton(int gamepad, int button, float value) {
	v8::Locker locker{globalEnv->isolate()};

	Isolate::Scope isolate_scope(globalEnv->isolate());
	HandleScope handle_scope(globalEnv->isolate());
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(globalEnv->isolate(), globalEnv->isolate()->GetCurrentContext());
	Context::Scope context_scope(context);

	TryCatch try_catch(globalEnv->isolate());
	v8::Local<v8::Function> func = v8::Local<v8::Function>::New(globalEnv->isolate(), gamepadButtonFunction);
	Local<Value> result;
	const int argc = 3;
	Local<Value> argv[argc] = {Int32::New(globalEnv->isolate(), gamepad), Int32::New(globalEnv->isolate(), button), Number::New(globalEnv->isolate(), value)};
	if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
		v8::String::Utf8Value stack_trace(globalEnv->isolate(), try_catch.StackTrace(context).ToLocalChecked());
		sendLogMessage("Trace: %s", *stack_trace);
	}
}

bool startsWith(std::string str, std::string start) {
	return str.substr(0, start.size()) == start;
}

bool endsWith(std::string str, std::string end) {
	if (str.size() < end.size()) return false;
	for (size_t i = str.size() - end.size(); i < str.size(); ++i) {
		if (str[i] != end[i - (str.size() - end.size())]) return false;
	}
	return true;
}

std::string replaceAll(std::string str, const std::string &from, const std::string &to) {
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
		str.replace(start_pos, from.length(), to);
		start_pos += to.length();
	}
	return str;
}

#if 0
std::string assetsdir;
std::string kromjs;

struct Func {
	std::string name;
	std::vector<std::string> parameters;
	std::string body;
};

struct Klass {
	std::string name;
	std::string internal_name;
	std::string parent;
	std::string interfaces;
	std::map<std::string, Func *> methods;
	std::map<std::string, Func *> functions;
};

std::map<std::string, Klass *> classes;

enum ParseMode { ParseRegular, ParseMethods, ParseMethod, ParseFunction, ParseConstructor };

void patchCode(const char *newScript) {
	JsSetCurrentContext(context);

	JsCreateExternalArrayBuffer((void *)newScript, serialized ? serializedLength : (unsigned int)strlen(newScript), nullptr, nullptr, &script);
	JsCreateString("krom.js", strlen("krom.js"), &source);
	JsValueRef result;
	JsRun(script, cookie, source, JsParseScriptAttributeNone, &result);
}

void parseCode() {
	int types = 0;
	ParseMode mode = ParseRegular;
	Klass *currentClass = nullptr;
	Func *currentFunction = nullptr;
	std::string currentBody;
	int brackets = 1;

	std::ifstream infile(kromjs.c_str());
	std::string line;
	while (std::getline(infile, line)) {
		switch (mode) {
		case ParseRegular: {
			if (line.find("__super__ =") != std::string::npos) {
				size_t first = line.find_last_of(' = ');
				size_t last = line.find_last_of(';');
				currentClass->parent = line.substr(first + 1, last - first - 1);
			}
			else if (line.find("__interfaces__ =") != std::string::npos) {
				size_t first = line.find_last_of(' = ');
				size_t last = line.find_last_of(';');
				currentClass->interfaces = line.substr(first + 1, last - first - 1);
			}
			else if (endsWith(line, ".prototype = {") || line.find(".prototype = $extend(") != std::string::npos) { // parse methods
				mode = ParseMethods;
			}
			else if (line.find("$hxClasses[\"") != std::string::npos) {
				size_t first = line.find('\"');
				size_t last = line.find_last_of('\"');
				std::string name = line.substr(first + 1, last - first - 1);
				first = line.find('var') + 1;
				last = line.find('=', first + 1) - 1;
				std::string internal_name = line.substr(first + 1, last - first - 1);
				if (classes.find(internal_name) == classes.end()) {
					currentClass = new Klass;
					currentClass->name = name;
					currentClass->interfaces = "";
					currentClass->parent = "";
					currentClass->internal_name = internal_name;
					classes[internal_name] = currentClass;
					++types;
				}
				else {
					currentClass = classes[internal_name];
					currentClass->name = name;
				}
				// constructor
				if (line.find(" = function(") != std::string::npos) {
					if (currentClass->methods.find(internal_name) == currentClass->methods.end()) {
						currentFunction = new Func;
						currentFunction->name = internal_name;
						first = line.find('(') + 1;
						last = line.find_last_of(')');
						size_t last_param_start = first;
						for (size_t i = first; i <= last; ++i) {
							if (line[i] == ',') {
								currentFunction->parameters.push_back(line.substr(last_param_start, i - last_param_start));
								last_param_start = i + 1;
							}
							if (line[i] == ')') {
								currentFunction->parameters.push_back(line.substr(last_param_start, i - last_param_start));
								break;
							}
						}
						currentClass->methods[internal_name] = currentFunction;
					}
					else {
						currentFunction = currentClass->methods[internal_name];
					}
					if (line.find("};") == std::string::npos) {
						mode = ParseConstructor;
						currentBody = "";
						brackets = 1;
					}
				}
			}
			else if (line.find(" = function(") != std::string::npos && line.find("if") == std::string::npos) {
				if (line.find("var ") == std::string::npos) {
					size_t first = 0;
					size_t last = line.find('.');
					if (last == std::string::npos) {
						last = line.find('[');
					}
					std::string internal_name = line.substr(first, last - first);
					currentClass = classes[internal_name];

					first = line.find('.') + 1;
					last = line.find(' ');
					std::string methodname = line.substr(first, last - first);
					if (currentClass->methods.find(methodname) == currentClass->methods.end()) {
						currentFunction = new Func;
						currentFunction->name = methodname;
						first = line.find('(') + 1;
						last = line.find_last_of(')');
						size_t last_param_start = first;
						for (size_t i = first; i <= last; ++i) {
							if (line[i] == ',') {
								currentFunction->parameters.push_back(line.substr(last_param_start, i - last_param_start));
								last_param_start = i + 1;
							}
							if (line[i] == ')') {
								currentFunction->parameters.push_back(line.substr(last_param_start, i - last_param_start));
								break;
							}
						}
						currentClass->methods[methodname] = currentFunction;
					}
					else {
						currentFunction = currentClass->methods[methodname];
					}
					mode = ParseFunction;
					currentBody = "";
					brackets = 1;
				}
			}
			break;
		}
		case ParseMethods: {
			if (endsWith(line, "{")) {
				size_t first = 0;
				while (line[first] == ' ' || line[first] == '\t' || line[first] == ',') {
					++first;
				}
				size_t last = line.find(':');
				std::string methodname = line.substr(first, last - first);
				if (currentClass->methods.find(methodname) == currentClass->methods.end()) {
					currentFunction = new Func;
					currentFunction->name = methodname;
					first = line.find('(') + 1;
					if (first == std::string::npos) {
						first = 0;
					}
					last = line.find_last_of(')');
					if (last == std::string::npos) {
						last = 0;
					}
					size_t last_param_start = first;
					for (size_t i = first; i <= last; ++i) {
						if (line[i] == ',') {
							currentFunction->parameters.push_back(line.substr(last_param_start, i - last_param_start));
							last_param_start = i + 1;
						}
						if (line[i] == ')') {
							currentFunction->parameters.push_back(line.substr(last_param_start, i - last_param_start));
							break;
						}
					}
					currentClass->methods[methodname] = currentFunction;
				}
				else {
					currentFunction = currentClass->methods[methodname];
				}
				mode = ParseMethod;
				currentBody = "";
				brackets = 1;
			}
			else if (endsWith(line, "};") || endsWith(line, "});")) { // Base or extended class
				mode = ParseRegular;
			}
			break;
		}
		case ParseMethod: {
			brackets += std::count(line.begin(), line.end(), '{');
			brackets -= std::count(line.begin(), line.end(), '}');
			if (brackets > 0) {
				currentBody += line + " ";
			}
			else {
				if (currentFunction->body == "") {
					currentFunction->body = currentBody;
				}
				else if (currentFunction->body != currentBody) {
					currentFunction->body = currentBody;

					std::string script;
					script += currentClass->internal_name;
					script += ".prototype.";
					script += currentFunction->name;
					script += " = new Function([";
					for (size_t i = 0; i < currentFunction->parameters.size(); ++i) {
						script += "\"" + currentFunction->parameters[i] + "\"";
						if (i < currentFunction->parameters.size() - 1) script += ",";
					}
					script += "], \"";
					script += replaceAll(currentFunction->body, "\"", "\\\"");
					script += "\");";

					sendLogMessage("Patching method %s in class %s.", currentFunction->name.c_str(), currentClass->name.c_str());

					patchCode(script.c_str());
				}
				mode = ParseMethods;
			}
			break;
		}
		case ParseFunction: {
			brackets += std::count(line.begin(), line.end(), '{');
			brackets -= std::count(line.begin(), line.end(), '}');
			if (brackets > 0) {
				currentBody += line + " ";
			}
			else {
				if (currentFunction->body == "") {
					currentFunction->body = currentBody;
				}
				else if (currentFunction->body != currentBody) {
					currentFunction->body = currentBody;

					std::string script;
					script += currentClass->internal_name;
					script += ".";
					script += currentFunction->name;
					script += " = new Function([";
					for (size_t i = 0; i < currentFunction->parameters.size(); ++i) {
						script += "\"" + currentFunction->parameters[i] + "\"";
						if (i < currentFunction->parameters.size() - 1) script += ",";
					}
					script += "], \"";
					script += replaceAll(currentFunction->body, "\"", "\\\"");
					script += "\");";

					sendLogMessage("Patching function %s in class %s.", currentFunction->name.c_str(), currentClass->name.c_str());

					patchCode(script.c_str());
				}
				mode = ParseRegular;
			}
			break;
		}
		case ParseConstructor: {
			brackets += std::count(line.begin(), line.end(), '{');
			brackets -= std::count(line.begin(), line.end(), '}');
			if (brackets > 0) {
				currentBody += line + " ";
			}
			else {
				if (currentFunction->body == "") {
					currentFunction->body = currentBody;
				}
				else if (currentFunction->body != currentBody) {
					std::map<std::string, Func *>::iterator it;
					for (it = currentClass->methods.begin(); it != currentClass->methods.end(); it++) {
						it->second->body = "invalidate it";
					}

					currentFunction->body = currentBody;

					std::string script;
					script += "var ";
					script += currentClass->internal_name;
					script += " = $hxClasses[\"" + currentClass->name + "\"]";
					script += " = new Function([";
					for (size_t i = 0; i < currentFunction->parameters.size(); ++i) {
						script += "\"" + currentFunction->parameters[i] + "\"";
						if (i < currentFunction->parameters.size() - 1) script += ",";
					}
					script += "], \"";
					script += replaceAll(currentFunction->body, "\"", "\\\"");
					script += "\");";

					sendLogMessage("Patching constructor in class %s.", currentFunction->name.c_str());

					script += currentClass->internal_name;
					script += ".__name__ = \"" + currentClass->name + "\";";

					if (currentClass->parent != "") {
						script += currentClass->internal_name;
						script += ".__super__ = " + currentClass->parent + ";";
						script += currentClass->internal_name;
						script += ".prototype = $extend(" + currentClass->parent + ".prototype , {__class__: " + currentClass->internal_name + "});";
					}
					if (currentClass->interfaces != "") {
						script += currentClass->internal_name;
						script += ".__interfaces__ = " + currentClass->interfaces + ";";
					}
					patchCode(script.c_str());
				}
				mode = ParseRegular;
			}
			break;
		}
		}
	}
	sendLogMessage("%i new types found.", types);
	infile.close();
}
} // namespace

extern "C" void watchDirectories(char *path1, char *path2);

extern "C" void filechanged(char *path) {
	std::string strpath = path;
	if (endsWith(strpath, ".png")) {
		std::string name = strpath.substr(strpath.find_last_of('/') + 1);
		imageChanges[name] = true;
	}
	else if (endsWith(strpath, ".essl") || endsWith(strpath, ".glsl") || endsWith(strpath, ".d3d11")) {
		std::string name = strpath.substr(strpath.find_last_of('/') + 1);
		name = name.substr(0, name.find_last_of('.'));
		name = replace(name, '.', '_');
		name = replace(name, '-', '_');
		sendLogMessage("Shader changed: %s.", name.c_str());
		shaderFileNames[name] = strpath;
		shaderChanges[name] = true;
	}
	else if (endsWith(strpath, "krom.js")) {
		sendLogMessage("Code changed.");
		codechanged = true;
	}
}

//__declspec(dllimport) extern "C" void __stdcall Sleep(unsigned long
// milliseconds);

int kickstart(int argc, char **argv) {
	_argc = argc;
	_argv = argv;
	std::string bindir(argv[0]);
#ifdef KORE_WINDOWS
	bindir = bindir.substr(0, bindir.find_last_of("\\"));
#else
	bindir = bindir.substr(0, bindir.find_last_of("/"));
#endif
	assetsdir = argc > 1 ? argv[1] : bindir;
	shadersdir = argc > 2 ? argv[2] : bindir;

	int optionIndex = 3;
	if (shadersdir.rfind("--", 0) == 0) {
		shadersdir = bindir;
		optionIndex = 2;
	}

	bool readStdoutPath = false;
	bool readConsolePid = false;
	bool readPort = false;
	bool writebin = false;
	int port = 0;
	for (int i = optionIndex; i < argc; ++i) {
		if (readPort) {
			port = atoi(argv[i]);
			readPort = false;
		}
		else if (strcmp(argv[i], "--debug") == 0) {
			debugMode = true;
			readPort = true;
		}
		else if (strcmp(argv[i], "--watch") == 0) {
			watch = true;
		}
		else if (strcmp(argv[i], "--sound") == 0) {
			enableSound = true;
		}
		else if (strcmp(argv[i], "--nowindow") == 0) {
			nowindow = true;
		}
		else if (readStdoutPath) {
			freopen(argv[i], "w", stdout);
			readStdoutPath = false;
		}
		else if (strcmp(argv[i], "--stdout") == 0) {
			readStdoutPath = true;
		}
		else if (readConsolePid) {
#ifdef KORE_WINDOWS
			AttachConsole(atoi(argv[i]));
#endif
			readConsolePid = false;
		}
		else if (strcmp(argv[i], "--consolepid") == 0) {
			readConsolePid = true;
		}
		else if (strcmp(argv[i], "--writebin") == 0) {
			writebin = true;
		}
	}

	kromjs = assetsdir + "/krom.js";
	kinc_internal_set_files_location(&assetsdir[0u]);

	kinc_file_reader_t reader;
	if (!writebin && kinc_file_reader_open(&reader, "krom.bin", KINC_FILE_TYPE_ASSET)) {
		serialized = true;
		serializedLength = kinc_file_reader_size(&reader);
		kinc_file_reader_close(&reader);
	}

	if (!serialized && !kinc_file_reader_open(&reader, "krom.js", KINC_FILE_TYPE_ASSET)) {
		fprintf(stderr, "could not load krom.js. aborting.\n");
		exit(1);
	}

	char *code = new char[kinc_file_reader_size(&reader) + 1];
	kinc_file_reader_read(&reader, code, kinc_file_reader_size(&reader));
	code[kinc_file_reader_size(&reader)] = 0;
	kinc_file_reader_close(&reader);

	if (writebin) {
		std::string krombin = assetsdir + "/krom.bin";
		serializeScript(code, &krombin[0u]);
		return 0;
	}

	if (watch) {
		parseCode();
	}

	kinc_threads_init();

	if (watch) {
		watchDirectories(argv[1], argv[2]);
	}

	initKrom(code);

	if (debugMode) {
		startDebugger(runtime, port);
		for (;;) {
			Message message = receiveMessage();
			if (message.size > 0 && message.data[0] == DEBUGGER_MESSAGE_START) {
				if (message.data[1] != KROM_DEBUG_API) {
					const char *outdated;
					if (message.data[1] < KROM_DEBUG_API) {
						outdated = "your IDE";
					}
					else if (KROM_DEBUG_API < message.data[1]) {
						outdated = "Krom";
					}
					sendLogMessage("Krom uses Debug API version %i but your IDE targets "
					               "Debug API version %i. Please update %s.",
					               KROM_DEBUG_API, message.data[1], outdated);
					exit(1);
				}
				break;
			}
#ifdef KORE_WINDOWS
			Sleep(100);
#else
			usleep(100 * 1000);
#endif
		}
	}

	startKrom(code);

	kinc_start();

	if (enableSound) {
		kinc_a2_shutdown();
		kinc_mutex_lock(&mutex); // Prevent audio thread from running
	}

	exit(0); // TODO

	endKrom();

	return 0;
}
#endif

static void krom_start(const FunctionCallbackInfo<Value> &args) {
	node::Environment *env = node::Environment::GetCurrent(args);
	globalEnv = env;

	kinc_start();

	updateFunction.Reset();
	dropFilesFunction.Reset();
	cutFunction.Reset();
	copyFunction.Reset();
	pasteFunction.Reset();
	foregroundFunction.Reset();
	resumeFunction.Reset();
	pauseFunction.Reset();
	backgroundFunction.Reset();
	shutdownFunction.Reset();
	keyboardDownFunction.Reset();
	keyboardUpFunction.Reset();
	keyboardPressFunction.Reset();
	mouseDownFunction.Reset();
	mouseUpFunction.Reset();
	mouseMoveFunction.Reset();
	mouseWheelFunction.Reset();
	penDownFunction.Reset();
	penUpFunction.Reset();
	penMoveFunction.Reset();
	gamepadAxisFunction.Reset();
	gamepadButtonFunction.Reset();
	audioFunction.Reset();
}

static void bindFunctions(Local<Context> context, Local<Object> target) {
#define addFunction(name, func) env->SetMethod(target, #name, func);

	node::Environment *env = node::Environment::GetCurrent(context);

	addFunction(init, krom_init);
	addFunction(log, krom_log);
	addFunction(clear, krom_graphics_clear);
	addFunction(setCallback, krom_set_callback);
	addFunction(setDropFilesCallback, krom_set_drop_files_callback);
	addFunction(setCutCopyPasteCallback, krom_set_cut_copy_paste_callback);
	addFunction(setApplicationStateCallback, krom_set_application_state_callback);
	addFunction(setKeyboardDownCallback, krom_set_keyboard_down_callback);
	addFunction(setKeyboardUpCallback, krom_set_keyboard_up_callback);
	addFunction(setKeyboardPressCallback, krom_set_keyboard_press_callback);
	addFunction(setMouseDownCallback, krom_set_mouse_down_callback);
	addFunction(setMouseUpCallback, krom_set_mouse_up_callback);
	addFunction(setMouseMoveCallback, krom_set_mouse_move_callback);
	addFunction(setMouseWheelCallback, krom_set_mouse_wheel_callback);
	addFunction(setPenDownCallback, krom_set_pen_down_callback);
	addFunction(setPenUpCallback, krom_set_pen_up_callback);
	addFunction(setPenMoveCallback, krom_set_pen_move_callback);
	addFunction(setGamepadAxisCallback, krom_set_gamepad_axis_callback);
	addFunction(setGamepadButtonCallback, krom_set_gamepad_button_callback);
	addFunction(lockMouse, krom_lock_mouse);
	addFunction(unlockMouse, krom_unlock_mouse);
	addFunction(canLockMouse, krom_can_lock_mouse);
	addFunction(isMouseLocked, krom_is_mouse_locked);
	addFunction(setMousePosition, krom_set_mouse_position);
	addFunction(showMouse, krom_show_mouse);
	addFunction(createIndexBuffer, krom_create_indexbuffer);
	addFunction(deleteIndexBuffer, krom_delete_indexbuffer);
	addFunction(lockIndexBuffer, krom_lock_index_buffer);
	addFunction(unlockIndexBuffer, krom_unlock_index_buffer);
	addFunction(setIndexBuffer, krom_set_indexbuffer);
	addFunction(createVertexBuffer, krom_create_vertexbuffer);
	addFunction(deleteVertexBuffer, krom_delete_vertexbuffer);
	addFunction(lockVertexBuffer, krom_lock_vertex_buffer);
	addFunction(unlockVertexBuffer, krom_unlock_vertex_buffer);
	addFunction(setVertexBuffer, krom_set_vertexbuffer);
	addFunction(setVertexBuffers, krom_set_vertexbuffers);
	addFunction(drawIndexedVertices, krom_draw_indexed_vertices);
	addFunction(drawIndexedVerticesInstanced, krom_draw_indexed_vertices_instanced);
	addFunction(createVertexShader, krom_create_vertex_shader);
	addFunction(createVertexShaderFromSource, krom_create_vertex_shader_from_source);
	addFunction(createFragmentShader, krom_create_fragment_shader);
	addFunction(createFragmentShaderFromSource, krom_create_fragment_shader_from_source);
	addFunction(createGeometryShader, krom_create_geometry_shader);
	addFunction(createTessellationControlShader, krom_create_tessellation_control_shader);
	addFunction(createTessellationEvaluationShader, krom_create_tessellation_evaluation_shader);
	addFunction(deleteShader, krom_delete_shader);
	addFunction(createPipeline, krom_create_pipeline);
	addFunction(deletePipeline, krom_delete_pipeline);
	addFunction(compilePipeline, krom_compile_pipeline);
	addFunction(setPipeline, krom_set_pipeline);
	addFunction(loadImage, krom_load_image);
	addFunction(unloadImage, krom_unload_image);
	addFunction(loadSound, krom_load_sound);
	addFunction(setAudioCallback, krom_set_audio_callback);
	addFunction(writeAudioBuffer, krom_write_audio_buffer);
	addFunction(loadBlob, krom_load_blob);
	addFunction(getConstantLocation, krom_get_constant_location);
	addFunction(getTextureUnit, krom_get_texture_unit);
	addFunction(setTexture, krom_set_texture);
	addFunction(setRenderTarget, krom_set_render_target);
	addFunction(setTextureDepth, krom_set_texture_depth);
	addFunction(setImageTexture, krom_set_image_texture);
	addFunction(setTextureParameters, krom_set_texture_parameters);
	addFunction(setTexture3DParameters, krom_set_texture_3d_parameters);
	addFunction(setTextureCompareMode, krom_set_texture_compare_mode);
	addFunction(setCubeMapCompareMode, krom_set_cube_map_compare_mode);
	addFunction(setBool, krom_set_bool);
	addFunction(setInt, krom_set_int);
	addFunction(setFloat, krom_set_float);
	addFunction(setFloat2, krom_set_float2);
	addFunction(setFloat3, krom_set_float3);
	addFunction(setFloat4, krom_set_float4);
	addFunction(setFloats, krom_set_floats);
	addFunction(setMatrix, krom_set_matrix);
	addFunction(setMatrix3, krom_set_matrix3);
	addFunction(getTime, krom_get_time);
	addFunction(windowWidth, krom_window_width);
	addFunction(windowHeight, krom_window_height);
	addFunction(setWindowTitle, krom_set_window_title);
	addFunction(screenDpi, krom_screen_dpi);
	addFunction(systemId, krom_system_id);
	addFunction(requestShutdown, krom_request_shutdown);
	addFunction(displayCount, krom_display_count);
	addFunction(displayWidth, krom_display_width);
	addFunction(displayHeight, krom_display_height);
	addFunction(displayX, krom_display_x);
	addFunction(displayY, krom_display_y);
	addFunction(displayFrequency, krom_display_frequency);
	addFunction(displayIsPrimary, krom_display_is_primary);
	addFunction(writeStorage, krom_write_storage);
	addFunction(readStorage, krom_read_storage);
	addFunction(createRenderTarget, krom_create_render_target);
	addFunction(createRenderTargetCubeMap, krom_create_render_target_cube_map);
	addFunction(createTexture, krom_create_texture);
	addFunction(createTexture3D, krom_create_texture_3d);
	addFunction(createTextureFromBytes, krom_create_texture_from_bytes);
	addFunction(createTextureFromBytes3D, krom_create_texture_from_bytes_3d);
	addFunction(createTextureFromEncodedBytes, krom_create_texture_from_encoded_bytes);
	addFunction(getTexturePixels, krom_get_texture_pixels);
	addFunction(getRenderTargetPixels, krom_get_render_target_pixels);
	addFunction(lockTexture, krom_lock_texture);
	addFunction(unlockTexture, krom_unlock_texture);
	addFunction(clearTexture, krom_clear_texture);
	addFunction(generateTextureMipmaps, krom_generate_texture_mipmaps);
	addFunction(generateRenderTargetMipmaps, krom_generate_render_target_mipmaps);
	addFunction(setMipmaps, krom_set_mipmaps);
	addFunction(setDepthStencilFrom, krom_set_depth_stencil_from);
	addFunction(viewport, krom_viewport);
	addFunction(scissor, krom_scissor);
	addFunction(disableScissor, krom_disable_scissor);
	addFunction(renderTargetsInvertedY, krom_render_targets_inverted_y);
	addFunction(begin, krom_begin);
	addFunction(beginFace, krom_begin_face);
	addFunction(end, krom_end);
	addFunction(fileSaveBytes, krom_file_save_bytes);
	addFunction(sysCommand, krom_sys_command);
	addFunction(savePath, krom_save_path);
	addFunction(getArgCount, krom_get_arg_count);
	addFunction(getArg, krom_get_arg);
	addFunction(getFilesLocation, krom_get_files_location);
	addFunction(setBoolCompute, krom_set_bool_compute);
	addFunction(setIntCompute, krom_set_int_compute);
	addFunction(setFloatCompute, krom_set_float_compute);
	addFunction(setFloat2Compute, krom_set_float2_compute);
	addFunction(setFloat3Compute, krom_set_float3_compute);
	addFunction(setFloat4Compute, krom_set_float4_compute);
	addFunction(setFloatsCompute, krom_set_floats_compute);
	addFunction(setMatrixCompute, krom_set_matrix_compute);
	addFunction(setMatrix3Compute, krom_set_matrix3_compute);
	addFunction(setTextureCompute, krom_set_texture_compute);
	addFunction(setRenderTargetCompute, krom_set_render_target_compute);
	addFunction(setSampledTextureCompute, krom_set_sampled_texture_compute);
	addFunction(setSampledRenderTargetCompute, krom_set_sampled_render_target_compute);
	addFunction(setSampledDepthTextureCompute, krom_set_sampled_depth_texture_compute);
	addFunction(setTextureParametersCompute, krom_set_texture_parameters_compute);
	addFunction(setTexture3DParametersCompute, krom_set_texture_3d_parameters_compute);
	addFunction(setShaderCompute, krom_set_shader_compute);
	addFunction(deleteShaderCompute, krom_delete_shader_compute);
	addFunction(createShaderCompute, krom_create_shader_compute);
	addFunction(getConstantLocationCompute, krom_get_constant_location_compute);
	addFunction(getTextureUnitCompute, krom_get_texture_unit_compute);
	addFunction(compute, krom_compute);
	addFunction(start, krom_start);

#undef addFunction
}

static void registerFunctions(node::ExternalReferenceRegistry *registry) {
#define registerFunction(name, func) registry->Register(func)

	registerFunction(init, krom_init);
	registerFunction(log, krom_log);
	registerFunction(clear, krom_graphics_clear);
	registerFunction(setCallback, krom_set_callback);
	registerFunction(setDropFilesCallback, krom_set_drop_files_callback);
	registerFunction(setCutCopyPasteCallback, krom_set_cut_copy_paste_callback);
	registerFunction(setApplicationStateCallback, krom_set_application_state_callback);
	registerFunction(setKeyboardDownCallback, krom_set_keyboard_down_callback);
	registerFunction(setKeyboardUpCallback, krom_set_keyboard_up_callback);
	registerFunction(setKeyboardPressCallback, krom_set_keyboard_press_callback);
	registerFunction(setMouseDownCallback, krom_set_mouse_down_callback);
	registerFunction(setMouseUpCallback, krom_set_mouse_up_callback);
	registerFunction(setMouseMoveCallback, krom_set_mouse_move_callback);
	registerFunction(setMouseWheelCallback, krom_set_mouse_wheel_callback);
	registerFunction(setPenDownCallback, krom_set_pen_down_callback);
	registerFunction(setPenUpCallback, krom_set_pen_up_callback);
	registerFunction(setPenMoveCallback, krom_set_pen_move_callback);
	registerFunction(setGamepadAxisCallback, krom_set_gamepad_axis_callback);
	registerFunction(setGamepadButtonCallback, krom_set_gamepad_button_callback);
	registerFunction(lockMouse, krom_lock_mouse);
	registerFunction(unlockMouse, krom_unlock_mouse);
	registerFunction(canLockMouse, krom_can_lock_mouse);
	registerFunction(isMouseLocked, krom_is_mouse_locked);
	registerFunction(setMousePosition, krom_set_mouse_position);
	registerFunction(showMouse, krom_show_mouse);
	registerFunction(createIndexBuffer, krom_create_indexbuffer);
	registerFunction(deleteIndexBuffer, krom_delete_indexbuffer);
	registerFunction(lockIndexBuffer, krom_lock_index_buffer);
	registerFunction(unlockIndexBuffer, krom_unlock_index_buffer);
	registerFunction(setIndexBuffer, krom_set_indexbuffer);
	registerFunction(createVertexBuffer, krom_create_vertexbuffer);
	registerFunction(deleteVertexBuffer, krom_delete_vertexbuffer);
	registerFunction(lockVertexBuffer, krom_lock_vertex_buffer);
	registerFunction(unlockVertexBuffer, krom_unlock_vertex_buffer);
	registerFunction(setVertexBuffer, krom_set_vertexbuffer);
	registerFunction(setVertexBuffers, krom_set_vertexbuffers);
	registerFunction(drawIndexedVertices, krom_draw_indexed_vertices);
	registerFunction(drawIndexedVerticesInstanced, krom_draw_indexed_vertices_instanced);
	registerFunction(createVertexShader, krom_create_vertex_shader);
	registerFunction(createVertexShaderFromSource, krom_create_vertex_shader_from_source);
	registerFunction(createFragmentShader, krom_create_fragment_shader);
	registerFunction(createFragmentShaderFromSource, krom_create_fragment_shader_from_source);
	registerFunction(createGeometryShader, krom_create_geometry_shader);
	registerFunction(createTessellationControlShader, krom_create_tessellation_control_shader);
	registerFunction(createTessellationEvaluationShader, krom_create_tessellation_evaluation_shader);
	registerFunction(deleteShader, krom_delete_shader);
	registerFunction(createPipeline, krom_create_pipeline);
	registerFunction(deletePipeline, krom_delete_pipeline);
	registerFunction(compilePipeline, krom_compile_pipeline);
	registerFunction(setPipeline, krom_set_pipeline);
	registerFunction(loadImage, krom_load_image);
	registerFunction(unloadImage, krom_unload_image);
	registerFunction(loadSound, krom_load_sound);
	registerFunction(setAudioCallback, krom_set_audio_callback);
	registerFunction(writeAudioBuffer, krom_write_audio_buffer);
	registerFunction(loadBlob, krom_load_blob);
	registerFunction(getConstantLocation, krom_get_constant_location);
	registerFunction(getTextureUnit, krom_get_texture_unit);
	registerFunction(setTexture, krom_set_texture);
	registerFunction(setRenderTarget, krom_set_render_target);
	registerFunction(setTextureDepth, krom_set_texture_depth);
	registerFunction(setImageTexture, krom_set_image_texture);
	registerFunction(setTextureParameters, krom_set_texture_parameters);
	registerFunction(setTexture3DParameters, krom_set_texture_3d_parameters);
	registerFunction(setTextureCompareMode, krom_set_texture_compare_mode);
	registerFunction(setCubeMapCompareMode, krom_set_cube_map_compare_mode);
	registerFunction(setBool, krom_set_bool);
	registerFunction(setInt, krom_set_int);
	registerFunction(setFloat, krom_set_float);
	registerFunction(setFloat2, krom_set_float2);
	registerFunction(setFloat3, krom_set_float3);
	registerFunction(setFloat4, krom_set_float4);
	registerFunction(setFloats, krom_set_floats);
	registerFunction(setMatrix, krom_set_matrix);
	registerFunction(setMatrix3, krom_set_matrix3);
	registerFunction(getTime, krom_get_time);
	registerFunction(windowWidth, krom_window_width);
	registerFunction(windowHeight, krom_window_height);
	registerFunction(setWindowTitle, krom_set_window_title);
	registerFunction(screenDpi, krom_screen_dpi);
	registerFunction(systemId, krom_system_id);
	registerFunction(requestShutdown, krom_request_shutdown);
	registerFunction(displayCount, krom_display_count);
	registerFunction(displayWidth, krom_display_width);
	registerFunction(displayHeight, krom_display_height);
	registerFunction(displayX, krom_display_x);
	registerFunction(displayY, krom_display_y);
	registerFunction(displayFrequency, krom_display_frequency);
	registerFunction(displayIsPrimary, krom_display_is_primary);
	registerFunction(writeStorage, krom_write_storage);
	registerFunction(readStorage, krom_read_storage);
	registerFunction(createRenderTarget, krom_create_render_target);
	registerFunction(createRenderTargetCubeMap, krom_create_render_target_cube_map);
	registerFunction(createTexture, krom_create_texture);
	registerFunction(createTexture3D, krom_create_texture_3d);
	registerFunction(createTextureFromBytes, krom_create_texture_from_bytes);
	registerFunction(createTextureFromBytes3D, krom_create_texture_from_bytes_3d);
	registerFunction(createTextureFromEncodedBytes, krom_create_texture_from_encoded_bytes);
	registerFunction(getTexturePixels, krom_get_texture_pixels);
	registerFunction(getRenderTargetPixels, krom_get_render_target_pixels);
	registerFunction(lockTexture, krom_lock_texture);
	registerFunction(unlockTexture, krom_unlock_texture);
	registerFunction(clearTexture, krom_clear_texture);
	registerFunction(generateTextureMipmaps, krom_generate_texture_mipmaps);
	registerFunction(generateRenderTargetMipmaps, krom_generate_render_target_mipmaps);
	registerFunction(setMipmaps, krom_set_mipmaps);
	registerFunction(setDepthStencilFrom, krom_set_depth_stencil_from);
	registerFunction(viewport, krom_viewport);
	registerFunction(scissor, krom_scissor);
	registerFunction(disableScissor, krom_disable_scissor);
	registerFunction(renderTargetsInvertedY, krom_render_targets_inverted_y);
	registerFunction(begin, krom_begin);
	registerFunction(beginFace, krom_begin_face);
	registerFunction(end, krom_end);
	registerFunction(fileSaveBytes, krom_file_save_bytes);
	registerFunction(sysCommand, krom_sys_command);
	registerFunction(savePath, krom_save_path);
	registerFunction(getArgCount, krom_get_arg_count);
	registerFunction(getArg, krom_get_arg);
	registerFunction(getFilesLocation, krom_get_files_location);
	registerFunction(setBoolCompute, krom_set_bool_compute);
	registerFunction(setIntCompute, krom_set_int_compute);
	registerFunction(setFloatCompute, krom_set_float_compute);
	registerFunction(setFloat2Compute, krom_set_float2_compute);
	registerFunction(setFloat3Compute, krom_set_float3_compute);
	registerFunction(setFloat4Compute, krom_set_float4_compute);
	registerFunction(setFloatsCompute, krom_set_floats_compute);
	registerFunction(setMatrixCompute, krom_set_matrix_compute);
	registerFunction(setMatrix3Compute, krom_set_matrix3_compute);
	registerFunction(setTextureCompute, krom_set_texture_compute);
	registerFunction(setRenderTargetCompute, krom_set_render_target_compute);
	registerFunction(setSampledTextureCompute, krom_set_sampled_texture_compute);
	registerFunction(setSampledRenderTargetCompute, krom_set_sampled_render_target_compute);
	registerFunction(setSampledDepthTextureCompute, krom_set_sampled_depth_texture_compute);
	registerFunction(setTextureParametersCompute, krom_set_texture_parameters_compute);
	registerFunction(setTexture3DParametersCompute, krom_set_texture_3d_parameters_compute);
	registerFunction(setShaderCompute, krom_set_shader_compute);
	registerFunction(deleteShaderCompute, krom_delete_shader_compute);
	registerFunction(createShaderCompute, krom_create_shader_compute);
	registerFunction(getConstantLocationCompute, krom_get_constant_location_compute);
	registerFunction(getTextureUnitCompute, krom_get_texture_unit_compute);
	registerFunction(compute, krom_compute);
	registerFunction(start, krom_start);

#undef registerFunction
}

namespace krom {
	void Initialize(Local<Object> target, Local<Value> unused, Local<Context> context, void *priv) {
		bindFunctions(context, target);
	}

	void RegisterExternalReferences(node::ExternalReferenceRegistry *registry) {
		registerFunctions(registry);
	}
}

NODE_MODULE_CONTEXT_AWARE_INTERNAL(krom, krom::Initialize)
NODE_MODULE_EXTERNAL_REFERENCE(krom, krom::RegisterExternalReferences)
