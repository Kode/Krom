#include <ChakraCore.h>

#if 0
#include "Base/ThreadBoundThreadContextManager.h"
#include "Base/ThreadContextTlsEntry.h"
#include "Core/AtomLockGuids.h"
#include "Core/ConfigParser.h"
#include "Runtime.h"
#ifdef DYNAMIC_PROFILE_STORAGE
#include "Language/DynamicProfileStorage.h"
#endif
#include "JsrtContext.h"
#include "TestHooks.h"
#ifdef VTUNE_PROFILING
#include "Base/VTuneChakraProfile.h"
#endif
#ifdef ENABLE_JS_ETW
#include "Base/EtwTrace.h"
#endif
#endif

#include "pch.h"

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

CHAKRA_API
JsStringToPointer(_In_ JsValueRef value, _Outptr_result_buffer_(*stringLength) const wchar_t **stringValue, _Out_ size_t *stringLength);

const int KROM_API = 6;
const int KROM_DEBUG_API = 2;

bool AttachProcess(HANDLE hmod);

#ifdef KORE_MACOS
const char *macgetresourcepath();
#endif

const char *getExeDir();

JsRuntimeHandle runtime;
JsContextRef context;

#ifdef KORE_WINDOWS
#define CALLBACK __stdcall
#else
#define CALLBACK
#endif

namespace {
	int _argc;
	char **_argv;
	bool debugMode = false;
	bool watch = false;
	bool enableSound = false;
	bool nowindow = false;
	bool serialized = false;
	unsigned int serializedLength = 0;

	JsValueRef updateFunction;
	JsValueRef dropFilesFunction;
	JsValueRef cutFunction;
	JsValueRef copyFunction;
	JsValueRef pasteFunction;
	JsValueRef foregroundFunction;
	JsValueRef resumeFunction;
	JsValueRef pauseFunction;
	JsValueRef backgroundFunction;
	JsValueRef shutdownFunction;
	JsValueRef keyboardDownFunction;
	JsValueRef keyboardUpFunction;
	JsValueRef keyboardPressFunction;
	JsValueRef mouseDownFunction;
	JsValueRef mouseUpFunction;
	JsValueRef mouseMoveFunction;
	JsValueRef mouseWheelFunction;
	JsValueRef penDownFunction;
	JsValueRef penUpFunction;
	JsValueRef penMoveFunction;
	JsValueRef gamepadAxisFunction;
	JsValueRef gamepadButtonFunction;
	JsValueRef audioFunction;
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

		if (debugMode) {
			std::vector<int> message;
			message.push_back(IDE_MESSAGE_LOG);
			size_t messageLength = strlen(msg);
			message.push_back(messageLength);
			for (size_t i = 0; i < messageLength; ++i) {
				message.push_back(msg[i]);
			}
			sendMessage(message.data(), message.size());
		}
	}

	void sendLogMessage(const char *format, ...) {
		va_list args;
		va_start(args, format);
		sendLogMessageArgs(format, args);
		va_end(args);
	}

	JsValueRef CALLBACK krom_init(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		char title[256];
		size_t length;
		JsCopyString(arguments[1], title, 255, &length);
		title[length] = 0;
		int width, height, samplesPerPixel;
		JsNumberToInt(arguments[2], &width);
		JsNumberToInt(arguments[3], &height);
		JsNumberToInt(arguments[4], &samplesPerPixel);
		bool vSync;
		JsBooleanToBool(arguments[5], &vSync);
		int windowMode, windowFeatures;
		JsNumberToInt(arguments[6], &windowMode);
		JsNumberToInt(arguments[7], &windowFeatures);

		int apiVersion = 0;
		if (argumentCount > 8) {
			JsNumberToInt(arguments[8], &apiVersion);
		}

		if (apiVersion != KROM_API) {
			const char *outdated;
			if (apiVersion < KROM_API) {
				outdated = "Kha";
			}
			else if (KROM_API < apiVersion) {
				outdated = "Krom";
			}
			sendLogMessage("Krom uses API version %i but Kha targets API version %i. Please update %s.", KROM_API, apiVersion, outdated);
			exit(1);
		}

		kinc_window_options_t win;
		win.title = title;
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
		kinc_init(title, width, height, &win, &frame);

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

		kinc_keyboard_key_down_callback = keyDown;
		kinc_keyboard_key_up_callback = keyUp;
		kinc_keyboard_key_press_callback = keyPress;
		kinc_mouse_move_callback = mouseMove;
		kinc_mouse_press_callback = mouseDown;
		kinc_mouse_release_callback = mouseUp;
		kinc_mouse_scroll_callback = mouseWheel;
		kinc_pen_press_callback = penDown;
		kinc_pen_release_callback = penUp;
		kinc_pen_move_callback = penMove;
		kinc_gamepad_axis_callback = gamepadAxis;
		kinc_gamepad_button_callback = gamepadButton;

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_log(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		if (argumentCount < 2) {
			return JS_INVALID_REFERENCE;
		}
		JsValueRef stringValue;
		JsConvertValueToString(arguments[1], &stringValue);

		const wchar_t *str = nullptr;
		size_t strLength = 0;
		JsStringToPointer(stringValue, &str, &strLength);

		size_t done = 0;
		char message[512];
		while (done < strLength) {
			size_t i;
			for (i = 0; i < 510; ++i) {
				message[i] = str[done++];
				if (done >= strLength) {
					message[++i] = '\n';
					++i;
					break;
				}
			}
			message[i] = 0;
			sendLogMessage(message);
		}
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_graphics_clear(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int flags, color, stencil;
		JsNumberToInt(arguments[1], &flags);
		JsNumberToInt(arguments[2], &color);
		double depth;
		JsNumberToDouble(arguments[3], &depth);
		JsNumberToInt(arguments[4], &stencil);
		kinc_g4_clear(flags, color, depth, stencil);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		updateFunction = arguments[1];
		JsAddRef(updateFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_drop_files_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                 void *callbackState) {
		dropFilesFunction = arguments[1];
		JsAddRef(dropFilesFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_cut_copy_paste_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                     void *callbackState) {
		cutFunction = arguments[1];
		copyFunction = arguments[2];
		pasteFunction = arguments[3];
		JsAddRef(cutFunction, nullptr);
		JsAddRef(copyFunction, nullptr);
		JsAddRef(pasteFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_application_state_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                        void *callbackState) {
		foregroundFunction = arguments[1];
		resumeFunction = arguments[2];
		pauseFunction = arguments[3];
		backgroundFunction = arguments[4];
		shutdownFunction = arguments[5];
		JsAddRef(foregroundFunction, nullptr);
		JsAddRef(resumeFunction, nullptr);
		JsAddRef(pauseFunction, nullptr);
		JsAddRef(backgroundFunction, nullptr);
		JsAddRef(shutdownFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_keyboard_down_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                    void *callbackState) {
		keyboardDownFunction = arguments[1];
		JsAddRef(keyboardDownFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_keyboard_up_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                  void *callbackState) {
		keyboardUpFunction = arguments[1];
		JsAddRef(keyboardUpFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_keyboard_press_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                     void *callbackState) {
		keyboardPressFunction = arguments[1];
		JsAddRef(keyboardPressFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_mouse_down_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                 void *callbackState) {
		mouseDownFunction = arguments[1];
		JsAddRef(mouseDownFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_mouse_up_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                               void *callbackState) {
		mouseUpFunction = arguments[1];
		JsAddRef(mouseUpFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_mouse_move_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                 void *callbackState) {
		mouseMoveFunction = arguments[1];
		JsAddRef(mouseMoveFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_mouse_wheel_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                  void *callbackState) {
		mouseWheelFunction = arguments[1];
		JsAddRef(mouseWheelFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_pen_down_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                               void *callbackState) {
		penDownFunction = arguments[1];
		JsAddRef(penDownFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_pen_up_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                             void *callbackState) {
		penUpFunction = arguments[1];
		JsAddRef(penUpFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_pen_move_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                               void *callbackState) {
		penMoveFunction = arguments[1];
		JsAddRef(penMoveFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_gamepad_axis_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                   void *callbackState) {
		gamepadAxisFunction = arguments[1];
		JsAddRef(gamepadAxisFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_gamepad_button_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                     void *callbackState) {
		gamepadButtonFunction = arguments[1];
		JsAddRef(gamepadButtonFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_lock_mouse(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		kinc_mouse_lock(0);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_unlock_mouse(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		kinc_mouse_unlock(0);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_can_lock_mouse(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef value;
		JsBoolToBoolean(kinc_mouse_can_lock(0), &value);
		return value;
	}

	JsValueRef CALLBACK krom_is_mouse_locked(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                         void *callbackState) {
		JsValueRef value;
		JsBoolToBoolean(kinc_mouse_is_locked(0), &value);
		return value;
	}

	JsValueRef CALLBACK krom_set_mouse_position(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                         void *callbackState) {
		int windowId, x, y;
		JsNumberToInt(arguments[1], &windowId);
		JsNumberToInt(arguments[2], &x);
		JsNumberToInt(arguments[3], &y);
		kinc_mouse_set_position(windowId, x, y);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_show_mouse(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		bool value;
		JsBooleanToBool(arguments[1], &value);
		if (value) {
			kinc_mouse_show();
		}
		else {
			kinc_mouse_hide();
		}
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_audio_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                            void *callbackState) {
		audioFunction = arguments[1];
		JsAddRef(audioFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_create_indexbuffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                            void *callbackState) {
		int count;
		JsNumberToInt(arguments[1], &count);
		JsValueRef ib;
		kinc_g4_index_buffer_t *buffer = (kinc_g4_index_buffer_t *)malloc(sizeof(kinc_g4_index_buffer_t));
		kinc_g4_index_buffer_init(buffer, count, KINC_G4_INDEX_BUFFER_FORMAT_32BIT);
		JsCreateExternalObject(buffer, nullptr, &ib);
		return ib;
	}

	JsValueRef CALLBACK krom_delete_indexbuffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                            void *callbackState) {
		kinc_g4_index_buffer_t *buffer;
		JsGetExternalData(arguments[1], (void **)&buffer);
		kinc_g4_index_buffer_destroy(buffer);
		free(buffer);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_lock_index_buffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                           void *callbackState) {
		kinc_g4_index_buffer_t *buffer;
		JsGetExternalData(arguments[1], (void **)&buffer);
		int *indices = kinc_g4_index_buffer_lock(buffer);
		JsValueRef value;
		JsCreateExternalArrayBuffer(indices, kinc_g4_index_buffer_count(buffer) * sizeof(int), nullptr, nullptr, &value);
		JsValueRef array;
		JsCreateTypedArray(JsArrayTypeUint32, value, 0, kinc_g4_index_buffer_count(buffer), &array);
		return array;
	}

	JsValueRef CALLBACK krom_unlock_index_buffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                             void *callbackState) {
		kinc_g4_index_buffer_t *buffer;
		JsGetExternalData(arguments[1], (void **)&buffer);
		kinc_g4_index_buffer_unlock(buffer);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_indexbuffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                         void *callbackState) {
		kinc_g4_index_buffer_t *buffer;
		JsGetExternalData(arguments[1], (void **)&buffer);
		kinc_g4_set_index_buffer(buffer);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_create_vertexbuffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                             void *callbackState) {
		JsValueRef lengthObj;
		JsGetProperty(arguments[2], getId("length"), &lengthObj);
		int length;
		JsNumberToInt(lengthObj, &length);

		JsValueRef one;
		JsIntToNumber(1, &one);

		kinc_g4_vertex_structure_t structure;
		kinc_g4_vertex_structure_init(&structure);
		for (int i = 0; i < length; ++i) {
			JsValueRef index, element;
			JsIntToNumber(i, &index);
			JsGetIndexedProperty(arguments[2], index, &element);
			JsValueRef str;
			JsGetProperty(element, getId("name"), &str);
			char *name = new char[256]; // TODO
			size_t strLength;
			JsCopyString(str, name, 255, &strLength);
			name[strLength] = 0;
			JsValueRef dataObj;
			JsGetProperty(element, getId("data"), &dataObj);
			int data;
			JsNumberToInt(dataObj, &data);
			kinc_g4_vertex_structure_add(&structure, name, (kinc_g4_vertex_data_t)data);
		}

		int value1, value3, value4;
		JsNumberToInt(arguments[1], &value1);
		JsNumberToInt(arguments[3], &value3);
		JsNumberToInt(arguments[4], &value4);
		kinc_g4_vertex_buffer_t *buffer = (kinc_g4_vertex_buffer_t *)malloc(sizeof(kinc_g4_vertex_buffer_t));
		kinc_g4_vertex_buffer_init(buffer, value1, &structure, (kinc_g4_usage_t)value3, value4);
		JsValueRef obj;
		JsCreateExternalObject(buffer, nullptr, &obj);
		return obj;
	}

	JsValueRef CALLBACK krom_delete_vertexbuffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                             void *callbackState) {
		kinc_g4_vertex_buffer_t *buffer;
		JsGetExternalData(arguments[1], (void **)&buffer);
		kinc_g4_vertex_buffer_destroy(buffer);
		free(buffer);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_lock_vertex_buffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                            void *callbackState) {
		kinc_g4_vertex_buffer_t *buffer;
		JsGetExternalData(arguments[1], (void **)&buffer);
		int start, count;
		JsNumberToInt(arguments[2], &start);
		JsNumberToInt(arguments[3], &count);

		float *vertices = kinc_g4_vertex_buffer_lock(buffer, start, count);
		JsValueRef value;
		JsCreateExternalArrayBuffer(vertices, kinc_g4_vertex_buffer_count(buffer) * kinc_g4_vertex_buffer_stride(buffer), nullptr, nullptr, &value);
		JsValueRef array;
		JsCreateTypedArray(JsArrayTypeFloat32, value, 0, kinc_g4_vertex_buffer_count(buffer) * kinc_g4_vertex_buffer_stride(buffer) / 4, &array);
		return array;
	}

	JsValueRef CALLBACK krom_unlock_vertex_buffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                              void *callbackState) {
		kinc_g4_vertex_buffer_t *buffer;
		JsGetExternalData(arguments[1], (void **)&buffer);
		int count;
		JsNumberToInt(arguments[2], &count);

		kinc_g4_vertex_buffer_unlock(buffer, count);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_vertexbuffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                          void *callbackState) {
		kinc_g4_vertex_buffer_t *buffer;
		JsGetExternalData(arguments[1], (void **)&buffer);
		kinc_g4_set_vertex_buffer(buffer);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_vertexbuffers(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                           void *callbackState) {
		kinc_g4_vertex_buffer_t *vertexBuffers[8] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
		JsValueRef lengthObj;
		JsGetProperty(arguments[1], getId("length"), &lengthObj);
		int length;
		JsNumberToInt(lengthObj, &length);
		for (int i = 0; i < length; ++i) {
			JsValueRef index, obj, bufObj;
			JsIntToNumber(i, &index);
			JsGetIndexedProperty(arguments[1], index, &obj);
			JsGetProperty(obj, getId("buffer"), &bufObj);
			kinc_g4_vertex_buffer_t *buffer;
			JsGetExternalData(bufObj, (void **)&buffer);
			vertexBuffers[i] = buffer;
		}
		kinc_g4_set_vertex_buffers(vertexBuffers, length);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_draw_indexed_vertices(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                               void *callbackState) {
		int start, count;
		JsNumberToInt(arguments[1], &start);
		JsNumberToInt(arguments[2], &count);
		if (count < 0) {
			kinc_g4_draw_indexed_vertices();
		}
		else {
			kinc_g4_draw_indexed_vertices_from_to(start, count);
		}
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_draw_indexed_vertices_instanced(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                         void *callbackState) {
		int instanceCount, start, count;
		JsNumberToInt(arguments[1], &instanceCount);
		JsNumberToInt(arguments[2], &start);
		JsNumberToInt(arguments[3], &count);
		if (count < 0) {
			kinc_g4_draw_indexed_vertices_instanced(instanceCount);
		}
		else {
			kinc_g4_draw_indexed_vertices_instanced_from_to(instanceCount, start, count);
		}
		return JS_INVALID_REFERENCE;
	}

	std::string replace(std::string str, char a, char b) {
		for (size_t i = 0; i < str.size(); ++i) {
			if (str[i] == a) str[i] = b;
		}
		return str;
	}

	JsValueRef CALLBACK krom_create_vertex_shader(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                              void *callbackState) {
		Kore::u8 *content;
		unsigned bufferLength;
		JsGetArrayBufferStorage(arguments[1], &content, &bufferLength);
		kinc_g4_shader_t *shader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
		kinc_g4_shader_init(shader, content, bufferLength, KINC_G4_SHADER_TYPE_VERTEX);

		JsValueRef value;
		JsCreateExternalObject(shader, nullptr, &value);
		JsSetProperty(value, getId("name"), arguments[2], false);
		return value;
	}

	JsValueRef CALLBACK krom_create_vertex_shader_from_source(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                          void *callbackState) {
		size_t length;
		JsCopyString(arguments[1], tempStringVS, tempStringSize, &length);
		tempStringVS[length] = 0;
		kinc_g4_shader_t *shader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
		kinc_g4_shader_init_from_source(shader, tempStringVS, KINC_G4_SHADER_TYPE_VERTEX);

		JsValueRef value;
		JsCreateExternalObject(shader, nullptr, &value);
		JsValueRef string;
		JsCreateString("", 0, &string);
		JsSetProperty(value, getId("name"), string, false);
		return value;
	}

	JsValueRef CALLBACK krom_create_fragment_shader(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                void *callbackState) {
		Kore::u8 *content;
		unsigned bufferLength;
		JsGetArrayBufferStorage(arguments[1], &content, &bufferLength);
		kinc_g4_shader_t *shader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
		kinc_g4_shader_init(shader, content, bufferLength, KINC_G4_SHADER_TYPE_FRAGMENT);

		JsValueRef value;
		JsCreateExternalObject(shader, nullptr, &value);
		JsSetProperty(value, getId("name"), arguments[2], false);
		return value;
	}

	JsValueRef CALLBACK krom_create_fragment_shader_from_source(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                            void *callbackState) {
		size_t length;
		JsCopyString(arguments[1], tempStringFS, tempStringSize, &length);
		tempStringFS[length] = 0;
		kinc_g4_shader_t *shader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
		kinc_g4_shader_init_from_source(shader, tempStringVS, KINC_G4_SHADER_TYPE_FRAGMENT);

		JsValueRef value;
		JsCreateExternalObject(shader, nullptr, &value);
		JsValueRef string;
		JsCreateString("", 0, &string);
		JsSetProperty(value, getId("name"), string, false);
		return value;
	}

	JsValueRef CALLBACK krom_create_geometry_shader(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                void *callbackState) {
		Kore::u8 *content;
		unsigned bufferLength;
		JsGetArrayBufferStorage(arguments[1], &content, &bufferLength);
		kinc_g4_shader_t *shader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
		kinc_g4_shader_init(shader, content, bufferLength, KINC_G4_SHADER_TYPE_GEOMETRY);

		JsValueRef value;
		JsCreateExternalObject(shader, nullptr, &value);
		JsSetProperty(value, getId("name"), arguments[2], false);
		return value;
	}

	JsValueRef CALLBACK krom_create_tessellation_control_shader(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                            void *callbackState) {
		Kore::u8 *content;
		unsigned bufferLength;
		JsGetArrayBufferStorage(arguments[1], &content, &bufferLength);
		kinc_g4_shader_t *shader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
		kinc_g4_shader_init(shader, content, bufferLength, KINC_G4_SHADER_TYPE_TESSELLATION_CONTROL);

		JsValueRef value;
		JsCreateExternalObject(shader, nullptr, &value);
		JsSetProperty(value, getId("name"), arguments[2], false);
		return value;
	}

	JsValueRef CALLBACK krom_create_tessellation_evaluation_shader(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                               void *callbackState) {
		Kore::u8 *content;
		unsigned bufferLength;
		JsGetArrayBufferStorage(arguments[1], &content, &bufferLength);
		kinc_g4_shader_t *shader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
		kinc_g4_shader_init(shader, content, bufferLength, KINC_G4_SHADER_TYPE_TESSELLATION_EVALUATION);

		JsValueRef value;
		JsCreateExternalObject(shader, nullptr, &value);
		JsSetProperty(value, getId("name"), arguments[2], false);
		return value;
	}

	JsValueRef CALLBACK krom_delete_shader(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		kinc_g4_shader_t *shader;
		JsGetExternalData(arguments[1], (void **)&shader);
		kinc_g4_shader_destroy(shader);
		free(shader);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_create_pipeline(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                         void *callbackState) {
		kinc_g4_pipeline_t *pipeline = (kinc_g4_pipeline_t *)malloc(sizeof(kinc_g4_pipeline_t));
		kinc_g4_pipeline_init(pipeline);
		JsValueRef pipelineObj;
		JsCreateExternalObject(pipeline, nullptr, &pipelineObj);
		return pipelineObj;
	}

	JsValueRef CALLBACK krom_delete_pipeline(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                         void *callbackState) {
		kinc_g4_pipeline_t *pipeline;
		JsGetExternalData(arguments[1], (void **)&pipeline);
		kinc_g4_pipeline_destroy(pipeline);
		free(pipeline);
		return JS_INVALID_REFERENCE;
	}

	void recompilePipeline(JsValueRef projobj) {
		JsValueRef zero, one, two, three, four, five, six, seven;
		JsIntToNumber(0, &zero);
		JsIntToNumber(1, &one);
		JsIntToNumber(2, &two);
		JsIntToNumber(3, &three);
		JsIntToNumber(4, &four);
		JsIntToNumber(5, &five);
		JsIntToNumber(6, &six);
		JsIntToNumber(7, &seven);

		JsValueRef structsfield;
		JsGetIndexedProperty(projobj, one, &structsfield);
		kinc_g4_vertex_structure_t **structures;
		JsGetExternalData(structsfield, (void **)&structures);

		JsValueRef sizefield;
		JsGetIndexedProperty(projobj, two, &sizefield);
		int size;
		JsNumberToInt(sizefield, &size);

		JsValueRef vsfield;
		JsGetIndexedProperty(projobj, three, &vsfield);
		kinc_g4_shader_t *vs;
		JsGetExternalData(vsfield, (void **)&vs);

		JsValueRef fsfield;
		JsGetIndexedProperty(projobj, four, &fsfield);
		kinc_g4_shader_t *fs;
		JsGetExternalData(fsfield, (void **)&fs);

		kinc_g4_pipeline_t *pipeline = (kinc_g4_pipeline_t *)malloc(sizeof(kinc_g4_pipeline_t));
		pipeline->vertex_shader = vs;
		pipeline->fragment_shader = fs;

		JsValueRef gsfield;
		JsGetIndexedProperty(projobj, five, &gsfield);
		JsValueType type;
		JsGetValueType(gsfield, &type);
		if (type == JsUndefined && type != JsNull) {
			kinc_g4_shader_t *gs;
			JsGetExternalData(gsfield, (void **)&gs);
			pipeline->geometry_shader = gs;
		}

		JsValueRef tcsfield;
		JsGetIndexedProperty(projobj, six, &tcsfield);
		JsGetValueType(tcsfield, &type);
		if (type == JsUndefined && type != JsNull) {
			kinc_g4_shader_t *tcs;
			JsGetExternalData(tcsfield, (void **)&tcs);
			pipeline->tessellation_control_shader = tcs;
		}

		JsValueRef tesfield;
		JsGetIndexedProperty(projobj, six, &tesfield);
		JsGetValueType(tesfield, &type);
		if (type == JsUndefined && type != JsNull) {
			kinc_g4_shader_t *tes;
			JsGetExternalData(tesfield, (void **)&tes);
			pipeline->tessellation_evaluation_shader = tes;
		}

		for (int i = 0; i < size; ++i) {
			pipeline->input_layout[i] = structures[i];
		}
		pipeline->input_layout[size] = nullptr;

		kinc_g4_pipeline_compile(pipeline);

		JsValueRef pipelineObj;
		JsCreateExternalObject(pipeline, nullptr, &pipelineObj);

		JsSetIndexedProperty(projobj, zero, pipelineObj);
	}

#define getPipeInt(name)                                                                                                                                       \
	JsValueRef name##Obj;                                                                                                                                      \
	int name;                                                                                                                                                  \
	JsGetProperty(arguments[12], getId(#name), &name##Obj);                                                                                                    \
	JsNumberToInt(name##Obj, &name)

#define getPipeBool(name)                                                                                                                                      \
	JsValueRef name##Obj;                                                                                                                                      \
	bool name;                                                                                                                                                 \
	JsGetProperty(arguments[12], getId(#name), &name##Obj);                                                                                                    \
	JsBooleanToBool(name##Obj, &name)

	JsValueRef CALLBACK krom_compile_pipeline(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                          void *callbackState) {
		JsValueRef progobj = arguments[1];

		JsValueRef one;
		JsIntToNumber(1, &one);

		kinc_g4_pipeline_t *pipeline;
		JsGetExternalData(progobj, (void **)&pipeline);

		kinc_g4_vertex_structure_t s0, s1, s2, s3;
		kinc_g4_vertex_structure_init(&s0);
		kinc_g4_vertex_structure_init(&s1);
		kinc_g4_vertex_structure_init(&s2);
		kinc_g4_vertex_structure_init(&s3);
		kinc_g4_vertex_structure_t *structures[4] = {&s0, &s1, &s2, &s3};

		int size;
		JsNumberToInt(arguments[6], &size);
		for (int i1 = 0; i1 < size; ++i1) {
			JsValueRef jsstructure = arguments[i1 + 2];

			JsValueRef instancedObj;
			JsGetProperty(jsstructure, getId("instanced"), &instancedObj);
			bool instanced;
			JsBooleanToBool(instancedObj, &instanced);
			structures[i1]->instanced = instanced;

			JsValueRef elementsObj;
			JsGetProperty(jsstructure, getId("elements"), &elementsObj);

			JsValueRef lengthObj;
			JsGetProperty(elementsObj, getId("length"), &lengthObj);
			int length;
			JsNumberToInt(lengthObj, &length);
			for (int i2 = 0; i2 < length; ++i2) {
				JsValueRef index;
				JsIntToNumber(i2, &index);
				JsValueRef element;
				JsGetIndexedProperty(elementsObj, index, &element);
				JsValueRef str;
				JsGetProperty(element, getId("name"), &str);
				JsValueRef dataObj;
				JsGetProperty(element, getId("data"), &dataObj);
				int data;
				JsNumberToInt(dataObj, &data);
				char *name = new char[256]; // TODO
				size_t length;
				JsCopyString(str, name, 255, &length);
				name[length] = 0;
				kinc_g4_vertex_structure_add(structures[i1], name, (kinc_g4_vertex_data_t)data);
			}
		}

		JsValueRef two, three, four, five, six, seven;
		JsIntToNumber(2, &two);
		JsIntToNumber(3, &three);
		JsIntToNumber(4, &four);
		JsIntToNumber(5, &five);
		JsIntToNumber(6, &six);
		JsIntToNumber(7, &seven);

		JsValueRef structuresObj;
		JsCreateExternalObject(structures, nullptr, &structuresObj);
		JsSetIndexedProperty(progobj, one, structuresObj);

		JsSetIndexedProperty(progobj, two, arguments[6]);

		kinc_g4_shader_t *vertexShader;
		JsGetExternalData(arguments[7], (void **)&vertexShader);
		JsValueRef vsObj;
		JsCreateExternalObject(vertexShader, nullptr, &vsObj);
		JsSetIndexedProperty(progobj, three, vsObj);
		JsValueRef vsname;
		JsGetProperty(arguments[7], getId("name"), &vsname);
		JsSetProperty(progobj, getId("vsname"), vsname, false);

		kinc_g4_shader_t *fragmentShader;
		JsGetExternalData(arguments[8], (void **)&fragmentShader);
		JsValueRef fsObj;
		JsCreateExternalObject(fragmentShader, nullptr, &fsObj);
		JsSetIndexedProperty(progobj, four, fsObj);
		JsValueRef fsname;
		JsGetProperty(arguments[8], getId("name"), &fsname);
		JsSetProperty(progobj, getId("fsname"), fsname, false);

		pipeline->vertex_shader = vertexShader;
		pipeline->fragment_shader = fragmentShader;

		JsValueType gsType;
		JsGetValueType(arguments[9], &gsType);
		if (gsType != JsNull && gsType != JsUndefined) {
			kinc_g4_shader_t *geometryShader;
			JsGetExternalData(arguments[9], (void **)&geometryShader);
			JsValueRef gsObj;
			JsCreateExternalObject(geometryShader, nullptr, &gsObj);
			JsSetIndexedProperty(progobj, five, gsObj);
			JsValueRef gsname;
			JsGetProperty(arguments[9], getId("name"), &gsname);
			JsSetProperty(progobj, getId("gsname"), gsname, false);
			pipeline->geometry_shader = geometryShader;
		}

		JsValueType tcsType;
		JsGetValueType(arguments[10], &tcsType);
		if (tcsType != JsNull && tcsType != JsUndefined) {
			kinc_g4_shader_t *tessellationControlShader;
			JsGetExternalData(arguments[10], (void **)&tessellationControlShader);
			JsValueRef tcsObj;
			JsCreateExternalObject(tessellationControlShader, nullptr, &tcsObj);
			JsSetIndexedProperty(progobj, six, tcsObj);
			JsValueRef tcsname;
			JsGetProperty(arguments[10], getId("name"), &tcsname);
			JsSetProperty(progobj, getId("tcsname"), tcsname, false);
			pipeline->tessellation_control_shader = tessellationControlShader;
		}

		JsValueType tesType;
		JsGetValueType(arguments[11], &tesType);
		if (tesType != JsNull && tesType != JsUndefined) {
			kinc_g4_shader_t *tessellationEvaluationShader;
			JsGetExternalData(arguments[11], (void **)&tessellationEvaluationShader);
			JsValueRef tesObj;
			JsCreateExternalObject(tessellationEvaluationShader, nullptr, &tesObj);
			JsSetIndexedProperty(progobj, seven, tesObj);
			JsValueRef tesname;
			JsGetProperty(arguments[11], getId("name"), &tesname);
			JsSetProperty(progobj, getId("tesname"), tesname, false);
			pipeline->tessellation_evaluation_shader = tessellationEvaluationShader;
		}

		for (int i = 0; i < size; ++i) {
			pipeline->input_layout[i] = structures[i];
		}
		pipeline->input_layout[size] = nullptr;

		getPipeInt(cullMode);
		pipeline->cull_mode = (kinc_g4_cull_mode_t)cullMode;
		getPipeBool(depthWrite);
		pipeline->depth_write = depthWrite;
		getPipeInt(depthMode);
		pipeline->depth_mode = (kinc_g4_compare_mode_t)depthMode;

		getPipeInt(stencilMode);
		pipeline->stencil_mode = (kinc_g4_compare_mode_t)stencilMode;
		getPipeInt(stencilBothPass);
		pipeline->stencil_both_pass = (kinc_g4_stencil_action_t)stencilBothPass;
		getPipeInt(stencilDepthFail);
		pipeline->stencil_depth_fail = (kinc_g4_stencil_action_t)stencilDepthFail;
		getPipeInt(stencilFail);
		pipeline->stencil_fail = (kinc_g4_stencil_action_t)stencilFail;
		getPipeInt(stencilReferenceValue);
		pipeline->stencil_reference_value = stencilReferenceValue;
		getPipeInt(stencilReadMask);
		pipeline->stencil_read_mask = stencilReadMask;
		getPipeInt(stencilWriteMask);
		pipeline->stencil_write_mask = stencilWriteMask;

		getPipeInt(blendSource);
		pipeline->blend_source = (kinc_g4_blending_operation_t)blendSource;
		getPipeInt(blendDestination);
		pipeline->blend_destination = (kinc_g4_blending_operation_t)blendDestination;
		getPipeInt(alphaBlendSource);
		pipeline->alpha_blend_source = (kinc_g4_blending_operation_t)alphaBlendSource;
		getPipeInt(alphaBlendDestination);
		pipeline->alpha_blend_destination = (kinc_g4_blending_operation_t)alphaBlendDestination;

		JsValueRef maskRed, maskGreen, maskBlue, maskAlpha;
		JsGetProperty(arguments[12], getId("colorWriteMaskRed"), &maskRed);
		JsGetProperty(arguments[12], getId("colorWriteMaskGreen"), &maskGreen);
		JsGetProperty(arguments[12], getId("colorWriteMaskBlue"), &maskBlue);
		JsGetProperty(arguments[12], getId("colorWriteMaskAlpha"), &maskAlpha);

		for (int i = 0; i < 8; ++i) {
			bool b;
			JsValueRef index, element;
			JsIntToNumber(i, &index);

			JsGetIndexedProperty(maskRed, index, &element);
			JsBooleanToBool(element, &b);
			pipeline->color_write_mask_red[i] = b;

			JsGetIndexedProperty(maskGreen, index, &element);
			JsBooleanToBool(element, &b);
			pipeline->color_write_mask_green[i] = b;

			JsGetIndexedProperty(maskBlue, index, &element);
			JsBooleanToBool(element, &b);
			pipeline->color_write_mask_blue[i] = b;

			JsGetIndexedProperty(maskAlpha, index, &element);
			JsBooleanToBool(element, &b);
			pipeline->color_write_mask_alpha[i] = b;
		}

		getPipeBool(conservativeRasterization);
		pipeline->conservative_rasterization = conservativeRasterization;

		kinc_g4_pipeline_compile(pipeline);

		return JS_INVALID_REFERENCE;
	}

	std::string shadersdir;

	JsValueRef CALLBACK krom_set_pipeline(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef progobj = arguments[1];
		kinc_g4_pipeline_t *pipeline;
		JsGetExternalData(progobj, (void **)&pipeline);

		if (debugMode) {
			char vsname[256];
			JsValueRef vsnameObj;
			JsGetProperty(progobj, getId("vsname"), &vsnameObj);
			size_t vslength;
			JsCopyString(vsnameObj, vsname, 255, &vslength);
			vsname[vslength] = 0;

			char fsname[256];
			JsValueRef fsnameObj;
			JsGetProperty(progobj, getId("fsname"), &fsnameObj);
			size_t fslength;
			JsCopyString(fsnameObj, fsname, 255, &fslength);
			fsname[fslength] = 0;

			bool shaderChanged = false;

			if (shaderChanges[vsname]) {
				shaderChanged = true;
				sendLogMessage("Reloading shader %s.", vsname);
				std::string filename = shaderFileNames[vsname];
				std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary);
				std::vector<char> buffer((std::istreambuf_iterator<char>(input)), (std::istreambuf_iterator<char>()));
				kinc_g4_shader_t *vertexShader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
				kinc_g4_shader_init(vertexShader, buffer.data(), (int)buffer.size(), KINC_G4_SHADER_TYPE_VERTEX);
				JsValueRef three;
				JsIntToNumber(3, &three);
				JsValueRef vsObj;
				JsCreateExternalObject(vertexShader, nullptr, &vsObj);
				JsSetIndexedProperty(progobj, three, vsObj);
				shaderChanges[vsname] = false;
			}

			if (shaderChanges[fsname]) {
				shaderChanged = true;
				sendLogMessage("Reloading shader %s.", fsname);
				std::string filename = shaderFileNames[fsname];
				std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary);
				std::vector<char> buffer((std::istreambuf_iterator<char>(input)), (std::istreambuf_iterator<char>()));
				kinc_g4_shader_t *fragmentShader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
				kinc_g4_shader_init(fragmentShader, buffer.data(), (int)buffer.size(), KINC_G4_SHADER_TYPE_FRAGMENT);
				JsValueRef four;
				JsIntToNumber(4, &four);
				JsValueRef vsObj;
				JsCreateExternalObject(fragmentShader, nullptr, &vsObj);
				JsSetIndexedProperty(progobj, four, vsObj);
				shaderChanges[vsname] = false;
			}

			JsValueRef gsnameObj;
			JsGetProperty(progobj, getId("gsname"), &gsnameObj);
			JsValueType gsnameType;
			JsGetValueType(gsnameObj, &gsnameType);
			if (gsnameType != JsNull && gsnameType != JsUndefined) {
				char gsname[256];
				size_t gslength;
				JsCopyString(gsnameObj, gsname, 255, &gslength);
				gsname[gslength] = 0;
				if (shaderChanges[gsname]) {
					shaderChanged = true;
					sendLogMessage("Reloading shader %s.", gsname);
					std::string filename = shaderFileNames[gsname];
					std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary);
					std::vector<char> buffer((std::istreambuf_iterator<char>(input)), (std::istreambuf_iterator<char>()));
					kinc_g4_shader_t *geometryShader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
					kinc_g4_shader_init(geometryShader, buffer.data(), (int)buffer.size(), KINC_G4_SHADER_TYPE_GEOMETRY);
					JsValueRef five;
					JsIntToNumber(5, &five);
					JsValueRef gsObj;
					JsCreateExternalObject(geometryShader, nullptr, &gsObj);
					JsSetIndexedProperty(progobj, five, gsObj);
					shaderChanges[gsname] = false;
				}
			}

			JsValueRef tcsnameObj;
			JsGetProperty(progobj, getId("tcsname"), &tcsnameObj);
			JsValueType tcsnameType;
			JsGetValueType(tcsnameObj, &tcsnameType);
			if (tcsnameType != JsNull && tcsnameType != JsUndefined) {
				char tcsname[256];
				size_t tcslength;
				JsCopyString(tcsnameObj, tcsname, 255, &tcslength);
				tcsname[tcslength] = 0;
				if (shaderChanges[tcsname]) {
					shaderChanged = true;
					sendLogMessage("Reloading shader %s.", tcsname);
					std::string filename = shaderFileNames[tcsname];
					std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary);
					std::vector<char> buffer((std::istreambuf_iterator<char>(input)), (std::istreambuf_iterator<char>()));
					kinc_g4_shader_t *tessellationControlShader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
					kinc_g4_shader_init(tessellationControlShader, buffer.data(), (int)buffer.size(), KINC_G4_SHADER_TYPE_TESSELLATION_CONTROL);
					JsValueRef six;
					JsIntToNumber(6, &six);
					JsValueRef tcsObj;
					JsCreateExternalObject(tessellationControlShader, nullptr, &tcsObj);
					JsSetIndexedProperty(progobj, six, tcsObj);
					shaderChanges[tcsname] = false;
				}
			}

			JsValueRef tesnameObj;
			JsGetProperty(progobj, getId("tesname"), &tesnameObj);
			JsValueType tesnameType;
			JsGetValueType(tesnameObj, &tesnameType);
			if (tesnameType != JsNull && tesnameType != JsUndefined) {
				char tesname[256];
				size_t teslength;
				JsCopyString(tcsnameObj, tesname, 255, &teslength);
				tesname[teslength] = 0;
				if (shaderChanges[tesname]) {
					shaderChanged = true;
					sendLogMessage("Reloading shader %s.", tesname);
					std::string filename = shaderFileNames[tesname];
					std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary);
					std::vector<char> buffer((std::istreambuf_iterator<char>(input)), (std::istreambuf_iterator<char>()));
					kinc_g4_shader_t *tessellationEvaluationShader = (kinc_g4_shader_t *)malloc(sizeof(kinc_g4_shader_t));
					kinc_g4_shader_init(tessellationEvaluationShader, buffer.data(), (int)buffer.size(), KINC_G4_SHADER_TYPE_TESSELLATION_EVALUATION);
					JsValueRef seven;
					JsIntToNumber(7, &seven);
					JsValueRef tesObj;
					JsCreateExternalObject(tessellationEvaluationShader, nullptr, &tesObj);
					JsSetIndexedProperty(progobj, seven, tesObj);
					shaderChanges[tesname] = false;
				}
			}

			if (shaderChanged) {
				recompilePipeline(progobj);
				JsGetExternalData(progobj, (void **)&pipeline);
			}
		}

		kinc_g4_set_pipeline(pipeline);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_load_image(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		char filename[256];
		size_t length;
		JsCopyString(arguments[1], filename, 255, &length);
		filename[length] = 0;
		bool readable;
		JsBooleanToBool(arguments[2], &readable);

		kinc_image_t image;
		size_t size = kinc_image_size_from_file(filename);
		void *memory = malloc(size);
		kinc_image_init_from_file(&image, memory, filename);

		kinc_g4_texture_t *texture = (kinc_g4_texture_t *)malloc(sizeof(kinc_g4_texture_t));
		kinc_g4_texture_init_from_image(texture, &image);

		JsValueRef obj;
		JsCreateExternalObject(texture, nullptr, &obj);
		JsValueRef width, height, realWidth, realHeight;
		JsIntToNumber(image.width, &width);
		JsSetProperty(obj, getId("width"), width, false);
		JsIntToNumber(image.height, &height);
		JsSetProperty(obj, getId("height"), height, false);
		JsIntToNumber(texture->tex_width, &realWidth);
		JsSetProperty(obj, getId("realWidth"), realWidth, false);
		JsIntToNumber(texture->tex_height, &realHeight);
		JsSetProperty(obj, getId("realHeight"), realHeight, false);
		JsSetProperty(obj, getId("filename"), arguments[1], false);

		if (readable) {
			kinc_image_t *imagePtr = (kinc_image_t *)malloc(sizeof(kinc_image_t));
			memcpy(imagePtr, &image, sizeof(image));

			JsValueRef imageObject;
			JsCreateExternalObject(imagePtr, nullptr, &imageObject);
			JsSetProperty(obj, getId("image"), imageObject, false);
		}
		else {
			kinc_image_destroy(&image);
			free(memory);
		}

		return obj;
	}

	JsValueRef CALLBACK krom_unload_image(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueType type;
		JsGetValueType(arguments[1], &type);
		if (type == JsNull || type == JsUndefined) return JS_INVALID_REFERENCE;

		JsValueRef tex, rt;
		JsGetProperty(arguments[1], getId("texture_"), &tex);
		JsGetProperty(arguments[1], getId("renderTarget_"), &rt);
		JsValueType texType, rtType;
		JsGetValueType(tex, &texType);
		JsGetValueType(rt, &rtType);

		if (texType == JsObject) {
			kinc_g4_texture_t *texture;
			JsGetExternalData(tex, (void **)&texture);
			kinc_g4_texture_destroy(texture);
			free(texture);

			JsValueRef imageObj;
			JsGetProperty(tex, getId("image"), &imageObj);
			JsValueType type;
			JsGetValueType(arguments[1], &type);
			if (type != JsNull && type != JsUndefined) {
				kinc_image_t *image;
				JsGetExternalData(imageObj, (void **)&image);
				free(image->data);
				kinc_image_destroy(image);
				free(image);
			}
		}
		else if (rtType == JsObject) {
			kinc_g4_render_target_t *renderTarget;
			JsGetExternalData(rt, (void **)&renderTarget);
			kinc_g4_render_target_destroy(renderTarget);
			free(renderTarget);
		}

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_load_sound(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		char filename[256];
		size_t length;
		JsCopyString(arguments[1], filename, 255, &length);
		filename[length] = 0;

		kinc_a1_sound_t *sound = kinc_a1_sound_create(filename);

		JsValueRef array;
		JsCreateArrayBuffer(sound->size * 2 * sizeof(float), &array);

		Kore::u8 *tobytes;
		unsigned bufferLength;
		JsGetArrayBufferStorage(array, &tobytes, &bufferLength);
		float *to = (float *)tobytes;

		Kore::s16 *left = (Kore::s16 *)&sound->left[0];
		Kore::s16 *right = (Kore::s16 *)&sound->right[0];
		for (int i = 0; i < sound->size; i += 1) {
			to[i * 2 + 0] = (float)(left[i] / 32767.0);
			to[i * 2 + 1] = (float)(right[i] / 32767.0);
		}

		delete sound;

		return array;
	}

	JsValueRef CALLBACK krom_write_audio_buffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                            void *callbackState) {
		Kore::u8 *buffer;
		unsigned bufferLength;
		JsGetArrayBufferStorage(arguments[1], &buffer, &bufferLength);

		int samples;
		JsNumberToInt(arguments[2], &samples);

		for (int i = 0; i < samples; ++i) {
			float value = *(float *)&buffer[audioReadLocation];
			audioReadLocation += 4;
			if (audioReadLocation >= bufferLength) audioReadLocation = 0;

			// TODO: This is madness
			// *(float *)&Kore::Audio2::buffer.data[Kore::Audio2::buffer.writeLocation] = value;
			// Kore::Audio2::buffer.writeLocation += 4;
			// if (Kore::Audio2::buffer.writeLocation >= Kore::Audio2::buffer.dataSize) Kore::Audio2::buffer.writeLocation = 0;
		}

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_load_blob(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		char filename[256];
		size_t length;
		JsCopyString(arguments[1], filename, 255, &length);
		filename[length] = 0;

		kinc_file_reader_t reader;
		if (!kinc_file_reader_open(&reader, filename, KINC_FILE_TYPE_ASSET)) return JS_INVALID_REFERENCE;

		JsValueRef array;
		JsCreateArrayBuffer(kinc_file_reader_size(&reader), &array);

		Kore::u8 *contents;
		unsigned contentsLength;
		JsGetArrayBufferStorage(array, &contents, &contentsLength);

		kinc_file_reader_read(&reader, contents, kinc_file_reader_size(&reader));

		kinc_file_reader_close(&reader);

		return array;
	}

	JsValueRef CALLBACK krom_get_constant_location(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                               void *callbackState) {
		kinc_g4_pipeline_t *pipeline;
		JsGetExternalData(arguments[1], (void **)&pipeline);

		char name[256];
		size_t length;
		JsCopyString(arguments[2], name, 255, &length);
		name[length] = 0;
		kinc_g4_constant_location_t location = kinc_g4_pipeline_get_constant_location(pipeline, name);
		kinc_g4_constant_location_t *locationPtr = (kinc_g4_constant_location_t *)malloc(sizeof(kinc_g4_constant_location_t));
		memcpy(locationPtr, &location, sizeof(location));

		JsValueRef obj;
		JsCreateExternalObject(locationPtr, nullptr, &obj);
		return obj;
	}

	JsValueRef CALLBACK krom_get_texture_unit(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                          void *callbackState) {
		kinc_g4_pipeline_t *pipeline;
		JsGetExternalData(arguments[1], (void **)&pipeline);

		char name[256];
		size_t length;
		JsCopyString(arguments[2], name, 255, &length);
		name[length] = 0;
		kinc_g4_texture_unit_t unit = kinc_g4_pipeline_get_texture_unit(pipeline, name);
		kinc_g4_texture_unit_t *unitPtr = (kinc_g4_texture_unit_t *)malloc(sizeof(kinc_g4_texture_unit_t));
		memcpy(unitPtr, &unit, sizeof(unit));

		JsValueRef obj;
		JsCreateExternalObject(unitPtr, nullptr, &obj);
		return obj;
	}

	JsValueRef CALLBACK krom_set_texture(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		kinc_g4_texture_unit_t *unit;
		JsGetExternalData(arguments[1], (void **)&unit);

		kinc_g4_texture_t *texture;
		bool imageChanged = false;
		if (debugMode) {
			JsValueRef filenameObj;
			JsGetProperty(arguments[2], getId("filename"), &filenameObj);
			size_t length;
			if (JsCopyString(filenameObj, tempString, tempStringSize, &length) == JsNoError) {
				tempString[length] = 0;
				if (imageChanges[tempString]) {
					imageChanges[tempString] = false;
					sendLogMessage("Image %s changed.", tempString);

					// TODO: Set all texture properties and free previous texture/image

					kinc_image_t image;
					size_t size = kinc_image_size_from_file(tempString);
					void *memory = malloc(size);
					kinc_image_init_from_file(&image, memory, tempString);

					texture = (kinc_g4_texture_t *)malloc(sizeof(kinc_g4_texture_t));
					kinc_g4_texture_init_from_image(texture, &image);

					JsSetExternalData(arguments[2], texture);
					imageChanged = true;
				}
			}
		}
		if (!imageChanged) {
			JsGetExternalData(arguments[2], (void **)&texture);
		}
		kinc_g4_set_texture(*unit, texture);

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_render_target(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                           void *callbackState) {
		kinc_g4_texture_unit_t *unit;
		JsGetExternalData(arguments[1], (void **)&unit);

		kinc_g4_render_target_t *renderTarget;
		JsGetExternalData(arguments[2], (void **)&renderTarget);
		kinc_g4_render_target_use_color_as_texture(renderTarget, *unit);

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_texture_depth(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                           void *callbackState) {
		kinc_g4_texture_unit_t *unit;
		JsGetExternalData(arguments[1], (void **)&unit);

		kinc_g4_render_target_t *renderTarget;
		JsGetExternalData(arguments[2], (void **)&renderTarget);
		kinc_g4_render_target_use_depth_as_texture(renderTarget, *unit);

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_image_texture(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                           void *callbackState) {
		kinc_g4_texture_unit_t *unit;
		JsGetExternalData(arguments[1], (void **)&unit);

		kinc_g4_texture_t *texture;
		JsGetExternalData(arguments[2], (void **)&texture);
		kinc_g4_set_image_texture(*unit, texture);

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_texture_parameters(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                void *callbackState) {
		kinc_g4_texture_unit_t *unit;
		JsGetExternalData(arguments[1], (void **)&unit);
		int u, v, min, max, mip;
		JsNumberToInt(arguments[2], &u);
		JsNumberToInt(arguments[3], &v);
		JsNumberToInt(arguments[4], &min);
		JsNumberToInt(arguments[5], &max);
		JsNumberToInt(arguments[6], &mip);
		kinc_g4_set_texture_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_U, (kinc_g4_texture_addressing_t)u);
		kinc_g4_set_texture_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_V, (kinc_g4_texture_addressing_t)v);
		kinc_g4_set_texture_minification_filter(*unit, (kinc_g4_texture_filter_t)min);
		kinc_g4_set_texture_magnification_filter(*unit, (kinc_g4_texture_filter_t)max);
		kinc_g4_set_texture_mipmap_filter(*unit, (kinc_g4_mipmap_filter_t)mip);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_texture_3d_parameters(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                   void *callbackState) {
		kinc_g4_texture_unit_t *unit;
		JsGetExternalData(arguments[1], (void **)&unit);
		int u, v, w, min, max, mip;
		JsNumberToInt(arguments[2], &u);
		JsNumberToInt(arguments[3], &v);
		JsNumberToInt(arguments[4], &w);
		JsNumberToInt(arguments[5], &min);
		JsNumberToInt(arguments[6], &max);
		JsNumberToInt(arguments[7], &mip);
		kinc_g4_set_texture3d_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_U, (kinc_g4_texture_addressing_t)u);
		kinc_g4_set_texture3d_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_V, (kinc_g4_texture_addressing_t)v);
		kinc_g4_set_texture3d_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_W, (kinc_g4_texture_addressing_t)w);
		kinc_g4_set_texture3d_minification_filter(*unit, (kinc_g4_texture_filter_t)min);
		kinc_g4_set_texture3d_magnification_filter(*unit, (kinc_g4_texture_filter_t)max);
		kinc_g4_set_texture3d_mipmap_filter(*unit, (kinc_g4_mipmap_filter_t)mip);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_texture_compare_mode(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                  void *callbackState) {
		kinc_g4_texture_unit_t *unit;
		JsGetExternalData(arguments[1], (void **)&unit);
		bool enabled;
		JsBooleanToBool(arguments[2], &enabled);
		kinc_g4_set_texture_compare_mode(*unit, enabled);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_cube_map_compare_mode(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                   void *callbackState) {
		kinc_g4_texture_unit_t *unit;
		JsGetExternalData(arguments[1], (void **)&unit);
		bool enabled;
		JsBooleanToBool(arguments[2], &enabled);
		kinc_g4_set_cubemap_compare_mode(*unit, enabled);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_bool(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		kinc_g4_constant_location_t *location;
		JsGetExternalData(arguments[1], (void **)&location);
		bool value;
		JsBooleanToBool(arguments[2], &value);
		kinc_g4_set_bool(*location, value);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_int(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		kinc_g4_constant_location_t *location;
		JsGetExternalData(arguments[1], (void **)&location);
		int value;
		JsNumberToInt(arguments[2], &value);
		kinc_g4_set_int(*location, value);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_float(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		kinc_g4_constant_location_t *location;
		JsGetExternalData(arguments[1], (void **)&location);
		double value;
		JsNumberToDouble(arguments[2], &value);
		kinc_g4_set_float(*location, value);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_float2(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		kinc_g4_constant_location_t *location;
		JsGetExternalData(arguments[1], (void **)&location);
		double value1, value2;
		JsNumberToDouble(arguments[2], &value1);
		JsNumberToDouble(arguments[3], &value2);
		kinc_g4_set_float2(*location, value1, value2);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_float3(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		kinc_g4_constant_location_t *location;
		JsGetExternalData(arguments[1], (void **)&location);
		double value1, value2, value3;
		JsNumberToDouble(arguments[2], &value1);
		JsNumberToDouble(arguments[3], &value2);
		JsNumberToDouble(arguments[4], &value3);
		kinc_g4_set_float3(*location, value1, value2, value3);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_float4(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		kinc_g4_constant_location_t *location;
		JsGetExternalData(arguments[1], (void **)&location);
		double value1, value2, value3, value4;
		JsNumberToDouble(arguments[2], &value1);
		JsNumberToDouble(arguments[3], &value2);
		JsNumberToDouble(arguments[4], &value3);
		JsNumberToDouble(arguments[5], &value4);
		kinc_g4_set_float4(*location, value1, value2, value3, value4);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_floats(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		kinc_g4_constant_location_t *location;
		JsGetExternalData(arguments[1], (void **)&location);

		Kore::u8 *data;
		unsigned bufferLength;
		JsGetArrayBufferStorage(arguments[2], &data, &bufferLength);

		float *from = (float *)data;

		kinc_g4_set_floats(*location, from, int(bufferLength / 4));
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_matrix(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		kinc_g4_constant_location_t *location;
		JsGetExternalData(arguments[1], (void **)&location);

		Kore::u8 *data;
		unsigned bufferLength;
		JsGetArrayBufferStorage(arguments[2], &data, &bufferLength);

		float *from = (float *)data;
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

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_matrix3(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		kinc_g4_constant_location_t *location;
		JsGetExternalData(arguments[1], (void **)&location);

		Kore::u8 *data;
		unsigned bufferLength;
		JsGetArrayBufferStorage(arguments[2], &data, &bufferLength);

		float *from = (float *)data;
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

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_get_time(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef obj;
		JsDoubleToNumber(kinc_time(), &obj);
		return obj;
	}

	JsValueRef CALLBACK krom_window_width(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int windowId;
		JsNumberToInt(arguments[1], &windowId);
		JsValueRef obj;
		JsIntToNumber(kinc_window_width(windowId), &obj);
		return obj;
	}

	JsValueRef CALLBACK krom_window_height(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int windowId;
		JsNumberToInt(arguments[1], &windowId);
		JsValueRef obj;
		JsIntToNumber(kinc_window_height(windowId), &obj);
		return obj;
	}

	JsValueRef CALLBACK krom_set_window_title(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                          void *callbackState) {
		int windowId;
		JsNumberToInt(arguments[1], &windowId);
		char title[256];
		size_t length;
		JsCopyString(arguments[2], title, 255, &length);
		title[length] = 0;
		kinc_window_set_title(windowId, title);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_screen_dpi(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef obj;
		JsIntToNumber(kinc_display_current_mode(kinc_primary_display()).pixels_per_inch, &obj);
		return obj;
	}

	JsValueRef CALLBACK krom_system_id(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef value;
		JsCreateString(kinc_system_id(), strlen(kinc_system_id()), &value);
		return value;
	}

	JsValueRef CALLBACK krom_request_shutdown(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                          void *callbackState) {
		kinc_stop();
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_display_count(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef value;
		JsIntToNumber(kinc_count_displays(), &value);
		return value;
	}

	JsValueRef CALLBACK krom_display_width(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int index;
		JsNumberToInt(arguments[1], &index);
		JsValueRef value;
		JsIntToNumber(kinc_display_current_mode(kinc_primary_display()).width, &value);
		return value;
	}

	JsValueRef CALLBACK krom_display_height(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int index;
		JsNumberToInt(arguments[1], &index);
		JsValueRef value;
		JsIntToNumber(kinc_display_current_mode(kinc_primary_display()).height, &value);
		return value;
	}

	JsValueRef CALLBACK krom_display_x(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int index;
		JsNumberToInt(arguments[1], &index);
		JsValueRef value;
		JsIntToNumber(kinc_display_current_mode(kinc_primary_display()).x, &value);
		return value;
	}

	JsValueRef CALLBACK krom_display_y(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int index;
		JsNumberToInt(arguments[1], &index);
		JsValueRef value;
		JsIntToNumber(kinc_display_current_mode(kinc_primary_display()).y, &value);
		return value;
	}

	JsValueRef CALLBACK krom_display_is_primary(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                            void *callbackState) {
		int index;
		JsNumberToInt(arguments[1], &index);
		JsValueRef value;
		JsBoolToBoolean(index == kinc_primary_display(), &value);
		return value;
	}

	JsValueRef CALLBACK krom_write_storage(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		size_t length;
		JsCopyString(arguments[1], tempString, tempStringSize, &length);
		tempString[length] = 0;

		Kore::u8 *buffer;
		unsigned bufferLength;
		JsGetArrayBufferStorage(arguments[2], &buffer, &bufferLength);

		kinc_file_writer_t writer;
		if (!kinc_file_writer_open(&writer, tempString)) return JS_INVALID_REFERENCE;
		kinc_file_writer_write(&writer, buffer, (int)bufferLength);
		kinc_file_writer_close(&writer);

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_read_storage(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		size_t length;
		JsCopyString(arguments[1], tempString, tempStringSize, &length);
		tempString[length] = 0;

		kinc_file_reader_t reader;
		if (!kinc_file_reader_open(&reader, tempString, KINC_FILE_TYPE_SAVE)) return JS_INVALID_REFERENCE;

		JsValueRef buffer;
		JsCreateArrayBuffer(kinc_file_reader_size(&reader), &buffer);

		Kore::u8 *bufferData;
		unsigned bufferLength;
		JsGetArrayBufferStorage(buffer, &bufferData, &bufferLength);

		kinc_file_reader_read(&reader, bufferData, kinc_file_reader_size(&reader));
		kinc_file_reader_close(&reader);

		return buffer;
	}

	JsValueRef CALLBACK krom_create_render_target(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                              void *callbackState) {
		int value1, value2, value3, value4, value5;
		JsNumberToInt(arguments[1], &value1);
		JsNumberToInt(arguments[2], &value2);
		JsNumberToInt(arguments[3], &value3);
		JsNumberToInt(arguments[4], &value4);
		JsNumberToInt(arguments[5], &value5);
		kinc_g4_render_target_t *renderTarget = (kinc_g4_render_target_t *)malloc(sizeof(kinc_g4_render_target_t));
		kinc_g4_render_target_init(renderTarget, value1, value2, value3, false, (kinc_g4_render_target_format_t)value4, value5, 0);

		JsValueRef value;
		JsCreateExternalObject(renderTarget, nullptr, &value);

		JsValueRef width, height;
		JsIntToNumber(renderTarget->width, &width);
		JsIntToNumber(renderTarget->height, &height);

		JsSetProperty(value, getId("width"), width, false);
		JsSetProperty(value, getId("height"), height, false);

		return value;
	}

	JsValueRef CALLBACK krom_create_render_target_cube_map(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                       void *callbackState) {
		int value1, value2, value3, value4;
		JsNumberToInt(arguments[1], &value1);
		JsNumberToInt(arguments[2], &value2);
		JsNumberToInt(arguments[3], &value3);
		JsNumberToInt(arguments[4], &value4);
		kinc_g4_render_target_t *renderTarget = (kinc_g4_render_target_t *)malloc(sizeof(kinc_g4_render_target_t));
		kinc_g4_render_target_init_cube(renderTarget, value1, value2, false, (kinc_g4_render_target_format_t)value3, value4, 0);

		JsValueRef value;
		JsCreateExternalObject(renderTarget, nullptr, &value);

		JsValueRef width, height;
		JsIntToNumber(renderTarget->width, &width);
		JsIntToNumber(renderTarget->height, &height);

		JsSetProperty(value, getId("width"), width, false);
		JsSetProperty(value, getId("height"), height, false);

		return value;
	}

	JsValueRef CALLBACK krom_create_texture(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int value1, value2, value3;
		JsNumberToInt(arguments[1], &value1);
		JsNumberToInt(arguments[2], &value2);
		JsNumberToInt(arguments[3], &value3);

		kinc_g4_texture_t *texture = (kinc_g4_texture_t *)malloc(sizeof(kinc_g4_texture_t));
		kinc_g4_texture_init(texture, value1, value2, (kinc_image_format_t)value3);

		JsValueRef value;
		JsCreateExternalObject(texture, nullptr, &value);

		JsValueRef realWidth, realHeight;
		JsIntToNumber(texture->tex_width, &realWidth);
		JsIntToNumber(texture->tex_height, &realHeight);

		JsSetProperty(value, getId("width"), arguments[1], false);
		JsSetProperty(value, getId("height"), arguments[2], false);
		JsSetProperty(value, getId("realWidth"), realWidth, false);
		JsSetProperty(value, getId("realHeight"), realHeight, false);

		return value;
	}

	JsValueRef CALLBACK krom_create_texture_3d(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                           void *callbackState) {
		int value1, value2, value3, value4;
		JsNumberToInt(arguments[1], &value1);
		JsNumberToInt(arguments[2], &value2);
		JsNumberToInt(arguments[3], &value3);
		JsNumberToInt(arguments[4], &value4);

		kinc_g4_texture_t *texture = (kinc_g4_texture_t *)malloc(sizeof(kinc_g4_texture_t));
		kinc_g4_texture_init3d(texture, value1, value2, value3, (kinc_image_format_t)value4);

		JsValueRef tex;
		JsCreateExternalObject(texture, nullptr, &tex);

		JsValueRef realWidth, realHeight;
		JsIntToNumber(texture->tex_width, &realWidth);
		JsIntToNumber(texture->tex_height, &realHeight);

		JsSetProperty(tex, getId("width"), arguments[1], false);
		JsSetProperty(tex, getId("height"), arguments[2], false);
		JsSetProperty(tex, getId("depth"), arguments[3], false);
		JsSetProperty(tex, getId("realWidth"), realWidth, false);
		JsSetProperty(tex, getId("realHeight"), realHeight, false);

		return tex;
	}

	JsValueRef CALLBACK krom_create_texture_from_bytes(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                   void *callbackState) {
		Kore::u8 *content;
		unsigned bufferLength;
		JsGetArrayBufferStorage(arguments[1], &content, &bufferLength);

		int value2, value3, value4;
		bool readable;
		JsNumberToInt(arguments[2], &value2);
		JsNumberToInt(arguments[3], &value3);
		JsNumberToInt(arguments[4], &value4);
		JsBooleanToBool(arguments[5], &readable);

		void *data = malloc(bufferLength);
		memcpy(data, content, bufferLength);

		kinc_image_t image;
		kinc_image_init_from_bytes(&image, data, value2, value3, (kinc_image_format_t)value4);

		kinc_g4_texture_t *texture = (kinc_g4_texture_t *)malloc(sizeof(kinc_g4_texture_t));
		kinc_g4_texture_init_from_image(texture, &image);

		JsValueRef value;
		JsCreateExternalObject(texture, nullptr, &value);

		JsValueRef width, height, realWidth, realHeight;
		JsIntToNumber(image.width, &width);
		JsIntToNumber(image.height, &height);
		JsIntToNumber(texture->tex_width, &realWidth);
		JsIntToNumber(texture->tex_height, &realHeight);

		JsSetProperty(value, getId("width"), width, false);
		JsSetProperty(value, getId("height"), height, false);
		JsSetProperty(value, getId("realWidth"), realWidth, false);
		JsSetProperty(value, getId("realHeight"), realHeight, false);

		if (readable) {
			kinc_image_t *imagePtr = (kinc_image_t *)malloc(sizeof(kinc_image_t));
			memcpy(imagePtr, &image, sizeof(image));

			JsValueRef imageObject;
			JsCreateExternalObject(imagePtr, nullptr, &imageObject);
			JsSetProperty(value, getId("image"), imageObject, false);
		}
		else {
			kinc_image_destroy(&image);
		}

		return value;
	}

	JsValueRef CALLBACK krom_create_texture_from_bytes_3d(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                      void *callbackState) {
		Kore::u8 *content;
		unsigned bufferLength;
		JsGetArrayBufferStorage(arguments[1], &content, &bufferLength);

		int value2, value3, value4, value5;
		bool readable;
		JsNumberToInt(arguments[2], &value2);
		JsNumberToInt(arguments[3], &value3);
		JsNumberToInt(arguments[4], &value4);
		JsNumberToInt(arguments[5], &value5);
		JsBooleanToBool(arguments[6], &readable);

		void *data = malloc(bufferLength);
		memcpy(data, content, bufferLength);

		kinc_image_t image;
		kinc_image_init_from_bytes3d(&image, data, value2, value3, value4, (kinc_image_format_t)value5);

		kinc_g4_texture_t *texture = (kinc_g4_texture_t *)malloc(sizeof(kinc_g4_texture_t));
		kinc_g4_texture_init_from_image3d(texture, &image);

		JsValueRef value;
		JsCreateExternalObject(texture, nullptr, &value);

		JsValueRef width, height, depth, realWidth, realHeight;
		JsIntToNumber(image.width, &width);
		JsIntToNumber(image.height, &height);
		JsIntToNumber(image.depth, &depth);
		JsIntToNumber(texture->tex_width, &realWidth);
		JsIntToNumber(texture->tex_height, &realHeight);

		JsSetProperty(value, getId("width"), width, false);
		JsSetProperty(value, getId("height"), height, false);
		JsSetProperty(value, getId("depth"), depth, false);
		JsSetProperty(value, getId("realWidth"), realWidth, false);
		JsSetProperty(value, getId("realHeight"), realHeight, false);

		if (readable) {
			kinc_image_t *imagePtr = (kinc_image_t *)malloc(sizeof(kinc_image_t));
			memcpy(imagePtr, &image, sizeof(image));

			JsValueRef imageObject;
			JsCreateExternalObject(imagePtr, nullptr, &imageObject);
			JsSetProperty(value, getId("image"), imageObject, false);
		}
		else {
			kinc_image_destroy(&image);
		}

		return value;
	}

	JsValueRef CALLBACK krom_create_texture_from_encoded_bytes(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                           void *callbackState) {
		Kore::u8 *content;
		unsigned bufferLength;
		JsGetArrayBufferStorage(arguments[1], &content, &bufferLength);

		char format[32];
		size_t length;
		JsCopyString(arguments[2], format, 31, &length);
		format[length] = 0;
		bool readable;
		JsBooleanToBool(arguments[3], &readable);

		size_t size = kinc_image_size_from_encoded_bytes(content, bufferLength, format);
		void *memory = malloc(size);
		kinc_image_t image;
		kinc_image_init_from_encoded_bytes(&image, memory, content, bufferLength, format);

		kinc_g4_texture_t *texture = (kinc_g4_texture_t *)malloc(sizeof(kinc_g4_texture_t));
		kinc_g4_texture_init_from_image(texture, &image);

		JsValueRef value;
		JsCreateExternalObject(texture, nullptr, &value);

		JsValueRef width, height, realWidth, realHeight;
		JsIntToNumber(image.width, &width);
		JsIntToNumber(image.height, &height);
		JsIntToNumber(texture->tex_width, &realWidth);
		JsIntToNumber(texture->tex_height, &realHeight);

		JsSetProperty(value, getId("width"), width, false);
		JsSetProperty(value, getId("height"), height, false);
		JsSetProperty(value, getId("realWidth"), realWidth, false);
		JsSetProperty(value, getId("realHeight"), realHeight, false);

		if (readable) {
			kinc_image_t *imagePtr = (kinc_image_t *)malloc(sizeof(kinc_image_t));
			memcpy(imagePtr, &image, sizeof(image));

			JsValueRef imageObject;
			JsCreateExternalObject(imagePtr, nullptr, &imageObject);
			JsSetProperty(value, getId("image"), imageObject, false);
		}
		else {
			kinc_image_destroy(&image);
			free(memory);
		}

		return value;
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

	JsValueRef CALLBACK krom_get_texture_pixels(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                            void *callbackState) {
		kinc_g4_texture *texture;
		JsGetExternalData(arguments[1], (void **)&texture);

		JsValueRef imageObj;
		JsGetProperty(arguments[1], getId("image"), &imageObj);
		JsValueType type;
		JsGetValueType(arguments[1], &type);
		if (type == JsNull || type == JsUndefined) {
			return JS_INVALID_REFERENCE;
		}
		else {
			kinc_image_t *image;
			JsGetExternalData(imageObj, (void **)&image);
			Kore::u8 *data = kinc_image_get_pixels(image);
			int byteLength = formatByteSize(texture->format) * texture->tex_width * texture->tex_height * texture->tex_depth;
			JsValueRef value;
			JsCreateExternalArrayBuffer(data, byteLength, nullptr, nullptr, &value);
			return value;
		}
	}

	JsValueRef CALLBACK krom_get_render_target_pixels(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                  void *callbackState) {
		kinc_g4_render_target_t *rt;
		JsGetExternalData(arguments[1], (void **)&rt);

		Kore::u8 *content;
		unsigned bufferLength;
		JsGetArrayBufferStorage(arguments[2], &content, &bufferLength);

		kinc_g4_render_target_get_pixels(rt, content);

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_lock_texture(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		kinc_g4_texture_t *texture;
		JsGetExternalData(arguments[1], (void **)&texture);
		Kore::u8 *tex = kinc_g4_texture_lock(texture);

		JsValueRef stride;
		JsIntToNumber(kinc_g4_texture_stride(texture), &stride);
		JsSetProperty(arguments[1], getId("stride"), stride, false);

		int byteLength = kinc_g4_texture_stride(texture) * texture->tex_height * texture->tex_depth;
		JsValueRef value;
		JsCreateExternalArrayBuffer(tex, byteLength, nullptr, nullptr, &value);
		return value;
	}

	JsValueRef CALLBACK krom_unlock_texture(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		kinc_g4_texture_t *texture;
		JsGetExternalData(arguments[1], (void **)&texture);
		kinc_g4_texture_unlock(texture);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_clear_texture(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		kinc_g4_texture_t *texture;
		JsGetExternalData(arguments[1], (void **)&texture);
		int x, y, z, width, height, depth, color;
		JsNumberToInt(arguments[2], &x);
		JsNumberToInt(arguments[3], &y);
		JsNumberToInt(arguments[4], &z);
		JsNumberToInt(arguments[5], &width);
		JsNumberToInt(arguments[6], &height);
		JsNumberToInt(arguments[7], &depth);
		JsNumberToInt(arguments[8], &color);
		kinc_g4_texture_clear(texture, x, y, z, width, height, depth, color);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_generate_texture_mipmaps(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                  void *callbackState) {
		kinc_g4_texture_t *texture;
		JsGetExternalData(arguments[1], (void **)&texture);
		int levels;
		JsNumberToInt(arguments[2], &levels);
		kinc_g4_texture_generate_mipmaps(texture, levels);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_generate_render_target_mipmaps(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                        void *callbackState) {
		kinc_g4_render_target_t *rt;
		JsGetExternalData(arguments[1], (void **)&rt);
		int levels;
		JsNumberToInt(arguments[2], &levels);
		kinc_g4_render_target_generate_mipmaps(rt, levels);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_mipmaps(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		kinc_g4_texture_t *texture;
		JsGetExternalData(arguments[1], (void **)&texture);

		JsValueRef lengthObj;
		JsGetProperty(arguments[2], getId("length"), &lengthObj);
		int length;
		JsNumberToInt(lengthObj, &length);
		for (int i = 0; i < length; ++i) {
			JsValueRef index, element;
			JsIntToNumber(i, &index);
			JsGetIndexedProperty(arguments[2], index, &element);
			JsValueRef obj;
			JsGetProperty(element, getId("texture_"), &obj);
			kinc_g4_texture_t *mipmap;
			JsGetExternalData(obj, (void **)&mipmap);

			JsValueRef imageObj;
			JsGetProperty(obj, getId("image"), &imageObj);
			JsValueType type;
			JsGetValueType(obj, &type);
			if (type != JsNull && type != JsUndefined) {
				kinc_image_t *image;
				JsGetExternalData(imageObj, (void **)&image);
				kinc_g4_texture_set_mipmap(texture, image, i + 1);
			}
		}
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_depth_stencil_from(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                void *callbackState) {
		kinc_g4_render_target_t *renderTarget;
		JsGetExternalData(arguments[1], (void **)&renderTarget);
		kinc_g4_render_target_t *sourceTarget;
		JsGetExternalData(arguments[2], (void **)&sourceTarget);
		kinc_g4_render_target_set_depth_stencil_from(renderTarget, sourceTarget);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_viewport(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int x, y, w, h;
		JsNumberToInt(arguments[1], &x);
		JsNumberToInt(arguments[2], &y);
		JsNumberToInt(arguments[3], &w);
		JsNumberToInt(arguments[4], &h);

		kinc_g4_viewport(x, y, w, h);

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_scissor(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int x, y, w, h;
		JsNumberToInt(arguments[1], &x);
		JsNumberToInt(arguments[2], &y);
		JsNumberToInt(arguments[3], &w);
		JsNumberToInt(arguments[4], &h);

		kinc_g4_scissor(x, y, w, h);

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_disable_scissor(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                         void *callbackState) {
		kinc_g4_disable_scissor();
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_render_targets_inverted_y(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                   void *callbackState) {
		JsValueRef value;
		JsBoolToBoolean(kinc_g4_render_targets_inverted_y(), &value);
		return value;
	}

	JsValueRef CALLBACK krom_begin(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueType type;
		JsGetValueType(arguments[1], &type);
		if (type == JsNull || type == JsUndefined) {
			kinc_g4_restore_render_target();
			return JS_INVALID_REFERENCE;
		}
		else {
			JsValueRef rt;
			JsGetProperty(arguments[1], getId("renderTarget_"), &rt);
			kinc_g4_render_target_t *renderTarget;
			JsGetExternalData(rt, (void **)&renderTarget);

			JsValueType type2;
			JsGetValueType(arguments[2], &type2);
			if (type2 == JsNull || type2 == JsUndefined) {
				kinc_g4_set_render_targets(&renderTarget, 1);
			}
			else {
				kinc_g4_render_target_t *renderTargets[8] = {renderTarget, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
				JsValueRef lengthObj;
				JsGetProperty(arguments[2], getId("length"), &lengthObj);
				int length;
				JsNumberToInt(lengthObj, &length);
				if (length > 7) length = 7;
				for (int i = 0; i < length; ++i) {
					JsValueRef index, element;
					JsIntToNumber(i, &index);
					JsGetIndexedProperty(arguments[2], index, &element);
					JsValueRef obj;
					JsGetProperty(element, getId("renderTarget_"), &obj);
					kinc_g4_render_target_t *art;
					JsGetExternalData(obj, (void **)&art);
					renderTargets[i + 1] = art;
				}
				kinc_g4_set_render_targets(renderTargets, length + 1);
			}
			return JS_INVALID_REFERENCE;
		}
	}

	JsValueRef CALLBACK krom_begin_face(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef rt;
		JsGetProperty(arguments[1], getId("renderTarget_"), &rt);
		kinc_g4_render_target_t *renderTarget;
		JsGetExternalData(rt, (void **)&renderTarget);
		int face;
		JsNumberToInt(arguments[2], &face);
		kinc_g4_set_render_target_face(renderTarget, face);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_end(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_file_save_bytes(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                         void *callbackState) {
		size_t length;
		JsCopyString(arguments[1], tempString, tempStringSize, &length);
		tempString[length] = 0;

		Kore::u8 *content;
		unsigned bufferLength;
		JsGetArrayBufferStorage(arguments[2], &content, &bufferLength);

		FILE *file = fopen(tempString, "wb");
		if (file == nullptr) return JS_INVALID_REFERENCE;
		fwrite(content, 1, (int)bufferLength, file);
		fclose(file);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_sys_command(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		char command[1024];
		size_t length;
		JsCopyString(arguments[1], command, 1023, &length);
		command[length] = 0;
		int result = system(command);
		JsValueRef value;
		JsIntToNumber(result, &value);
		return value;
	}

	// TODO: Remove this if possible
	JsValueRef CALLBACK krom_save_path(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef value;
		JsCreateString(kinc_internal_save_path(), strlen(kinc_internal_save_path()), &value);
		return value;
	}

	JsValueRef CALLBACK krom_get_arg_count(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef value;
		JsIntToNumber(_argc, &value);
		return value;
	}

	JsValueRef CALLBACK krom_get_arg(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int index;
		JsNumberToInt(arguments[1], &index);
		JsValueRef value;
		JsCreateString(_argv[index], strlen(_argv[index]), &value);
		return value;
	}

	JsValueRef CALLBACK krom_get_files_location(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                            void *callbackState) {
		JsValueRef value;
		JsCreateString(kinc_internal_get_files_location(), strlen(kinc_internal_get_files_location()), &value);
		return value;
	}

	JsValueRef CALLBACK krom_set_bool_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                          void *callbackState) {
		kinc_compute_constant_location_t *location;
		JsGetExternalData(arguments[1], (void **)&location);
		int value;
		JsNumberToInt(arguments[2], &value);
		kinc_compute_set_bool(*location, value != 0);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_int_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                         void *callbackState) {
		kinc_compute_constant_location_t *location;
		JsGetExternalData(arguments[1], (void **)&location);
		int value;
		JsNumberToInt(arguments[2], &value);
		kinc_compute_set_int(*location, value);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_float_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                           void *callbackState) {
		kinc_compute_constant_location_t *location;
		JsGetExternalData(arguments[1], (void **)&location);
		double value;
		JsNumberToDouble(arguments[2], &value);
		kinc_compute_set_float(*location, value);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_float2_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                            void *callbackState) {
		kinc_compute_constant_location_t *location;
		JsGetExternalData(arguments[1], (void **)&location);
		double value1, value2;
		JsNumberToDouble(arguments[2], &value1);
		JsNumberToDouble(arguments[3], &value2);
		kinc_compute_set_float2(*location, value1, value2);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_float3_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                            void *callbackState) {
		kinc_compute_constant_location_t *location;
		JsGetExternalData(arguments[1], (void **)&location);
		double value1, value2, value3;
		JsNumberToDouble(arguments[2], &value1);
		JsNumberToDouble(arguments[3], &value2);
		JsNumberToDouble(arguments[4], &value3);
		kinc_compute_set_float3(*location, value1, value2, value3);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_float4_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                            void *callbackState) {
		kinc_compute_constant_location_t *location;
		JsGetExternalData(arguments[1], (void **)&location);
		double value1, value2, value3, value4;
		JsNumberToDouble(arguments[2], &value1);
		JsNumberToDouble(arguments[3], &value2);
		JsNumberToDouble(arguments[4], &value3);
		JsNumberToDouble(arguments[5], &value4);
		kinc_compute_set_float4(*location, value1, value2, value3, value4);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_floats_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                            void *callbackState) {
		kinc_compute_constant_location_t *location;
		JsGetExternalData(arguments[1], (void **)&location);

		Kore::u8 *data;
		unsigned bufferLength;
		JsGetArrayBufferStorage(arguments[2], &data, &bufferLength);

		float *from = (float *)data;

		kinc_compute_set_floats(*location, from, int(bufferLength / 4));

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_matrix_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                            void *callbackState) {
		kinc_compute_constant_location_t *location;
		JsGetExternalData(arguments[1], (void **)&location);

		Kore::u8 *data;
		unsigned bufferLength;
		JsGetArrayBufferStorage(arguments[2], &data, &bufferLength);

		float *from = (float *)data;
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

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_matrix3_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                             void *callbackState) {
		kinc_compute_constant_location_t *location;
		JsGetExternalData(arguments[1], (void **)&location);

		Kore::u8 *data;
		unsigned bufferLength;
		JsGetArrayBufferStorage(arguments[2], &data, &bufferLength);

		float *from = (float *)data;
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

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_texture_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                             void *callbackState) {
		kinc_compute_texture_unit *unit;
		JsGetExternalData(arguments[1], (void **)&unit);

		kinc_g4_texture_t *texture;
		JsGetExternalData(arguments[2], (void **)&texture);

		int access;
		JsNumberToInt(arguments[3], &access);

		kinc_compute_set_texture(*unit, texture, (kinc_compute_access_t)access);

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_render_target_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                   void *callbackState) {
		kinc_compute_texture_unit_t *unit;
		JsGetExternalData(arguments[1], (void **)&unit);

		kinc_g4_render_target_t *renderTarget;
		JsGetExternalData(arguments[2], (void **)&renderTarget);

		int access;
		JsNumberToInt(arguments[3], &access);

		kinc_compute_set_render_target(*unit, renderTarget, (kinc_compute_access_t)access);

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_sampled_texture_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                     void *callbackState) {
		kinc_compute_texture_unit *unit;
		JsGetExternalData(arguments[1], (void **)&unit);

		kinc_g4_texture_t *texture;
		JsGetExternalData(arguments[2], (void **)&texture);
		kinc_compute_set_sampled_texture(*unit, texture);

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_sampled_render_target_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                           void *callbackState) {
		kinc_compute_texture_unit *unit;
		JsGetExternalData(arguments[1], (void **)&unit);

		kinc_g4_render_target_t *renderTarget;
		JsGetExternalData(arguments[2], (void **)&renderTarget);
		kinc_compute_set_sampled_render_target(*unit, renderTarget);

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_sampled_depth_texture_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                           void *callbackState) {
		kinc_compute_texture_unit *unit;
		JsGetExternalData(arguments[1], (void **)&unit);

		kinc_g4_render_target_t *renderTarget;
		JsGetExternalData(arguments[2], (void **)&renderTarget);
		kinc_compute_set_sampled_depth_from_render_target(*unit, renderTarget);

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_texture_parameters_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                        void *callbackState) {
		kinc_compute_texture_unit *unit;
		JsGetExternalData(arguments[1], (void **)&unit);

		int u, v, min, max, mip;
		JsNumberToInt(arguments[2], &u);
		JsNumberToInt(arguments[3], &v);
		JsNumberToInt(arguments[4], &min);
		JsNumberToInt(arguments[5], &max);
		JsNumberToInt(arguments[6], &mip);

		kinc_compute_set_texture_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_U, (kinc_g4_texture_addressing_t)u);
		kinc_compute_set_texture_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_V, (kinc_g4_texture_addressing_t)v);
		kinc_compute_set_texture_minification_filter(*unit, (kinc_g4_texture_filter_t)min);
		kinc_compute_set_texture_magnification_filter(*unit, (kinc_g4_texture_filter_t)max);
		kinc_compute_set_texture_mipmap_filter(*unit, (kinc_g4_mipmap_filter_t)mip);

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_texture_3d_parameters_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                           void *callbackState) {
		kinc_compute_texture_unit *unit;
		JsGetExternalData(arguments[1], (void **)&unit);

		int u, v, w, min, max, mip;
		JsNumberToInt(arguments[2], &u);
		JsNumberToInt(arguments[3], &v);
		JsNumberToInt(arguments[4], &w);
		JsNumberToInt(arguments[5], &min);
		JsNumberToInt(arguments[6], &max);
		JsNumberToInt(arguments[7], &mip);

		kinc_compute_set_texture3d_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_U, (kinc_g4_texture_addressing_t)u);
		kinc_compute_set_texture3d_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_V, (kinc_g4_texture_addressing_t)v);
		kinc_compute_set_texture3d_addressing(*unit, KINC_G4_TEXTURE_DIRECTION_W, (kinc_g4_texture_addressing_t)w);
		kinc_compute_set_texture3d_minification_filter(*unit, (kinc_g4_texture_filter_t)min);
		kinc_compute_set_texture3d_magnification_filter(*unit, (kinc_g4_texture_filter_t)max);
		kinc_compute_set_texture3d_mipmap_filter(*unit, (kinc_g4_mipmap_filter_t)mip);

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_max_bound_textures(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                            void *callbackState) {
		JsValueRef value;
		JsIntToNumber(kinc_g4_max_bound_textures(), &value);
		return value;
	}

	JsValueRef CALLBACK krom_set_shader_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                            void *callbackState) {
		kinc_compute_shader_t *shader;
		JsGetExternalData(arguments[1], (void **)&shader);
		kinc_compute_set_shader(shader);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_create_shader_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                               void *callbackState) {
		Kore::u8 *content;
		unsigned bufferLength;
		JsGetArrayBufferStorage(arguments[1], &content, &bufferLength);

		kinc_compute_shader_t *shader = (kinc_compute_shader_t *)malloc(sizeof(kinc_compute_shader_t));
		kinc_compute_shader_init(shader, content, (int)bufferLength);

		JsValueRef value;
		JsCreateExternalObject(shader, nullptr, &value);
		return value;
	}

	JsValueRef CALLBACK krom_delete_shader_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                               void *callbackState) {
		kinc_compute_shader_t *shader;
		JsGetExternalData(arguments[1], (void **)&shader);
		kinc_compute_shader_destroy(shader);
		free(shader);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_get_constant_location_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                       void *callbackState) {
		kinc_compute_shader_t *shader;
		JsGetExternalData(arguments[1], (void **)&shader);

		size_t length;
		JsCopyString(arguments[2], tempString, tempStringSize, &length);
		tempString[length] = 0;

		kinc_compute_constant_location_t location = kinc_compute_shader_get_constant_location(shader, tempString);
		kinc_compute_constant_location_t *heapLocation = (kinc_compute_constant_location_t *)malloc(sizeof(kinc_compute_constant_location_t));
		memcpy(heapLocation, &location, sizeof(kinc_compute_constant_location_t));

		JsValueRef value;
		JsCreateExternalObject(heapLocation, nullptr, &value);

		return value;
	}

	JsValueRef CALLBACK krom_get_texture_unit_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
	                                                  void *callbackState) {
		kinc_compute_shader_t *shader;
		JsGetExternalData(arguments[1], (void **)&shader);

		size_t length;
		JsCopyString(arguments[2], tempString, tempStringSize, &length);
		tempString[length] = 0;

		kinc_compute_texture_unit_t unit = kinc_compute_shader_get_texture_unit(shader, tempString);
		kinc_compute_texture_unit_t *heapUnit = (kinc_compute_texture_unit_t *)malloc(sizeof(kinc_compute_texture_unit_t));
		memcpy(heapUnit, &unit, sizeof(kinc_compute_texture_unit_t));

		JsValueRef value;
		JsCreateExternalObject(heapUnit, nullptr, &value);

		return value;
	}

	JsValueRef CALLBACK krom_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int x, y, z;
		JsNumberToInt(arguments[1], &x);
		JsNumberToInt(arguments[2], &y);
		JsNumberToInt(arguments[3], &z);
		kinc_compute(x, y, z);
		return JS_INVALID_REFERENCE;
	}

#define addFunction(name, funcName)                                                                                                                            \
	JsPropertyIdRef name##Id;                                                                                                                                  \
	JsValueRef name##Func;                                                                                                                                     \
	JsCreateFunction(funcName, nullptr, &name##Func);                                                                                                          \
	JsCreatePropertyId(#name, strlen(#name), &name##Id);                                                                                                       \
	JsSetProperty(krom, name##Id, name##Func, false)

#define createId(name) JsCreatePropertyId(#name, strlen(#name), &name##_id)

	void bindFunctions() {
		createId(buffer);

		JsValueRef krom;
		JsCreateObject(&krom);

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

		JsValueRef global;
		JsGetGlobalObject(&global);

		JsSetProperty(global, getId("Krom"), krom, false);
	}

	JsSourceContext cookie = 1234;
	JsValueRef script, source;

	void initKrom(char *scriptfile) {
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

		JsCreateExternalArrayBuffer((void *)scriptfile, serialized ? serializedLength : (unsigned int)strlen(scriptfile), nullptr, nullptr, &script);
		JsCreateString("krom.js", strlen("krom.js"), &source);
	}

	void startKrom(char *scriptfile) {
		JsValueRef result;
		if (serialized) {
			JsRunSerialized(
			    script,
			    [](JsSourceContext sourceContext, JsValueRef *scriptBuffer, JsParseScriptAttributes *parseAttributes) {
				    fprintf(stderr, "krom.bin does not match this Krom version");
				    return false;
			    },
			    cookie, source, &result);
		}
		else {
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

	void serializeScript(char *code, char *outpath) {
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
		JsCreateExternalArrayBuffer((void *)code, (unsigned int)strlen(code), nullptr, nullptr, &codeObj);
		JsSerialize(codeObj, &bufferObj, JsParseScriptAttributeNone);
		Kore::u8 *buffer;
		unsigned bufferLength;
		JsGetArrayBufferStorage(bufferObj, &buffer, &bufferLength);

		FILE *file = fopen(outpath, "wb");
		if (file == nullptr) return;
		fwrite(buffer, 1, (int)bufferLength, file);
		fclose(file);
	}

	void endKrom() {
		JsSetCurrentContext(JS_INVALID_REFERENCE);
		JsDisposeRuntime(runtime);
	}

	void updateAudio(kinc_a2_buffer_t *buffer, int samples) {
		kinc_mutex_lock(&audioMutex);
		audioSamples += samples;
		kinc_mutex_unlock(&audioMutex);
	}

	void update() {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		if (enableSound) {
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
		}

		kinc_g4_begin(0);

		runJS();

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);

		kinc_g4_end(0);

		unsigned int nextIdleTick;
		JsIdle(&nextIdleTick);

		kinc_g4_swap_buffers();
	}

	void dropFiles(wchar_t *filePath) {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[2];
		JsGetUndefinedValue(&args[0]);
		size_t len = wcslen(filePath);
		if (sizeof(wchar_t) == 2) {
			JsCreateStringUtf16((const uint16_t *)filePath, len, &args[1]);
		}
		else {
			uint16_t *str = new uint16_t[len + 1];
			for (int i = 0; i < len; i++) str[i] = filePath[i];
			str[len] = 0;
			JsCreateStringUtf16(str, len, &args[1]);
			delete[] str;
		}
		JsValueRef result;
		JsCallFunction(dropFilesFunction, args, 2, &result);

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);
	}

	char cutCopyString[4096];

	char *copy() {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[1];
		JsGetUndefinedValue(&args[0]);
		JsValueRef result;
		JsCallFunction(copyFunction, args, 1, &result);

		JsValueRef stringValue;
		JsConvertValueToString(result, &stringValue);
		size_t length;
		JsCopyString(stringValue, nullptr, 0, &length);
		if (length > 4095) return nullptr;
		JsCopyString(stringValue, cutCopyString, 4095, &length);
		cutCopyString[length] = 0;

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);

		return cutCopyString;
	}

	char *cut() {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[1];
		JsGetUndefinedValue(&args[0]);
		JsValueRef result;
		JsCallFunction(cutFunction, args, 1, &result);

		JsValueRef stringValue;
		JsConvertValueToString(result, &stringValue);
		size_t length;
		JsCopyString(stringValue, nullptr, 0, &length);
		if (length > 4095) return nullptr;
		JsCopyString(stringValue, cutCopyString, 4095, &length);
		cutCopyString[length] = 0;

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);

		return cutCopyString;
	}

	void paste(char *data) {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[2];
		JsGetUndefinedValue(&args[0]);
		JsCreateString(data, strlen(data), &args[1]);
		JsValueRef result;
		JsCallFunction(pasteFunction, args, 2, &result);

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);
	}

	void foreground() {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[1];
		JsGetUndefinedValue(&args[0]);
		JsValueRef result;
		JsCallFunction(foregroundFunction, args, 1, &result);

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);
	}

	void resume() {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[1];
		JsGetUndefinedValue(&args[0]);
		JsValueRef result;
		JsCallFunction(resumeFunction, args, 1, &result);

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);
	}

	void pause() {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[1];
		JsGetUndefinedValue(&args[0]);
		JsValueRef result;
		JsCallFunction(pauseFunction, args, 1, &result);

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);
	}

	void background() {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[1];
		JsGetUndefinedValue(&args[0]);
		JsValueRef result;
		JsCallFunction(backgroundFunction, args, 1, &result);

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);
	}

	void shutdown() {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[1];
		JsGetUndefinedValue(&args[0]);
		JsValueRef result;
		JsCallFunction(shutdownFunction, args, 1, &result);

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);
	}

	void keyDown(int code) {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[2];
		JsGetUndefinedValue(&args[0]);
		JsIntToNumber((int)code, &args[1]);
		JsValueRef result;
		JsCallFunction(keyboardDownFunction, args, 2, &result);

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);
	}

	void keyUp(int code) {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[2];
		JsGetUndefinedValue(&args[0]);
		JsIntToNumber((int)code, &args[1]);
		JsValueRef result;
		JsCallFunction(keyboardUpFunction, args, 2, &result);

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);
	}

	void keyPress(unsigned int character) {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[2];
		JsGetUndefinedValue(&args[0]);
		JsIntToNumber((int)character, &args[1]);
		JsValueRef result;
		JsCallFunction(keyboardPressFunction, args, 2, &result);

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);
	}

	void mouseMove(int window, int x, int y, int mx, int my) {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[5];
		JsGetUndefinedValue(&args[0]);
		JsIntToNumber(x, &args[1]);
		JsIntToNumber(y, &args[2]);
		JsIntToNumber(mx, &args[3]);
		JsIntToNumber(my, &args[4]);
		JsValueRef result;
		JsCallFunction(mouseMoveFunction, args, 5, &result);

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);
	}

	void mouseDown(int window, int button, int x, int y) {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[4];
		JsGetUndefinedValue(&args[0]);
		JsIntToNumber(button, &args[1]);
		JsIntToNumber(x, &args[2]);
		JsIntToNumber(y, &args[3]);
		JsValueRef result;
		JsCallFunction(mouseDownFunction, args, 4, &result);

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);
	}

	void mouseUp(int window, int button, int x, int y) {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[4];
		JsGetUndefinedValue(&args[0]);
		JsIntToNumber(button, &args[1]);
		JsIntToNumber(x, &args[2]);
		JsIntToNumber(y, &args[3]);
		JsValueRef result;
		JsCallFunction(mouseUpFunction, args, 4, &result);

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);
	}

	void mouseWheel(int window, int delta) {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[2];
		JsGetUndefinedValue(&args[0]);
		JsIntToNumber(delta, &args[1]);
		JsValueRef result;
		JsCallFunction(mouseWheelFunction, args, 2, &result);

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);
	}

	void penDown(int window, int x, int y, float pressure) {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[4];
		JsGetUndefinedValue(&args[0]);
		JsIntToNumber(x, &args[1]);
		JsIntToNumber(y, &args[2]);
		JsDoubleToNumber(pressure, &args[3]);
		JsValueRef result;
		JsCallFunction(penDownFunction, args, 4, &result);

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);
	}

	void penUp(int window, int x, int y, float pressure) {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[4];
		JsGetUndefinedValue(&args[0]);
		JsIntToNumber(x, &args[1]);
		JsIntToNumber(y, &args[2]);
		JsDoubleToNumber(pressure, &args[3]);
		JsValueRef result;
		JsCallFunction(penUpFunction, args, 4, &result);

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);
	}

	void penMove(int window, int x, int y, float pressure) {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[4];
		JsGetUndefinedValue(&args[0]);
		JsIntToNumber(x, &args[1]);
		JsIntToNumber(y, &args[2]);
		JsDoubleToNumber(pressure, &args[3]);
		JsValueRef result;
		JsCallFunction(penMoveFunction, args, 4, &result);

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);
	}

	void gamepadAxis(int gamepad, int axis, float value) {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[4];
		JsGetUndefinedValue(&args[0]);
		JsIntToNumber(gamepad, &args[1]);
		JsIntToNumber(axis, &args[2]);
		JsDoubleToNumber(value, &args[3]);
		JsValueRef result;
		JsCallFunction(gamepadAxisFunction, args, 4, &result);

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);
	}

	void gamepadButton(int gamepad, int button, float value) {
		kinc_mutex_lock(&mutex);
		JsSetCurrentContext(context);

		JsValueRef args[4];
		JsGetUndefinedValue(&args[0]);
		JsIntToNumber(gamepad, &args[1]);
		JsIntToNumber(button, &args[2]);
		JsDoubleToNumber(value, &args[3]);
		JsValueRef result;
		JsCallFunction(gamepadButtonFunction, args, 4, &result);

		JsSetCurrentContext(JS_INVALID_REFERENCE);
		kinc_mutex_unlock(&mutex);
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
}

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

//__declspec(dllimport) extern "C" void __stdcall Sleep(unsigned long milliseconds);

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
					sendLogMessage("Krom uses Debug API version %i but your IDE targets Debug API version %i. Please update %s.", KROM_DEBUG_API,
					               message.data[1], outdated);
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
