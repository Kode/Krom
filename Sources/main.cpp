#include "pch.h"

#include <ChakraCore.h>

#include "Runtime.h"
#include "Core/AtomLockGuids.h"
#include "Core/ConfigParser.h"
#include "Base/ThreadContextTlsEntry.h"
#include "Base/ThreadBoundThreadContextManager.h"
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Kore/IO/FileReader.h>
#include <Kore/IO/FileWriter.h>
#include <Kore/Graphics4/Graphics.h>
#include <Kore/Graphics4/PipelineState.h>
#include <Kore/Graphics4/Shader.h>
#include <Kore/Compute/Compute.h>
#include <Kore/Input/Keyboard.h>
#include <Kore/Input/Mouse.h>
#include <Kore/Input/Pen.h>
#include <Kore/Input/Gamepad.h>
#include <Kore/Audio2/Audio.h>
#include <Kore/Audio1/Audio.h>
#include <Kore/Audio1/Sound.h>
#include <Kore/Audio1/SoundStream.h>
#include <Kore/Math/Random.h>
#include <Kore/System.h>
#include <Kore/Display.h>
#include <Kore/Log.h>
#include <Kore/Threads/Thread.h>
#include <Kore/Threads/Mutex.h>

#include "debug.h"

#include <stdio.h>
#include <stdarg.h>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef KORE_WINDOWS
#include <Windows.h> // AttachConsole
#endif

void sendMessage(const char* message);

#ifdef KORE_MACOS
const char* macgetresourcepath();
#endif

const char* getExeDir();

namespace {
	int _argc;
	char** _argv;
	bool debugMode = false;
	bool watch = false;
	bool nosound = false;
	bool nowindow = false;

	JsValueRef updateFunction;
	JsValueRef dropFilesFunction;
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

	Kore::Mutex mutex;

	void update();
	void initAudioBuffer();
	void mix(int samples);
	void dropFiles(wchar_t* filePath);
	void keyDown(Kore::KeyCode code);
	void keyUp(Kore::KeyCode code);
    void keyPress(wchar_t character);
	void mouseMove(int window, int x, int y, int mx, int my);
	void mouseDown(int window, int button, int x, int y);
	void mouseUp(int window, int button, int x, int y);
	void mouseWheel(int window, int delta);
	void penDown(int window, int x, int y, float pressure);
	void penUp(int window, int x, int y, float pressure);
	void penMove(int window, int x, int y, float pressure);
	void gamepad1Axis(int axis, float value);
	void gamepad1Button(int button, float value);
	void gamepad2Axis(int axis, float value);
	void gamepad2Button(int button, float value);
	void gamepad3Axis(int axis, float value);
	void gamepad3Button(int button, float value);
	void gamepad4Axis(int axis, float value);
	void gamepad4Button(int button, float value);

	JsPropertyIdRef getId(const char* name) {
		JsPropertyIdRef id;
		JsErrorCode err = JsCreatePropertyId(name, strlen(name), &id);
		assert(err == JsNoError);
		return id;
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

		Kore::WindowOptions win;
		win.title = title;
		win.width = width;
		win.height = height;
		win.x = -1;
		win.y = -1;
		win.visible = !nowindow;
		win.mode = Kore::WindowMode(windowMode);
		win.windowFeatures = windowFeatures;
		Kore::FramebufferOptions frame;
		frame.verticalSync = vSync;
		frame.samplesPerPixel = samplesPerPixel;
		Kore::System::init(title, width, height, &win, &frame);

		mutex.create();
		if (!nosound) {
			Kore::Audio2::audioCallback = mix;
			Kore::Audio2::init();
			initAudioBuffer();
		}
		Kore::Random::init((int)(Kore::System::time() * 1000));

		Kore::System::setCallback(update);
		Kore::System::setDropFilesCallback(dropFiles);

		Kore::Keyboard::the()->KeyDown = keyDown;
		Kore::Keyboard::the()->KeyUp = keyUp;
		Kore::Keyboard::the()->KeyPress = keyPress;
		Kore::Mouse::the()->Move = mouseMove;
		Kore::Mouse::the()->Press = mouseDown;
		Kore::Mouse::the()->Release = mouseUp;
		Kore::Mouse::the()->Scroll = mouseWheel;
		Kore::Pen::the()->Press = penDown;
		Kore::Pen::the()->Release = penUp;
		Kore::Pen::the()->Move = penMove;
		Kore::Gamepad::get(0)->Axis = gamepad1Axis;
		Kore::Gamepad::get(0)->Button = gamepad1Button;
		Kore::Gamepad::get(1)->Axis = gamepad2Axis;
		Kore::Gamepad::get(1)->Button = gamepad2Button;
		Kore::Gamepad::get(2)->Axis = gamepad3Axis;
		Kore::Gamepad::get(2)->Button = gamepad3Button;
		Kore::Gamepad::get(3)->Axis = gamepad4Axis;
		Kore::Gamepad::get(3)->Button = gamepad4Button;

		return JS_INVALID_REFERENCE;
	}

	void sendLogMessageArgs(const char* format, va_list args) {
		char message[4096];
		vsnprintf(message, sizeof(message) - 2, format, args);
		Kore::log(Kore::Info, "%s", message);

		if (debugMode) {
			char json[4096];
			strcpy(json, "{\"method\":\"Log.entryAdded\",\"params\":{\"entry\":{\"source\":\"javascript\",\"level\":\"log\",\"text\":\"");
			strcat(json, message);
			strcat(json, "\",\"timestamp\":0}}}");
			sendMessage(json);
		}
	}

	void sendLogMessage(const char* format, ...) {
		va_list args;
		va_start(args, format);
		sendLogMessageArgs(format, args);
		va_end(args);
	}

	JsValueRef CALLBACK LogCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		if (argumentCount < 2) {
			return JS_INVALID_REFERENCE;
		}
		char message[256];
		size_t length;
		JsCopyString(arguments[1], message, 255, &length);
		message[length] = 0;
		sendLogMessage(message);
	}

	JsValueRef CALLBACK graphics_clear(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int flags, color, stencil;
		JsNumberToInt(arguments[1], &flags);
		JsNumberToInt(arguments[2], &color);
		double depth;
		JsNumberToDouble(arguments[3], &depth);
		JsNumberToInt(arguments[4], &stencil);
		Kore::Graphics4::clear(flags, color, depth, stencil);
	}

	JsValueRef CALLBACK krom_set_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		updateFunction = arguments[1];
		JsAddRef(updateFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_drop_files_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		dropFilesFunction = arguments[1];
		JsAddRef(dropFilesFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_keyboard_down_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		keyboardDownFunction = arguments[1];
		JsAddRef(keyboardDownFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_keyboard_up_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		keyboardUpFunction = arguments[1];
		JsAddRef(keyboardUpFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_keyboard_press_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		keyboardPressFunction = arguments[1];
		JsAddRef(keyboardPressFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_mouse_down_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		mouseDownFunction = arguments[1];
		JsAddRef(mouseDownFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_mouse_up_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		mouseUpFunction = arguments[1];
		JsAddRef(mouseUpFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_mouse_move_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		mouseMoveFunction = arguments[1];
		JsAddRef(mouseMoveFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_mouse_wheel_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		mouseWheelFunction = arguments[1];
		JsAddRef(mouseWheelFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_pen_down_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		penDownFunction = arguments[1];
		JsAddRef(penDownFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_pen_up_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		penUpFunction = arguments[1];
		JsAddRef(penUpFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_pen_move_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		penMoveFunction = arguments[1];
		JsAddRef(penMoveFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_gamepad_axis_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		gamepadAxisFunction = arguments[1];
		JsAddRef(gamepadAxisFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_gamepad_button_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		gamepadButtonFunction = arguments[1];
		JsAddRef(gamepadButtonFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_lock_mouse(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Mouse::the()->lock(0);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_unlock_mouse(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Mouse::the()->unlock(0);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_can_lock_mouse(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef value;
		JsBoolToBoolean(Kore::Mouse::the()->canLock(0), &value);
		return value;
	}

	JsValueRef CALLBACK krom_is_mouse_locked(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef value;
		JsBoolToBoolean(Kore::Mouse::the()->isLocked(0), &value);
		return value;
	}

	JsValueRef CALLBACK krom_show_mouse(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		bool value;
		JsBooleanToBool(arguments[1], &value);
		Kore::Mouse::the()->show(value);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_audio_callback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		audioFunction = arguments[1];
		JsAddRef(audioFunction, nullptr);
		return JS_INVALID_REFERENCE;
	}

	// TODO: krom_audio_lock
	JsValueRef CALLBACK audio_thread(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		bool lock;
		JsBooleanToBool(arguments[1], &lock);

		if (lock) mutex.lock();    //v8::Locker::Locker(isolate);
		else mutex.unlock();       //v8::Unlocker(args.GetIsolate());
		
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_create_indexbuffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int count;
		JsNumberToInt(arguments[1], &count);
		JsValueRef ib;
		JsCreateExternalObject(new Kore::Graphics4::IndexBuffer(count), nullptr, &ib);
		return ib;
	}

	JsValueRef CALLBACK krom_delete_indexbuffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::IndexBuffer* buffer;
		JsGetExternalData(arguments[1], (void**)&buffer);
		delete buffer;
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_lock_index_buffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::IndexBuffer* buffer;
		JsGetExternalData(arguments[1], (void**)&buffer);
		int* indices = buffer->lock();
		JsValueRef value;
		JsCreateExternalArrayBuffer(indices, buffer->count() * sizeof(int), nullptr, nullptr, &value);
		JsValueRef array;
		JsCreateTypedArray(JsArrayTypeUint32, value, 0, buffer->count(), &array);
		return array;
	}

	JsValueRef CALLBACK krom_unlock_index_buffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::IndexBuffer* buffer;
		JsGetExternalData(arguments[1], (void**)&buffer);
		buffer->unlock();
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_indexbuffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::IndexBuffer* buffer;
		JsGetExternalData(arguments[1], (void**)&buffer);
		Kore::Graphics4::setIndexBuffer(*buffer);
		return JS_INVALID_REFERENCE;
	}

	Kore::Graphics4::VertexData convertVertexData(int num) {
		switch (num) {
		case 0:
			return Kore::Graphics4::Float1VertexData;
		case 1:
			return Kore::Graphics4::Float2VertexData;
		case 2:
			return Kore::Graphics4::Float3VertexData;
		case 3:
			return Kore::Graphics4::Float4VertexData;
		case 4:
			return Kore::Graphics4::Float4x4VertexData;
		}
		return Kore::Graphics4::Float1VertexData;
	}

	JsValueRef CALLBACK krom_create_vertexbuffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();

		Local<Object> jsstructure = args[1]->ToObject();
		int32_t length = jsstructure->Get(String::NewFromUtf8(isolate, "length"))->ToInt32()->Value();
		Kore::Graphics4::VertexStructure structure;
		for (int32_t i = 0; i < length; ++i) {
			Local<Object> element = jsstructure->Get(i)->ToObject();
			Local<Value> str = element->Get(String::NewFromUtf8(isolate, "name"));
			String::Utf8Value utf8_value(str);
			Local<Object> dataobj = element->Get(String::NewFromUtf8(isolate, "data"))->ToObject();
			int32_t data = dataobj->Get(1)->ToInt32()->Value();
			char* name = new char[32]; // TODO
			strcpy(name, *utf8_value);
			structure.add(name, convertVertexData(data));
		}

		obj->SetInternalField(0, External::New(isolate, new Kore::Graphics4::VertexBuffer(args[0]->Int32Value(), structure, (Kore::Graphics4::Usage)args[2]->Int32Value(), args[3]->Int32Value())));
		args.GetReturnValue().Set(obj);
	}

	JsValueRef CALLBACK krom_delete_vertexbuffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::VertexBuffer* buffer;
		JsGetExternalData(arguments[1], (void**)&buffer);
		delete buffer;
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_lock_vertex_buffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::VertexBuffer* buffer;
		JsGetExternalData(arguments[1], (void**)&buffer);
		float* vertices = buffer->lock();
		JsValueRef value;
		JsCreateExternalArrayBuffer(vertices, buffer->count() * buffer->stride(), nullptr, nullptr, &value);
		JsValueRef array;
		JsCreateTypedArray(JsArrayTypeFloat32, value, 0, buffer->count() * buffer->stride() / 4, &array);
		return array;
	}

	JsValueRef CALLBACK krom_unlock_vertex_buffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::VertexBuffer* buffer;
		JsGetExternalData(arguments[1], (void**)&buffer);
		buffer->unlock();
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_vertexbuffer(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::VertexBuffer* buffer;
		JsGetExternalData(arguments[1], (void**)&buffer);
		Kore::Graphics4::setVertexBuffer(*buffer);
		return JS_INVALID_REFERENCE;
	}

	void krom_set_vertexbuffers(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Kore::Graphics4::VertexBuffer* vertexBuffers[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
		Local<Object> jsarray = args[0]->ToObject();
		int32_t length = jsarray->Get(String::NewFromUtf8(isolate, "length"))->ToInt32()->Value();
		for (int32_t i = 0; i < length; ++i) {
			Local<Object> bufferobj = jsarray->Get(i)->ToObject()->Get(String::NewFromUtf8(isolate, "buffer"))->ToObject();
			Local<External> bufferfield = Local<External>::Cast(bufferobj->GetInternalField(0));
			Kore::Graphics4::VertexBuffer* buffer = (Kore::Graphics4::VertexBuffer*)bufferfield->Value();
			vertexBuffers[i] = buffer;
		}
		Kore::Graphics4::setVertexBuffers(vertexBuffers, length);
	}

	JsValueRef CALLBACK krom_draw_indexed_vertices(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int start, count;
		JsNumberToInt(arguments[1], &start);
		JsNumberToInt(arguments[2], &count);
		if (count < 0) Kore::Graphics4::drawIndexedVertices();
		else Kore::Graphics4::drawIndexedVertices(start, count);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_draw_indexed_vertices_instanced(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int instanceCount, start, count;
		JsNumberToInt(arguments[1], &instanceCount);
		JsNumberToInt(arguments[2], &start);
		JsNumberToInt(arguments[3], &count);
		if (count < 0) Kore::Graphics4::drawIndexedVerticesInstanced(instanceCount);
		else Kore::Graphics4::drawIndexedVerticesInstanced(instanceCount, start, count);
		return JS_INVALID_REFERENCE;
	}

	std::string replace(std::string str, char a, char b) {
		for (size_t i = 0; i < str.size(); ++i) {
			if (str[i] == a) str[i] = b;
		}
		return str;
	}

	void krom_create_vertex_shader(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
		ArrayBuffer::Contents content;
		if (buffer->IsExternal()) content = buffer->GetContents();
		else content = buffer->Externalize();
		Kore::Graphics4::Shader* shader = new Kore::Graphics4::Shader(content.Data(), (int)content.ByteLength(), Kore::Graphics4::VertexShader);

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, shader));
		obj->Set(String::NewFromUtf8(isolate, "name"), args[1]);
		args.GetReturnValue().Set(obj);
	}

	void krom_create_vertex_shader_from_source(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		String::Utf8Value utf8_value(args[0]);
		char* source = new char[strlen(*utf8_value) + 1];
		strcpy(source, *utf8_value);
		Kore::Graphics4::Shader* shader = new Kore::Graphics4::Shader(source, Kore::Graphics4::VertexShader);

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, shader));
		Local<String> name = String::NewFromUtf8(isolate, "");
		obj->Set(String::NewFromUtf8(isolate, "name"), name);
		args.GetReturnValue().Set(obj);
	}

	void krom_create_fragment_shader(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
		ArrayBuffer::Contents content;
		if (buffer->IsExternal()) content = buffer->GetContents();
		else content = buffer->Externalize();
		Kore::Graphics4::Shader* shader = new Kore::Graphics4::Shader(content.Data(), (int)content.ByteLength(), Kore::Graphics4::FragmentShader);

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, shader));
		obj->Set(String::NewFromUtf8(isolate, "name"), args[1]);
		args.GetReturnValue().Set(obj);
	}

	void krom_create_fragment_shader_from_source(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		String::Utf8Value utf8_value(args[0]);
		char* source = new char[strlen(*utf8_value) + 1];
		strcpy(source, *utf8_value);
		Kore::Graphics4::Shader* shader = new Kore::Graphics4::Shader(source, Kore::Graphics4::FragmentShader);

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, shader));
		Local<String> name = String::NewFromUtf8(isolate, "");
		obj->Set(String::NewFromUtf8(isolate, "name"), name);
		args.GetReturnValue().Set(obj);
	}

	void krom_create_geometry_shader(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
		ArrayBuffer::Contents content;
		if (buffer->IsExternal()) content = buffer->GetContents();
		else content = buffer->Externalize();
		Kore::Graphics4::Shader* shader = new Kore::Graphics4::Shader(content.Data(), (int)content.ByteLength(), Kore::Graphics4::GeometryShader);

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, shader));
		obj->Set(String::NewFromUtf8(isolate, "name"), args[1]);
		args.GetReturnValue().Set(obj);
	}

	void krom_create_tessellation_control_shader(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
		ArrayBuffer::Contents content;
		if (buffer->IsExternal()) content = buffer->GetContents();
		else content = buffer->Externalize();
		Kore::Graphics4::Shader* shader = new Kore::Graphics4::Shader(content.Data(), (int)content.ByteLength(), Kore::Graphics4::TessellationControlShader);

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, shader));
		obj->Set(String::NewFromUtf8(isolate, "name"), args[1]);
		args.GetReturnValue().Set(obj);
	}

	void krom_create_tessellation_evaluation_shader(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
		ArrayBuffer::Contents content;
		if (buffer->IsExternal()) content = buffer->GetContents();
		else content = buffer->Externalize();
		Kore::Graphics4::Shader* shader = new Kore::Graphics4::Shader(content.Data(), (int)content.ByteLength(), Kore::Graphics4::TessellationEvaluationShader);

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, shader));
		obj->Set(String::NewFromUtf8(isolate, "name"), args[1]);
		args.GetReturnValue().Set(obj);
	}

	JsValueRef CALLBACK krom_delete_shader(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::Shader* shader;
		JsGetExternalData(arguments[1], (void**)&shader);
		delete shader;
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_create_pipeline(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::PipelineState* pipeline = new Kore::Graphics4::PipelineState;
		JsValueRef pipelineObj;
		JsCreateExternalObject(pipeline, nullptr, &pipelineObj);

		JsValueRef obj;
		JsCreateObject(&obj);

		JsValueRef zero;
		JsIntToNumber(0, &zero);

		JsSetIndexedProperty(obj, zero, pipelineObj);

		return obj;
	}

	JsValueRef CALLBACK krom_delete_pipeline(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef pipelineObj, zero;
		JsIntToNumber(0, &zero);
		JsGetIndexedProperty(arguments[1], zero, &pipelineObj);
		Kore::Graphics4::PipelineState* pipeline;
		JsGetExternalData(pipelineObj, (void**)&pipeline);
		delete pipeline;
		return JS_INVALID_REFERENCE;
	}

	void recompilePipeline(Local<Object> projobj) {
		Local<External> structsfield = Local<External>::Cast(projobj->GetInternalField(1));
		Kore::Graphics4::VertexStructure** structures = (Kore::Graphics4::VertexStructure**)structsfield->Value();

		Local<External> sizefield = Local<External>::Cast(projobj->GetInternalField(2));
		int32_t size = sizefield->ToInt32()->Value();

		Local<External> vsfield = Local<External>::Cast(projobj->GetInternalField(3));
		Kore::Graphics4::Shader* vs = (Kore::Graphics4::Shader*)vsfield->Value();

		Local<External> fsfield = Local<External>::Cast(projobj->GetInternalField(4));
		Kore::Graphics4::Shader* fs = (Kore::Graphics4::Shader*)fsfield->Value();

		Kore::Graphics4::PipelineState* pipeline = new Kore::Graphics4::PipelineState;
		pipeline->vertexShader = vs;
		pipeline->fragmentShader = fs;

		Local<External> gsfield = Local<External>::Cast(projobj->GetInternalField(5));
		if (!gsfield->IsNull() && !gsfield->IsUndefined()) {
			Kore::Graphics4::Shader* gs = (Kore::Graphics4::Shader*)gsfield->Value();
			pipeline->geometryShader = gs;
		}

		Local<External> tcsfield = Local<External>::Cast(projobj->GetInternalField(6));
		if (!tcsfield->IsNull() && !tcsfield->IsUndefined()) {
			Kore::Graphics4::Shader* tcs = (Kore::Graphics4::Shader*)tcsfield->Value();
			pipeline->tessellationControlShader = tcs;
		}

		Local<External> tesfield = Local<External>::Cast(projobj->GetInternalField(7));
		if (!tesfield->IsNull() && !tesfield->IsUndefined()) {
			Kore::Graphics4::Shader* tes = (Kore::Graphics4::Shader*)tesfield->Value();
			pipeline->tessellationEvaluationShader = tes;
		}

		for (int i = 0; i < size; ++i) {
			pipeline->inputLayout[i] = structures[i];
		}
		pipeline->inputLayout[size] = nullptr;

		pipeline->compile();

		projobj->SetInternalField(0, External::New(isolate, pipeline));
	}

	void krom_compile_pipeline(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());

		Local<Object> progobj = args[0]->ToObject();

		Local<External> progfield = Local<External>::Cast(progobj->GetInternalField(0));
		Kore::Graphics4::PipelineState* pipeline = (Kore::Graphics4::PipelineState*)progfield->Value();

		Kore::Graphics4::VertexStructure s0, s1, s2, s3;
		Kore::Graphics4::VertexStructure* structures[4] = { &s0, &s1, &s2, &s3 };

		int32_t size = args[5]->ToObject()->ToInt32()->Value();
		for (int32_t i1 = 0; i1 < size; ++i1) {
			Local<Object> jsstructure = args[i1 + 1]->ToObject();
			int32_t length = jsstructure->Get(String::NewFromUtf8(isolate, "length"))->ToInt32()->Value();
			for (int32_t i2 = 0; i2 < length; ++i2) {
				Local<Object> element = jsstructure->Get(i2)->ToObject();
				Local<Value> str = element->Get(String::NewFromUtf8(isolate, "name"));
				String::Utf8Value utf8_value(str);
				Local<Object> dataobj = element->Get(String::NewFromUtf8(isolate, "data"))->ToObject();
				int32_t data = dataobj->Get(1)->ToInt32()->Value();
				char* name = new char[32]; // TODO
				strcpy(name, *utf8_value);
				structures[i1]->add(name, convertVertexData(data));
			}
		}

		progobj->SetInternalField(1, External::New(isolate, structures));
		progobj->SetInternalField(2, External::New(isolate, &size));

		Local<External> vsfield = Local<External>::Cast(args[6]->ToObject()->GetInternalField(0));
		Kore::Graphics4::Shader* vertexShader = (Kore::Graphics4::Shader*)vsfield->Value();
		progobj->SetInternalField(3, External::New(isolate, vertexShader));
		progobj->Set(String::NewFromUtf8(isolate, "vsname"), args[6]->ToObject()->Get(String::NewFromUtf8(isolate, "name")));

		Local<External> fsfield = Local<External>::Cast(args[7]->ToObject()->GetInternalField(0));
		Kore::Graphics4::Shader* fragmentShader = (Kore::Graphics4::Shader*)fsfield->Value();
		progobj->SetInternalField(4, External::New(isolate, fragmentShader));
		progobj->Set(String::NewFromUtf8(isolate, "fsname"), args[7]->ToObject()->Get(String::NewFromUtf8(isolate, "name")));

		pipeline->vertexShader = vertexShader;
		pipeline->fragmentShader = fragmentShader;

		if (!args[8]->IsNull() && !args[8]->IsUndefined()) {
			Local<External> gsfield = Local<External>::Cast(args[8]->ToObject()->GetInternalField(0));
			Kore::Graphics4::Shader* geometryShader = (Kore::Graphics4::Shader*)gsfield->Value();
			progobj->SetInternalField(5, External::New(isolate, geometryShader));
			progobj->Set(String::NewFromUtf8(isolate, "gsname"), args[8]->ToObject()->Get(String::NewFromUtf8(isolate, "name")));
			pipeline->geometryShader = geometryShader;
		}

		if (!args[9]->IsNull() && !args[9]->IsUndefined()) {
			Local<External> tcsfield = Local<External>::Cast(args[9]->ToObject()->GetInternalField(0));
			Kore::Graphics4::Shader* tessellationControlShader = (Kore::Graphics4::Shader*)tcsfield->Value();
			progobj->SetInternalField(6, External::New(isolate, tessellationControlShader));
			progobj->Set(String::NewFromUtf8(isolate, "tcsname"), args[9]->ToObject()->Get(String::NewFromUtf8(isolate, "name")));
			pipeline->tessellationControlShader = tessellationControlShader;
		}

		if (!args[10]->IsNull() && !args[10]->IsUndefined()) {
			Local<External> tesfield = Local<External>::Cast(args[10]->ToObject()->GetInternalField(0));
			Kore::Graphics4::Shader* tessellationEvaluationShader = (Kore::Graphics4::Shader*)tesfield->Value();
			progobj->SetInternalField(7, External::New(isolate, tessellationEvaluationShader));
			progobj->Set(String::NewFromUtf8(isolate, "tesname"), args[10]->ToObject()->Get(String::NewFromUtf8(isolate, "name")));
			pipeline->tessellationEvaluationShader = tessellationEvaluationShader;
		}

		for (int i = 0; i < size; ++i) {
			pipeline->inputLayout[i] = structures[i];
		}
		pipeline->inputLayout[size] = nullptr;

		pipeline->cullMode = (Kore::Graphics4::CullMode)args[11]->ToObject()->Get(String::NewFromUtf8(isolate, "cullMode"))->Int32Value();

		pipeline->depthWrite = args[11]->ToObject()->Get(String::NewFromUtf8(isolate, "depthWrite"))->BooleanValue();
		pipeline->depthMode = (Kore::Graphics4::ZCompareMode)args[11]->ToObject()->Get(String::NewFromUtf8(isolate, "depthMode"))->Int32Value();

		pipeline->stencilMode = (Kore::Graphics4::ZCompareMode)args[11]->ToObject()->Get(String::NewFromUtf8(isolate, "stencilMode"))->Int32Value();
		pipeline->stencilBothPass = (Kore::Graphics4::StencilAction)args[11]->ToObject()->Get(String::NewFromUtf8(isolate, "stencilBothPass"))->Int32Value();
		pipeline->stencilDepthFail = (Kore::Graphics4::StencilAction)args[11]->ToObject()->Get(String::NewFromUtf8(isolate, "stencilDepthFail"))->Int32Value();
		pipeline->stencilFail = (Kore::Graphics4::StencilAction)args[11]->ToObject()->Get(String::NewFromUtf8(isolate, "stencilFail"))->Int32Value();
		pipeline->stencilReferenceValue = args[11]->ToObject()->Get(String::NewFromUtf8(isolate, "stencilReferenceValue"))->Int32Value();
		pipeline->stencilReadMask = args[11]->ToObject()->Get(String::NewFromUtf8(isolate, "stencilReadMask"))->Int32Value();
		pipeline->stencilWriteMask = args[11]->ToObject()->Get(String::NewFromUtf8(isolate, "stencilWriteMask"))->Int32Value();

		pipeline->blendSource = (Kore::Graphics4::BlendingOperation)args[11]->ToObject()->Get(String::NewFromUtf8(isolate, "blendSource"))->Int32Value();
		pipeline->blendDestination = (Kore::Graphics4::BlendingOperation)args[11]->ToObject()->Get(String::NewFromUtf8(isolate, "blendDestination"))->Int32Value();
		pipeline->alphaBlendSource = (Kore::Graphics4::BlendingOperation)args[11]->ToObject()->Get(String::NewFromUtf8(isolate, "alphaBlendSource"))->Int32Value();
		pipeline->alphaBlendDestination = (Kore::Graphics4::BlendingOperation)args[11]->ToObject()->Get(String::NewFromUtf8(isolate, "alphaBlendDestination"))->Int32Value();

		pipeline->colorWriteMaskRed = args[11]->ToObject()->Get(String::NewFromUtf8(isolate, "colorWriteMaskRed"))->BooleanValue();
		pipeline->colorWriteMaskGreen = args[11]->ToObject()->Get(String::NewFromUtf8(isolate, "colorWriteMaskGreen"))->BooleanValue();
		pipeline->colorWriteMaskBlue = args[11]->ToObject()->Get(String::NewFromUtf8(isolate, "colorWriteMaskBlue"))->BooleanValue();
		pipeline->colorWriteMaskAlpha = args[11]->ToObject()->Get(String::NewFromUtf8(isolate, "colorWriteMaskAlpha"))->BooleanValue();

		pipeline->conservativeRasterization = args[11]->ToObject()->Get(String::NewFromUtf8(isolate, "conservativeRasterization"))->BooleanValue();

		pipeline->compile();
	}

	std::string shadersdir;

	void krom_set_pipeline(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<Object> progobj = args[0]->ToObject();
		Local<External> progfield = Local<External>::Cast(progobj->GetInternalField(0));
		Kore::Graphics4::PipelineState* pipeline = (Kore::Graphics4::PipelineState*)progfield->Value();

		if (debugMode) {
			Local<Value> vsnameobj = progobj->Get(String::NewFromUtf8(isolate, "vsname"));
			String::Utf8Value vsname(vsnameobj);

			Local<Value> fsnameobj = progobj->Get(String::NewFromUtf8(isolate, "fsname"));
			String::Utf8Value fsname(fsnameobj);

			bool shaderChanged = false;

			if (shaderChanges[*vsname]) {
				shaderChanged = true;
				sendLogMessage("Reloading shader %s.", *vsname);
				std::string filename = shaderFileNames[*vsname];
				std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary);
				std::vector<char> buffer((std::istreambuf_iterator<char>(input)), (std::istreambuf_iterator<char>()));
				Kore::Graphics4::Shader* vertexShader = new Kore::Graphics4::Shader(buffer.data(), (int)buffer.size(), Kore::Graphics4::VertexShader);
				progobj->SetInternalField(3, External::New(isolate, vertexShader));
				shaderChanges[*vsname] = false;
			}

			if (shaderChanges[*fsname]) {
				shaderChanged = true;
				sendLogMessage("Reloading shader %s.", *fsname);
				std::string filename = shaderFileNames[*fsname];
				std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary);
				std::vector<char> buffer((std::istreambuf_iterator<char>(input)), (std::istreambuf_iterator<char>()));
				Kore::Graphics4::Shader* fragmentShader = new Kore::Graphics4::Shader(buffer.data(), (int)buffer.size(), Kore::Graphics4::FragmentShader);
				progobj->SetInternalField(4, External::New(isolate, fragmentShader));
				shaderChanges[*fsname] = false;
			}

			Local<Value> gsnameobj = progobj->Get(String::NewFromUtf8(isolate, "gsname"));
			if (!gsnameobj->IsNull() && !gsnameobj->IsUndefined()) {
				String::Utf8Value gsname(gsnameobj);
				if (shaderChanges[*gsname]) {
					shaderChanged = true;
					sendLogMessage("Reloading shader %s.", *gsname);
					std::string filename = shaderFileNames[*gsname];
					std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary);
					std::vector<char> buffer((std::istreambuf_iterator<char>(input)), (std::istreambuf_iterator<char>()));
					Kore::Graphics4::Shader* geometryShader = new Kore::Graphics4::Shader(buffer.data(), (int)buffer.size(), Kore::Graphics4::GeometryShader);
					progobj->SetInternalField(5, External::New(isolate, geometryShader));
					shaderChanges[*gsname] = false;
				}
			}

			Local<Value> tcsnameobj = progobj->Get(String::NewFromUtf8(isolate, "tcsname"));
			if (!tcsnameobj->IsNull() && !tcsnameobj->IsUndefined()) {
				String::Utf8Value tcsname(tcsnameobj);
				if (shaderChanges[*tcsname]) {
					shaderChanged = true;
					sendLogMessage("Reloading shader %s.", *tcsname);
					std::string filename = shaderFileNames[*tcsname];
					std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary);
					std::vector<char> buffer((std::istreambuf_iterator<char>(input)), (std::istreambuf_iterator<char>()));
					Kore::Graphics4::Shader* tessellationControlShader = new Kore::Graphics4::Shader(buffer.data(), (int)buffer.size(), Kore::Graphics4::TessellationControlShader);
					progobj->SetInternalField(6, External::New(isolate, tessellationControlShader));
					shaderChanges[*tcsname] = false;
				}
			}

			Local<Value> tesnameobj = progobj->Get(String::NewFromUtf8(isolate, "tesname"));
			if (!tesnameobj->IsNull() && !tesnameobj->IsUndefined()) {
				String::Utf8Value tesname(tesnameobj);
				if (shaderChanges[*tesname]) {
					shaderChanged = true;
					sendLogMessage("Reloading shader %s.", *tesname);
					std::string filename = shaderFileNames[*tesname];
					std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary);
					std::vector<char> buffer((std::istreambuf_iterator<char>(input)), (std::istreambuf_iterator<char>()));
					Kore::Graphics4::Shader* tessellationEvaluationShader = new Kore::Graphics4::Shader(buffer.data(), (int)buffer.size(), Kore::Graphics4::TessellationEvaluationShader);
					progobj->SetInternalField(7, External::New(isolate, tessellationEvaluationShader));
					shaderChanges[*tesname] = false;
				}
			}

			if (shaderChanged) {
				recompilePipeline(progobj);
				Local<External> progfield = Local<External>::Cast(progobj->GetInternalField(0));
				pipeline = (Kore::Graphics4::PipelineState*)progfield->Value();
			}
		}

		Kore::Graphics4::setPipeline(pipeline);
	}

	JsValueRef CALLBACK krom_load_image(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		char filename[256];
		size_t length;
		JsCopyString(arguments[1], filename, 255, &length);
		filename[length] = 0;
		bool readable;
		JsBooleanToBool(arguments[2], &readable);
		Kore::Graphics4::Texture* texture = new Kore::Graphics4::Texture(filename, readable);

		JsValueRef obj;
		JsCreateExternalObject(texture, nullptr, &obj);
		JsValueRef width, height, realWidth, realHeight;
		JsIntToNumber(texture->width, &width);
		JsSetProperty(obj, getId("width"), width, false);
		JsIntToNumber(texture->height, &height);
		JsSetProperty(obj, getId("height"), height, false);
		JsIntToNumber(texture->texWidth, &realWidth);
		JsSetProperty(obj, getId("realWidth"), realWidth, false);
		JsIntToNumber(texture->texHeight, &realHeight);
		JsSetProperty(obj, getId("realHeight"), realHeight, false);
		JsSetProperty(obj, getId("filename"), arguments[1], false);
		return obj;
	}

	void krom_unload_image(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		if (args[0]->IsNull() || args[0]->IsUndefined()) return;
		Local<Object> image = args[0]->ToObject();
		Local<Value> tex = image->Get(String::NewFromUtf8(isolate, "texture_"));
		Local<Value> rt = image->Get(String::NewFromUtf8(isolate, "renderTarget_"));

		if (tex->IsObject()) {
			Local<External> texfield = Local<External>::Cast(tex->ToObject()->GetInternalField(0));
			Kore::Graphics4::Texture* texture = (Kore::Graphics4::Texture*)texfield->Value();
			delete texture;
		}
		else if (rt->IsObject()) {
			Local<External> rtfield = Local<External>::Cast(rt->ToObject()->GetInternalField(0));
			Kore::Graphics4::RenderTarget* renderTarget = (Kore::Graphics4::RenderTarget*)rtfield->Value();
			delete renderTarget;
		}
	}

	void krom_load_sound(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		String::Utf8Value utf8_value(args[0]);

		Kore::Sound* sound = new Kore::Sound(*utf8_value);
		Local<ArrayBuffer> buffer;
		ArrayBuffer::Contents content;
		buffer = ArrayBuffer::New(isolate, sound->size * 2 * sizeof(float));
		content = buffer->Externalize();
		float* to = (float*)content.Data();

		Kore::s16* left = (Kore::s16*)&sound->left[0];
		Kore::s16* right = (Kore::s16*)&sound->right[0];
		for (int i = 0; i < sound->size; i += 1) {
			to[i * 2 + 0] = (float)(left [i] / 32767.0);
			to[i * 2 + 1] = (float)(right[i] / 32767.0);
		}

		args.GetReturnValue().Set(buffer);
		delete sound;
	}

	void write_audio_buffer(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		float value = (float)args[0]->ToNumber()->Value();

		*(float*)&Kore::Audio2::buffer.data[Kore::Audio2::buffer.writeLocation] = value;
		Kore::Audio2::buffer.writeLocation += 4;
		if (Kore::Audio2::buffer.writeLocation >= Kore::Audio2::buffer.dataSize) Kore::Audio2::buffer.writeLocation = 0;
	}

	JsValueRef CALLBACK krom_load_blob(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		char filename[256];
		size_t length;
		JsCopyString(arguments[1], filename, 255, &length);
		filename[length] = 0;

		Kore::FileReader reader;
		reader.open(filename);

		JsValueRef array;
		JsCreateArrayBuffer(reader.size(), &array);

		Kore::u8* contents;
		unsigned contentsLength;
		JsGetArrayBufferStorage(array, &contents, &contentsLength);

		memcpy(contents, reader.readAll(), reader.size());

		reader.close();

		return array;
	}

	JsValueRef CALLBACK krom_get_constant_location(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef zero, pipelineObj;
		JsIntToNumber(0, &zero);
		JsGetIndexedProperty(arguments[1], zero, &pipelineObj);
		Kore::Graphics4::PipelineState* pipeline;
		JsGetExternalData(pipelineObj, (void**)&pipeline);

		char name[256];
		size_t length;
		JsCopyString(arguments[2], name, 255, &length);
		name[length] = 0;
		Kore::Graphics4::ConstantLocation location = pipeline->getConstantLocation(name);

		JsValueRef obj;
		JsCreateExternalObject(new Kore::Graphics4::ConstantLocation(location), nullptr, &obj);
		return obj;
	}

	JsValueRef CALLBACK krom_get_texture_unit(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef zero, pipelineObj;
		JsIntToNumber(0, &zero);
		JsGetIndexedProperty(arguments[1], zero, &pipelineObj);
		Kore::Graphics4::PipelineState* pipeline;
		JsGetExternalData(pipelineObj, (void**)&pipeline);

		char name[256];
		size_t length;
		JsCopyString(arguments[2], name, 255, &length);
		name[length] = 0;
		Kore::Graphics4::TextureUnit unit = pipeline->getTextureUnit(name);

		JsValueRef obj;
		JsCreateExternalObject(new Kore::Graphics4::TextureUnit(unit), nullptr, &obj);
		return obj;
	}

	void krom_set_texture(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> unitfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::TextureUnit* unit = (Kore::Graphics4::TextureUnit*)unitfield->Value();

		if (args[1]->IsNull() || args[1]->IsUndefined()) return;

		Local<Object> image = args[1]->ToObject();
		Local<Value> tex = image->Get(String::NewFromUtf8(isolate, "texture_"));
		if (tex->IsObject()) {
			Kore::Graphics4::Texture* texture;
			bool imageChanged = false;
			if (debugMode) {
				String::Utf8Value filename(tex->ToObject()->Get(String::NewFromUtf8(isolate, "filename")));
				if (imageChanges[*filename]) {
					imageChanges[*filename] = false;
					sendLogMessage("Image %s changed.", *filename);
					texture = new Kore::Graphics4::Texture(*filename);
					tex->ToObject()->SetInternalField(0, External::New(isolate, texture));
					imageChanged = true;
				}
			}
			if (!imageChanged) {
				Local<External> texfield = Local<External>::Cast(tex->ToObject()->GetInternalField(0));
				texture = (Kore::Graphics4::Texture*)texfield->Value();
			}
			Kore::Graphics4::setTexture(*unit, texture);
		}
		else {
			Local<Value> rt = image->Get(String::NewFromUtf8(isolate, "renderTarget_"));
			Local<External> rtfield = Local<External>::Cast(rt->ToObject()->GetInternalField(0));
			Kore::Graphics4::RenderTarget* renderTarget = (Kore::Graphics4::RenderTarget*)rtfield->Value();
			renderTarget->useColorAsTexture(*unit);
		}
	}

	void krom_set_texture_depth(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> unitfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::TextureUnit* unit = (Kore::Graphics4::TextureUnit*)unitfield->Value();

		if (args[1]->IsNull() || args[1]->IsUndefined()) return;

		Local<Object> image = args[1]->ToObject();
		Local<Value> rt = image->Get(String::NewFromUtf8(isolate, "renderTarget_"));
		if (rt->IsObject()) {
			Local<External> rtfield = Local<External>::Cast(rt->ToObject()->GetInternalField(0));
			Kore::Graphics4::RenderTarget* renderTarget = (Kore::Graphics4::RenderTarget*)rtfield->Value();
			renderTarget->useDepthAsTexture(*unit);
		}
	}

	void krom_set_image_texture(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> unitfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::TextureUnit* unit = (Kore::Graphics4::TextureUnit*)unitfield->Value();

		if (args[1]->IsNull() || args[1]->IsUndefined()) return;

		Local<Object> image = args[1]->ToObject();
		Local<Value> tex = image->Get(String::NewFromUtf8(isolate, "texture_"));
		if (tex->IsObject()) {
			Local<External> texfield = Local<External>::Cast(tex->ToObject()->GetInternalField(0));
			Kore::Graphics4::Texture* texture = (Kore::Graphics4::Texture*)texfield->Value();
			Kore::Graphics4::setImageTexture(*unit, texture);
		}
	}

	Kore::Graphics4::TextureAddressing convertTextureAddressing(int index) {
		switch (index) {
		default:
		case 0: // Repeat
			return Kore::Graphics4::Repeat;
		case 1: // Mirror
			return Kore::Graphics4::Mirror;
		case 2: // Clamp
			return Kore::Graphics4::Clamp;
		}
	}

	Kore::Graphics4::TextureFilter convertTextureFilter(int index) {
		switch (index) {
		default:
		case 0: // PointFilter
			return Kore::Graphics4::PointFilter;
		case 1: // LinearFilter
			return Kore::Graphics4::LinearFilter;
		case 2: // AnisotropicFilter
			return Kore::Graphics4::AnisotropicFilter;
		}
	}

	Kore::Graphics4::MipmapFilter convertMipmapFilter(int index) {
		switch (index) {
		default:
		case 0: // NoMipFilter
			return Kore::Graphics4::NoMipFilter;
		case 1: // PointMipFilter
			return Kore::Graphics4::PointMipFilter;
		case 2: // LinearMipFilter
			return Kore::Graphics4::LinearMipFilter;
		}
	}

	JsValueRef CALLBACK krom_set_texture_parameters(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::TextureUnit* unit;
		JsGetExternalData(arguments[1], (void**)&unit);
		int u, v, min, max, mip;
		JsNumberToInt(arguments[2], &u);
		JsNumberToInt(arguments[3], &v);
		JsNumberToInt(arguments[4], &min);
		JsNumberToInt(arguments[5], &max);
		JsNumberToInt(arguments[6], &mip);
		Kore::Graphics4::setTextureAddressing(*unit, Kore::Graphics4::U, convertTextureAddressing(u));
		Kore::Graphics4::setTextureAddressing(*unit, Kore::Graphics4::V, convertTextureAddressing(v));
		Kore::Graphics4::setTextureMinificationFilter(*unit, convertTextureFilter(min));
		Kore::Graphics4::setTextureMagnificationFilter(*unit, convertTextureFilter(max));
		Kore::Graphics4::setTextureMipmapFilter(*unit, convertMipmapFilter(mip));
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_texture_3d_parameters(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::TextureUnit* unit;
		JsGetExternalData(arguments[1], (void**)&unit);
		int u, v, w, min, max, mip;
		JsNumberToInt(arguments[2], &u);
		JsNumberToInt(arguments[3], &v);
		JsNumberToInt(arguments[4], &w);
		JsNumberToInt(arguments[5], &min);
		JsNumberToInt(arguments[6], &max);
		JsNumberToInt(arguments[7], &mip);
		Kore::Graphics4::setTexture3DAddressing(*unit, Kore::Graphics4::U, convertTextureAddressing(u));
		Kore::Graphics4::setTexture3DAddressing(*unit, Kore::Graphics4::V, convertTextureAddressing(v));
		Kore::Graphics4::setTexture3DAddressing(*unit, Kore::Graphics4::W, convertTextureAddressing(w));
		Kore::Graphics4::setTexture3DMinificationFilter(*unit, convertTextureFilter(min));
		Kore::Graphics4::setTexture3DMagnificationFilter(*unit, convertTextureFilter(max));
		Kore::Graphics4::setTexture3DMipmapFilter(*unit, convertMipmapFilter(mip));
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_bool(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::ConstantLocation* location;
		JsGetExternalData(arguments[1], (void**)&location);
		int value;
		JsNumberToInt(arguments[2], &value);
		Kore::Graphics4::setBool(*location, value != 0);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_int(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::ConstantLocation* location;
		JsGetExternalData(arguments[1], (void**)&location);
		int value;
		JsNumberToInt(arguments[2], &value);
		Kore::Graphics4::setInt(*location, value);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_float(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::ConstantLocation* location;
		JsGetExternalData(arguments[1], (void**)&location);
		double value;
		JsNumberToDouble(arguments[2], &value);
		Kore::Graphics4::setFloat(*location, value);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_float2(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::ConstantLocation* location;
		JsGetExternalData(arguments[1], (void**)&location);
		double value1, value2;
		JsNumberToDouble(arguments[2], &value1);
		JsNumberToDouble(arguments[3], &value2);
		Kore::Graphics4::setFloat2(*location, value1, value2);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_float3(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::ConstantLocation* location;
		JsGetExternalData(arguments[1], (void**)&location);
		double value1, value2, value3;
		JsNumberToDouble(arguments[2], &value1);
		JsNumberToDouble(arguments[3], &value2);
		JsNumberToDouble(arguments[4], &value3);
		Kore::Graphics4::setFloat3(*location, value1, value2, value3);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_float4(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::ConstantLocation* location;
		JsGetExternalData(arguments[1], (void**)&location);
		double value1, value2, value3, value4;
		JsNumberToDouble(arguments[2], &value1);
		JsNumberToDouble(arguments[3], &value2);
		JsNumberToDouble(arguments[4], &value3);
		JsNumberToDouble(arguments[5], &value4);
		Kore::Graphics4::setFloat4(*location, value1, value2, value3, value4);
		return JS_INVALID_REFERENCE;
	}

	void krom_set_floats(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::ConstantLocation* location = (Kore::Graphics4::ConstantLocation*)locationfield->Value();

		Local<Float32Array> f32array = Local<Float32Array>::Cast(args[1]);
		ArrayBuffer::Contents content;
		if (f32array->Buffer()->IsExternal()) content = f32array->Buffer()->GetContents();
		else content = f32array->Buffer()->Externalize();
		float* from = (float*)content.Data();

		Kore::Graphics4::setFloats(*location, from, int(content.ByteLength() / 4));
	}

	void krom_set_matrix(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::ConstantLocation* location = (Kore::Graphics4::ConstantLocation*)locationfield->Value();

		Local<Float32Array> f32array = Local<Float32Array>::Cast(args[1]);
		ArrayBuffer::Contents content;
		if (f32array->Buffer()->IsExternal()) content = f32array->Buffer()->GetContents();
		else content = f32array->Buffer()->Externalize();
		float* from = (float*)content.Data();
		Kore::mat4 m;
		m.Set(0, 0, from[0]); m.Set(1, 0, from[1]); m.Set(2, 0, from[2]); m.Set(3, 0, from[3]);
		m.Set(0, 1, from[4]); m.Set(1, 1, from[5]); m.Set(2, 1, from[6]); m.Set(3, 1, from[7]);
		m.Set(0, 2, from[8]); m.Set(1, 2, from[9]); m.Set(2, 2, from[10]); m.Set(3, 2, from[11]);
		m.Set(0, 3, from[12]); m.Set(1, 3, from[13]); m.Set(2, 3, from[14]); m.Set(3, 3, from[15]);

		Kore::Graphics4::setMatrix(*location, m);
	}

	void krom_set_matrix3(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::ConstantLocation* location = (Kore::Graphics4::ConstantLocation*)locationfield->Value();

		Local<Float32Array> f32array = Local<Float32Array>::Cast(args[1]);
		ArrayBuffer::Contents content;
		if (f32array->Buffer()->IsExternal()) content = f32array->Buffer()->GetContents();
		else content = f32array->Buffer()->Externalize();
		float* from = (float*)content.Data();
		Kore::mat3 m;
		m.Set(0, 0, from[0]); m.Set(1, 0, from[1]); m.Set(2, 0, from[2]);
		m.Set(0, 1, from[3]); m.Set(1, 1, from[4]); m.Set(2, 1, from[5]);
		m.Set(0, 2, from[6]); m.Set(1, 2, from[7]); m.Set(2, 2, from[8]);

		Kore::Graphics4::setMatrix(*location, m);
	}

	JsValueRef CALLBACK krom_get_time(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef obj;
		JsDoubleToNumber(Kore::System::time(), &obj);
		return obj;
	}

	JsValueRef CALLBACK krom_window_width(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int windowId;
		JsNumberToInt(arguments[1], &windowId);
		JsValueRef obj;
		JsIntToNumber(Kore::System::windowWidth(windowId), &obj);
		return obj;
	}

	JsValueRef CALLBACK krom_window_height(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int windowId;
		JsNumberToInt(arguments[1], &windowId);
		JsValueRef obj;
		JsIntToNumber(Kore::System::windowHeight(windowId), &obj);
		return obj;
	}

	JsValueRef CALLBACK krom_screen_dpi(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef obj;
		JsIntToNumber(Kore::Display::primary()->pixelsPerInch(), &obj);
		return obj;
	}

	JsValueRef CALLBACK krom_system_id(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef value;
		JsCreateString(Kore::System::systemId(), strlen(Kore::System::systemId()), &value);
		return value;
	}

	JsValueRef CALLBACK krom_request_shutdown(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::System::stop();
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_display_count(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef value;
		JsIntToNumber(Kore::Display::count(), &value);
		return value;
	}

	JsValueRef CALLBACK krom_display_width(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int index;
		JsNumberToInt(arguments[1], &index);
		JsValueRef value;
		JsIntToNumber(Kore::Display::get(index)->width(), &value);
		return value;
	}

	JsValueRef CALLBACK krom_display_height(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int index;
		JsNumberToInt(arguments[1], &index);
		JsValueRef value;
		JsIntToNumber(Kore::Display::get(index)->height(), &value);
		return value;
	}

	JsValueRef CALLBACK krom_display_x(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int index;
		JsNumberToInt(arguments[1], &index);
		JsValueRef value;
		JsIntToNumber(Kore::Display::get(index)->x(), &value);
		return value;
	}

	JsValueRef CALLBACK krom_display_y(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int index;
		JsNumberToInt(arguments[1], &index);
		JsValueRef value;
		JsIntToNumber(Kore::Display::get(index)->y(), &value);
		return value;
	}

	JsValueRef CALLBACK krom_display_is_primary(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int index;
		JsNumberToInt(arguments[1], &index);
		JsValueRef value;
		JsBoolToBoolean(Kore::Display::get(index) == Kore::Display::primary(), &value);
		return value;
	}

	void krom_write_storage(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		String::Utf8Value utf8_name(args[0]);

		Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[1]);
		ArrayBuffer::Contents content;
		if (buffer->IsExternal()) content = buffer->GetContents();
		else content = buffer->Externalize();

		Kore::FileWriter writer;
		if (!writer.open(*utf8_name)) return;
		writer.write(content.Data(), (int)content.ByteLength());
	}

	void krom_read_storage(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		String::Utf8Value utf8_name(args[0]);

		Kore::FileReader reader;
		if (!reader.open(*utf8_name, Kore::FileReader::Save)) return;

		Local<ArrayBuffer> buffer = ArrayBuffer::New(isolate, reader.size());
		ArrayBuffer::Contents contents = buffer->Externalize();
		unsigned char* from = (unsigned char*)reader.readAll();
		unsigned char* to = (unsigned char*)contents.Data();
		for (int i = 0; i < reader.size(); ++i) {
			to[i] = from[i];
		}
		reader.close();
		args.GetReturnValue().Set(buffer);
	}

	JsValueRef CALLBACK krom_create_render_target(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int value1, value2, value3, value4, value5;
		JsNumberToInt(arguments[1], &value1);
		JsNumberToInt(arguments[2], &value2);
		JsNumberToInt(arguments[3], &value3);
		JsNumberToInt(arguments[4], &value4);
		JsNumberToInt(arguments[5], &value5);
		Kore::Graphics4::RenderTarget* renderTarget = new Kore::Graphics4::RenderTarget(value1, value2, value3, false, (Kore::Graphics4::RenderTargetFormat)value4, value5);

		JsValueRef value;
		JsCreateExternalObject(renderTarget, nullptr, &value);

		JsValueRef width, height;
		JsIntToNumber(renderTarget->width, &width);
		JsIntToNumber(renderTarget->height, &height);

		JsSetProperty(value, getId("width"), width, false);
		JsSetProperty(value, getId("height"), height, false);

		return value;
	}

	JsValueRef CALLBACK krom_create_render_target_cube_map(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int value1, value2, value3, value4;
		JsNumberToInt(arguments[1], &value1);
		JsNumberToInt(arguments[2], &value2);
		JsNumberToInt(arguments[3], &value3);
		JsNumberToInt(arguments[4], &value4);
		Kore::Graphics4::RenderTarget* renderTarget = new Kore::Graphics4::RenderTarget(value1, value2, false, (Kore::Graphics4::RenderTargetFormat)value3, value4);

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
		Kore::Graphics4::Texture* texture = new Kore::Graphics4::Texture(value1, value2, (Kore::Graphics4::Image::Format)value3, false);

		JsValueRef value;
		JsCreateExternalObject(texture, nullptr, &value);

		JsValueRef width, height, realWidth, realHeight;
		JsIntToNumber(texture->width, &width);
		JsIntToNumber(texture->height, &height);
		JsIntToNumber(texture->texWidth, &realWidth);
		JsIntToNumber(texture->texHeight, &realHeight);

		JsSetProperty(value, getId("width"), width, false);
		JsSetProperty(value, getId("height"), height, false);
		JsSetProperty(value, getId("realWidth"), realWidth, false);
		JsSetProperty(value, getId("realHeight"), realHeight, false);
		
		return value;
	}

	void krom_create_texture_3d(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Kore::Graphics4::Texture* texture = new Kore::Graphics4::Texture(args[0]->ToInt32()->Value(), args[1]->ToInt32()->Value(), args[2]->ToInt32()->Value(), (Kore::Graphics4::Image::Format)args[3]->ToInt32()->Value(), false);

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, texture));
		obj->Set(String::NewFromUtf8(isolate, "width"), Int32::New(isolate, texture->width));
		obj->Set(String::NewFromUtf8(isolate, "height"), Int32::New(isolate, texture->height));
		obj->Set(String::NewFromUtf8(isolate, "depth"), Int32::New(isolate, texture->depth));
		obj->Set(String::NewFromUtf8(isolate, "realWidth"), Int32::New(isolate, texture->texWidth));
		obj->Set(String::NewFromUtf8(isolate, "realHeight"), Int32::New(isolate, texture->texHeight));
		args.GetReturnValue().Set(obj);
	}

	void krom_create_texture_from_bytes(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());

		Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
		ArrayBuffer::Contents content;
		if (buffer->IsExternal()) content = buffer->GetContents();
		else content = buffer->Externalize();
		Kore::Graphics4::Texture* texture = new Kore::Graphics4::Texture(content.Data(), args[1]->ToInt32()->Value(), args[2]->ToInt32()->Value(), (Kore::Graphics4::Image::Format)args[3]->ToInt32()->Value(), args[4]->ToBoolean()->Value());

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, texture));
		obj->Set(String::NewFromUtf8(isolate, "width"), Int32::New(isolate, texture->width));
		obj->Set(String::NewFromUtf8(isolate, "height"), Int32::New(isolate, texture->height));
		obj->Set(String::NewFromUtf8(isolate, "realWidth"), Int32::New(isolate, texture->texWidth));
		obj->Set(String::NewFromUtf8(isolate, "realHeight"), Int32::New(isolate, texture->texHeight));
		args.GetReturnValue().Set(obj);
	}

	void krom_create_texture_from_bytes_3d(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());

		Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
		ArrayBuffer::Contents content;
		if (buffer->IsExternal()) content = buffer->GetContents();
		else content = buffer->Externalize();
		Kore::Graphics4::Texture* texture = new Kore::Graphics4::Texture(content.Data(), args[1]->ToInt32()->Value(), args[2]->ToInt32()->Value(), args[3]->ToInt32()->Value(), (Kore::Graphics4::Image::Format)args[4]->ToInt32()->Value(), args[5]->ToBoolean()->Value());

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, texture));
		obj->Set(String::NewFromUtf8(isolate, "width"), Int32::New(isolate, texture->width));
		obj->Set(String::NewFromUtf8(isolate, "height"), Int32::New(isolate, texture->height));
		obj->Set(String::NewFromUtf8(isolate, "depth"), Int32::New(isolate, texture->depth));
		obj->Set(String::NewFromUtf8(isolate, "realWidth"), Int32::New(isolate, texture->texWidth));
		obj->Set(String::NewFromUtf8(isolate, "realHeight"), Int32::New(isolate, texture->texHeight));
		args.GetReturnValue().Set(obj);
	}

	void krom_get_render_target_pixels(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());

		Local<External> field = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::RenderTarget* rt = (Kore::Graphics4::RenderTarget*)field->Value();

		Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[1]);
		ArrayBuffer::Contents content;
		if (buffer->IsExternal()) content = buffer->GetContents();
		else content = buffer->Externalize();

		Kore::u8* b = (Kore::u8*)content.Data();
		rt->getPixels(b);
	}

	int formatByteSize(Kore::Graphics4::Image::Format format) {
		switch (format) {
		case Kore::Graphics4::Image::RGBA128:
			return 16;
		case Kore::Graphics4::Image::RGBA64:
			return 8;
		case Kore::Graphics4::Image::RGB24:
			return 4;
		case Kore::Graphics4::Image::A32:
			return 4;
		case Kore::Graphics4::Image::A16:
			return 2;
		case Kore::Graphics4::Image::Grey8:
			return 1;
		case Kore::Graphics4::Image::BGRA32:
		case Kore::Graphics4::Image::RGBA32:
		default:
			return 4;
		}
	}

	JsValueRef CALLBACK krom_lock_texture(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::Texture* texture;
		JsGetExternalData(arguments[1], (void**)&texture);
		Kore::u8* tex = texture->lock();

		int byteLength = formatByteSize(texture->format) * texture->width * texture->height * texture->depth;
		JsValueRef value;
		JsCreateExternalArrayBuffer(tex, byteLength, nullptr, nullptr, &value);
		return value;
	}

	JsValueRef CALLBACK krom_unlock_texture(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::Texture* texture;
		JsGetExternalData(arguments[1], (void**)&texture);
		texture->unlock();
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_clear_texture(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::Texture* texture;
		JsGetExternalData(arguments[1], (void**)&texture);
		int x, y, z, width, height, depth, color;
		JsNumberToInt(arguments[2], &x);
		JsNumberToInt(arguments[3], &y);
		JsNumberToInt(arguments[4], &z);
		JsNumberToInt(arguments[5], &width);
		JsNumberToInt(arguments[6], &height);
		JsNumberToInt(arguments[7], &depth);
		JsNumberToInt(arguments[8], &color);
		texture->clear(x, y, z, width, height, depth, color);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_generate_texture_mipmaps(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::Texture* texture;
		JsGetExternalData(arguments[1], (void**)&texture);
		int levels;
		JsNumberToInt(arguments[2], &levels);
		texture->generateMipmaps(levels);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_generate_render_target_mipmaps(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::RenderTarget* rt;
		JsGetExternalData(arguments[1], (void**)&rt);
		int levels;
		JsNumberToInt(arguments[2], &levels);
		rt->generateMipmaps(levels);
		return JS_INVALID_REFERENCE;
	}

	void krom_set_mipmaps(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> field = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::Texture* texture = (Kore::Graphics4::Texture*)field->Value();

		Local<Object> jsarray = args[1]->ToObject();
		int32_t length = jsarray->Get(String::NewFromUtf8(isolate, "length"))->ToInt32()->Value();
		for (int32_t i = 0; i < length; ++i) {
			Local<Object> mipmapobj = jsarray->Get(i)->ToObject()->Get(String::NewFromUtf8(isolate, "texture_"))->ToObject();
			Local<External> mipmapfield = Local<External>::Cast(mipmapobj->GetInternalField(0));
			Kore::Graphics4::Texture* mipmap = (Kore::Graphics4::Texture*)mipmapfield->Value();
			texture->setMipmap(mipmap, i + 1);
		}
	}

	void krom_set_depth_stencil_from(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> targetfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::RenderTarget* renderTarget = (Kore::Graphics4::RenderTarget*)targetfield->Value();
		Local<External> sourcefield = Local<External>::Cast(args[1]->ToObject()->GetInternalField(0));
		Kore::Graphics4::RenderTarget* sourceTarget = (Kore::Graphics4::RenderTarget*)sourcefield->Value();
		renderTarget->setDepthStencilFrom(sourceTarget);
	}

	JsValueRef CALLBACK krom_viewport(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int x, y, w, h;
		JsNumberToInt(arguments[1], &x);
		JsNumberToInt(arguments[2], &y);
		JsNumberToInt(arguments[3], &w);
		JsNumberToInt(arguments[4], &h);

		Kore::Graphics4::viewport(x, y, w, h);

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_scissor(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int x, y, w, h;
		JsNumberToInt(arguments[1], &x);
		JsNumberToInt(arguments[2], &y);
		JsNumberToInt(arguments[3], &w);
		JsNumberToInt(arguments[4], &h);

		Kore::Graphics4::scissor(x, y, w, h);

		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_disable_scissor(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::Graphics4::disableScissor();
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_render_targets_inverted_y(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef value;
		JsBoolToBoolean(Kore::Graphics4::renderTargetsInvertedY(), &value);
		return value;
	}

	void krom_begin(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		if (args[0]->IsNull() || args[0]->IsUndefined()) {
			Kore::Graphics4::restoreRenderTarget();
		}
		else {
			Local<Object> obj = args[0]->ToObject()->Get(String::NewFromUtf8(isolate, "renderTarget_"))->ToObject();
			Local<External> rtfield = Local<External>::Cast(obj->GetInternalField(0));
			Kore::Graphics4::RenderTarget* renderTarget = (Kore::Graphics4::RenderTarget*)rtfield->Value();

			if (args[1]->IsNull() || args[1]->IsUndefined()) {
				Kore::Graphics4::setRenderTarget(renderTarget);
			}
			else {
				Kore::Graphics4::RenderTarget* renderTargets[8] = { renderTarget, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
				Local<Object> jsarray = args[1]->ToObject();
				int32_t length = jsarray->Get(String::NewFromUtf8(isolate, "length"))->ToInt32()->Value();
				if (length > 7) length = 7;
				for (int32_t i = 0; i < length; ++i) {
					Local<Object> artobj = jsarray->Get(i)->ToObject()->Get(String::NewFromUtf8(isolate, "renderTarget_"))->ToObject();
					Local<External> artfield = Local<External>::Cast(artobj->GetInternalField(0));
					Kore::Graphics4::RenderTarget* art = (Kore::Graphics4::RenderTarget*)artfield->Value();
					renderTargets[i + 1] = art;
				}
				Kore::Graphics4::setRenderTargets(renderTargets, length + 1);
			}
		}
	}

	void krom_begin_face(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<Object> obj = args[0]->ToObject()->Get(String::NewFromUtf8(isolate, "renderTarget_"))->ToObject();
		Local<External> rtfield = Local<External>::Cast(obj->GetInternalField(0));
		Kore::Graphics4::RenderTarget* renderTarget = (Kore::Graphics4::RenderTarget*)rtfield->Value();
		int face = args[1]->ToInt32()->Int32Value();
		Kore::Graphics4::setRenderTargetFace(renderTarget, face);
	}

	JsValueRef CALLBACK krom_end(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		return JS_INVALID_REFERENCE;
	}

	void krom_file_save_bytes(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		String::Utf8Value utf8_path(args[0]);

		Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[1]);
		ArrayBuffer::Contents content;
		if (buffer->IsExternal()) content = buffer->GetContents();
		else content = buffer->Externalize();

		FILE* file = fopen(*utf8_path, "wb");
		if (file == nullptr) return;
		fwrite(content.Data(), 1, (int)content.ByteLength(), file);
		fclose(file);
	}

	JsValueRef CALLBACK krom_sys_command(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		char command[256];
		size_t length;
		JsCopyString(arguments[1], command, 255, &length);
		command[length] = 0;
		int result = system(command);
		JsValueRef value;
		JsIntToNumber(result, &value);
		return value;
	}

	JsValueRef CALLBACK krom_save_path(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		JsValueRef value;
		JsCreateString(Kore::System::savePath(), strlen(Kore::System::savePath()), &value);
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

	JsValueRef CALLBACK krom_set_bool_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::ComputeConstantLocation* location;
		JsGetExternalData(arguments[0], (void**)&location);
		int value;
		JsNumberToInt(arguments[2], &value);
		Kore::Compute::setBool(*location, value != 0);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_int_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::ComputeConstantLocation* location;
		JsGetExternalData(arguments[0], (void**)&location);
		int value;
		JsNumberToInt(arguments[2], &value);
		Kore::Compute::setInt(*location, value);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_float_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::ComputeConstantLocation* location;
		JsGetExternalData(arguments[0], (void**)&location);
		double value;
		JsNumberToDouble(arguments[2], &value);
		Kore::Compute::setFloat(*location, value);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_float2_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::ComputeConstantLocation* location;
		JsGetExternalData(arguments[0], (void**)&location);
		double value1, value2;
		JsNumberToDouble(arguments[2], &value1);
		JsNumberToDouble(arguments[3], &value2);
		Kore::Compute::setFloat2(*location, value1, value2);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_float3_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::ComputeConstantLocation* location;
		JsGetExternalData(arguments[0], (void**)&location);
		double value1, value2, value3;
		JsNumberToDouble(arguments[2], &value1);
		JsNumberToDouble(arguments[3], &value2);
		JsNumberToDouble(arguments[4], &value3);
		Kore::Compute::setFloat3(*location, value1, value2, value3);
		return JS_INVALID_REFERENCE;
	}

	JsValueRef CALLBACK krom_set_float4_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::ComputeConstantLocation* location;
		JsGetExternalData(arguments[0], (void**)&location);
		double value1, value2, value3, value4;
		JsNumberToDouble(arguments[2], &value1);
		JsNumberToDouble(arguments[3], &value2);
		JsNumberToDouble(arguments[4], &value3);
		JsNumberToDouble(arguments[5], &value4);
		Kore::Compute::setFloat4(*location, value1, value2, value3, value4);
		return JS_INVALID_REFERENCE;
	}

	void krom_set_floats_compute(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::ComputeConstantLocation* location = (Kore::ComputeConstantLocation*)locationfield->Value();

		Local<Float32Array> f32array = Local<Float32Array>::Cast(args[1]);
		ArrayBuffer::Contents content;
		if (f32array->Buffer()->IsExternal()) content = f32array->Buffer()->GetContents();
		else content = f32array->Buffer()->Externalize();
		float* from = (float*)content.Data();

		Kore::Compute::setFloats(*location, from, int(content.ByteLength() / 4));
	}

	void krom_set_matrix_compute(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::ComputeConstantLocation* location = (Kore::ComputeConstantLocation*)locationfield->Value();

		Local<Float32Array> f32array = Local<Float32Array>::Cast(args[1]);
		ArrayBuffer::Contents content;
		if (f32array->Buffer()->IsExternal()) content = f32array->Buffer()->GetContents();
		else content = f32array->Buffer()->Externalize();
		float* from = (float*)content.Data();
		Kore::mat4 m;
		m.Set(0, 0, from[0]); m.Set(1, 0, from[1]); m.Set(2, 0, from[2]); m.Set(3, 0, from[3]);
		m.Set(0, 1, from[4]); m.Set(1, 1, from[5]); m.Set(2, 1, from[6]); m.Set(3, 1, from[7]);
		m.Set(0, 2, from[8]); m.Set(1, 2, from[9]); m.Set(2, 2, from[10]); m.Set(3, 2, from[11]);
		m.Set(0, 3, from[12]); m.Set(1, 3, from[13]); m.Set(2, 3, from[14]); m.Set(3, 3, from[15]);

		Kore::Compute::setMatrix(*location, m);
	}

	void krom_set_matrix3_compute(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::ComputeConstantLocation* location = (Kore::ComputeConstantLocation*)locationfield->Value();

		Local<Float32Array> f32array = Local<Float32Array>::Cast(args[1]);
		ArrayBuffer::Contents content;
		if (f32array->Buffer()->IsExternal()) content = f32array->Buffer()->GetContents();
		else content = f32array->Buffer()->Externalize();
		float* from = (float*)content.Data();
		Kore::mat3 m;
		m.Set(0, 0, from[0]); m.Set(1, 0, from[1]); m.Set(2, 0, from[2]);
		m.Set(0, 1, from[3]); m.Set(1, 1, from[4]); m.Set(2, 1, from[5]);
		m.Set(0, 2, from[6]); m.Set(1, 2, from[7]); m.Set(2, 2, from[8]);

		Kore::Compute::setMatrix(*location, m);
	}

	void krom_set_texture_compute(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> unitfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::ComputeTextureUnit* unit = (Kore::ComputeTextureUnit*)unitfield->Value();
		if (args[1]->IsNull() || args[1]->IsUndefined()) return;
		Local<Object> image = args[1]->ToObject();
		Local<Value> tex = image->Get(String::NewFromUtf8(isolate, "texture_"));
		int access = args[2]->ToInt32()->Int32Value();
		if (tex->IsObject()) {
			Local<External> texfield = Local<External>::Cast(tex->ToObject()->GetInternalField(0));
			Kore::Graphics4::Texture* texture = (Kore::Graphics4::Texture*)texfield->Value();
			Kore::Compute::setTexture(*unit, texture, (Kore::Compute::Access)access);
		}
		else {
			Local<Value> rt = image->Get(String::NewFromUtf8(isolate, "renderTarget_"));
			Local<External> rtfield = Local<External>::Cast(rt->ToObject()->GetInternalField(0));
			Kore::Graphics4::RenderTarget* renderTarget = (Kore::Graphics4::RenderTarget*)rtfield->Value();
			Kore::Compute::setTexture(*unit, renderTarget, (Kore::Compute::Access)access);
		}
	}

	void krom_set_sampled_texture_compute(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> unitfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::ComputeTextureUnit* unit = (Kore::ComputeTextureUnit*)unitfield->Value();
		if (args[1]->IsNull() || args[1]->IsUndefined()) return;
		Local<Object> image = args[1]->ToObject();
		Local<Value> tex = image->Get(String::NewFromUtf8(isolate, "texture_"));
		if (tex->IsObject()) {
			Local<External> texfield = Local<External>::Cast(tex->ToObject()->GetInternalField(0));
			Kore::Graphics4::Texture* texture = (Kore::Graphics4::Texture*)texfield->Value();
			Kore::Compute::setSampledTexture(*unit, texture);
		}
		else {
			Local<Value> rt = image->Get(String::NewFromUtf8(isolate, "renderTarget_"));
			Local<External> rtfield = Local<External>::Cast(rt->ToObject()->GetInternalField(0));
			Kore::Graphics4::RenderTarget* renderTarget = (Kore::Graphics4::RenderTarget*)rtfield->Value();
			Kore::Compute::setSampledTexture(*unit, renderTarget);
		}
	}

	void krom_set_sampled_depth_texture_compute(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> unitfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::ComputeTextureUnit* unit = (Kore::ComputeTextureUnit*)unitfield->Value();

		if (args[1]->IsNull() || args[1]->IsUndefined()) return;

		Local<Object> image = args[1]->ToObject();
		Local<Value> rt = image->Get(String::NewFromUtf8(isolate, "renderTarget_"));
		if (rt->IsObject()) {
			Local<External> rtfield = Local<External>::Cast(rt->ToObject()->GetInternalField(0));
			Kore::Graphics4::RenderTarget* renderTarget = (Kore::Graphics4::RenderTarget*)rtfield->Value();
			Kore::Compute::setSampledDepthTexture(*unit, renderTarget);
		}
	}

	void krom_set_texture_parameters_compute(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> unitfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::ComputeTextureUnit* unit = (Kore::ComputeTextureUnit*)unitfield->Value();
		Kore::Compute::setTextureAddressing(*unit, Kore::Graphics4::U, convertTextureAddressing(args[1]->ToInt32()->Int32Value()));
		Kore::Compute::setTextureAddressing(*unit, Kore::Graphics4::V, convertTextureAddressing(args[2]->ToInt32()->Int32Value()));
		Kore::Compute::setTextureMinificationFilter(*unit, convertTextureFilter(args[3]->ToInt32()->Int32Value()));
		Kore::Compute::setTextureMagnificationFilter(*unit, convertTextureFilter(args[4]->ToInt32()->Int32Value()));
		Kore::Compute::setTextureMipmapFilter(*unit, convertMipmapFilter(args[5]->ToInt32()->Int32Value()));
	}

	void krom_set_texture_3d_parameters_compute(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> unitfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::ComputeTextureUnit* unit = (Kore::ComputeTextureUnit*)unitfield->Value();
		Kore::Compute::setTexture3DAddressing(*unit, Kore::Graphics4::U, convertTextureAddressing(args[1]->ToInt32()->Int32Value()));
		Kore::Compute::setTexture3DAddressing(*unit, Kore::Graphics4::V, convertTextureAddressing(args[2]->ToInt32()->Int32Value()));
		Kore::Compute::setTexture3DAddressing(*unit, Kore::Graphics4::W, convertTextureAddressing(args[3]->ToInt32()->Int32Value()));
		Kore::Compute::setTexture3DMinificationFilter(*unit, convertTextureFilter(args[4]->ToInt32()->Int32Value()));
		Kore::Compute::setTexture3DMagnificationFilter(*unit, convertTextureFilter(args[5]->ToInt32()->Int32Value()));
		Kore::Compute::setTexture3DMipmapFilter(*unit, convertMipmapFilter(args[6]->ToInt32()->Int32Value()));
	}

	JsValueRef CALLBACK krom_set_shader_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::ComputeShader* shader;
		JsGetExternalData(arguments[1], (void**)&shader);
		Kore::Compute::setShader(shader);
		return JS_INVALID_REFERENCE;
	}

	void krom_create_shader_compute(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
		ArrayBuffer::Contents content;
		if (buffer->IsExternal()) content = buffer->GetContents();
		else content = buffer->Externalize();
		Kore::ComputeShader* shader = new Kore::ComputeShader(content.Data(), (int)content.ByteLength());

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, shader));
		args.GetReturnValue().Set(obj);
	}

	JsValueRef CALLBACK krom_delete_shader_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		Kore::ComputeShader* shader;
		JsGetExternalData(arguments[1], (void**)&shader);
		delete shader;
		return JS_INVALID_REFERENCE;
	}

	void krom_get_constant_location_compute(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> shaderfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::ComputeShader* shader = (Kore::ComputeShader*)shaderfield->Value();

		String::Utf8Value utf8_value(args[1]);
		Kore::ComputeConstantLocation location = shader->getConstantLocation(*utf8_value);

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, new Kore::ComputeConstantLocation(location)));
		args.GetReturnValue().Set(obj);
	}

	void krom_get_texture_unit_compute(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> shaderfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::ComputeShader* shader = (Kore::ComputeShader*)shaderfield->Value();

		String::Utf8Value utf8_value(args[1]);
		Kore::ComputeTextureUnit unit = shader->getTextureUnit(*utf8_value);

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, new Kore::ComputeTextureUnit(unit)));
		args.GetReturnValue().Set(obj);
	}

	JsValueRef CALLBACK krom_compute(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
		int x, y, z;
		JsNumberToInt(arguments[1], &x);
		JsNumberToInt(arguments[2], &y);
		JsNumberToInt(arguments[3], &z);
		Kore::Compute::compute(x, y, z);
		return JS_INVALID_REFERENCE;
	}

#define addFunction(name, funcName) JsPropertyIdRef name##Id;\
	JsValueRef name##Func;\
	JsCreateFunction(funcName, nullptr, &name##Func);\
	JsCreatePropertyId(#name, strlen(#name), &name##Id);\
	JsSetProperty(krom, name##Id, name##Func, false)

	void startV8(const char* bindir) {
		JsValueRef krom;
		JsCreateObject(&krom);

		addFunction(init, krom_init);
		addFunction(log, LogCallback);
		addFunction(clear, graphics_clear);
		addFunction(setCallback, krom_set_callback);
		addFunction(setDropFilesCallback, krom_set_drop_files_callback);
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
		addFunction(audioThread, audio_thread);
		addFunction(writeAudioBuffer, write_audio_buffer);
		addFunction(loadBlob, krom_load_blob);
		addFunction(getConstantLocation, krom_get_constant_location);
		addFunction(getTextureUnit, krom_get_texture_unit);
		addFunction(setTexture, krom_set_texture);
		addFunction(setTextureDepth, krom_set_texture_depth);
		addFunction(setImageTexture, krom_set_image_texture);
		addFunction(setTextureParameters, krom_set_texture_parameters);
		addFunction(setTexture3DParameters, krom_set_texture_3d_parameters);
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
		addFunction(setSampledTextureCompute, krom_set_sampled_texture_compute);
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

	bool startKrom(char* scriptfile) {
		v8::Locker locker{isolate};

		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		Local<Context> context = Local<Context>::New(isolate, globalContext);
		Context::Scope context_scope(context);

		Local<String> source = String::NewFromUtf8(isolate, scriptfile, NewStringType::kNormal).ToLocalChecked();
		Local<String> filename = String::NewFromUtf8(isolate, "krom.js", NewStringType::kNormal).ToLocalChecked();

		TryCatch try_catch(isolate);

		Local<Script> compiled_script = Script::Compile(source, filename);

		Local<Value> result;
		if (!compiled_script->Run(context).ToLocal(&result)) {
			v8::String::Utf8Value stack_trace(try_catch.StackTrace());
			sendLogMessage("Trace: %s", *stack_trace);
			return false;
		}

		return true;
	}

	bool codechanged = false;

	void parseCode();

	void runV8() {
		if (messageLoopPaused) return;

		if (codechanged) {
			parseCode();
			codechanged = false;
		}

		v8::Locker locker{isolate};

		Isolate::Scope isolate_scope(isolate);
		v8::MicrotasksScope microtasks_scope(isolate, v8::MicrotasksScope::kRunMicrotasks);
		HandleScope handle_scope(isolate);
		Local<Context> context = Local<Context>::New(isolate, globalContext);
		Context::Scope context_scope(context);

		TryCatch try_catch(isolate);
		Local<v8::Function> func = Local<v8::Function>::New(isolate, updateFunction);
		Local<Value> result;

		//**if (debugMode) v8inspector->willExecuteScript(context, func->ScriptId());
		if (!func->Call(context, context->Global(), 0, NULL).ToLocal(&result)) {
			v8::String::Utf8Value stack_trace(try_catch.StackTrace());
			sendLogMessage("Trace: %s", *stack_trace);
		}
		//**if (debugMode) v8inspector->didExecuteScript(context);
	}

	void endV8() {
		
	}

	void initAudioBuffer() {
		for (int i = 0; i < Kore::Audio2::buffer.dataSize; i++) {
			*(float*)&Kore::Audio2::buffer.data[i] = 0;
		}
	}

	void updateAudio(int samples) {
		/**v8::Locker locker{isolate};

		Isolate::Scope isolate_scope(isolate);
		v8::MicrotasksScope microtasks_scope(isolate, v8::MicrotasksScope::kRunMicrotasks);
		HandleScope handle_scope(isolate);
		Local<Context> context = Local<Context>::New(isolate, globalContext);
		Context::Scope context_scope(context);

		TryCatch try_catch(isolate);
		Local<v8::Function> func = Local<v8::Function>::New(isolate, audioFunction);
		Local<Value> result;
		const int argc = 1;
		Local<Value> argv[argc] = {Int32::New(isolate, samples)};
		if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
			v8::String::Utf8Value stack_trace(try_catch.StackTrace());
			sendLogMessage("Trace: %s", *stack_trace);
		}*/
	}

	void mix(int samples) {
		//mutex.Lock();
		updateAudio(samples);
		//mutex.Unlock();
	}

	void update() {
		if (!nosound) Kore::Audio2::update();
		Kore::Graphics4::begin();

		//mutex.Lock();
		runV8();
		//mutex.Unlock();

		if (debugMode) {
			do {
				tickDebugger();
			} while (messageLoopPaused);
		}
		Kore::Graphics4::end();
		Kore::Graphics4::swapBuffers();
	}

	void dropFiles(wchar_t* filePath) {
		/**v8::Locker locker{isolate};

		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, globalContext);
		Context::Scope context_scope(context);

		TryCatch try_catch(isolate);
		v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, dropFilesFunction);
		Local<Value> result;
		const int argc = 1;
		Local<Value> argv[argc];
		if (sizeof(wchar_t) == 2) {
			argv[0] = {String::NewFromTwoByte(isolate, (const uint16_t*)filePath)};
		}
		else {
			size_t len = wcslen(filePath);
			uint16_t* str = new uint16_t[len + 1];
			for (int i = 0; i < len; i++) str[i] = filePath[i];
			str[len] = 0;
			argv[0] = {String::NewFromTwoByte(isolate, str)};
			delete str;
		}
		if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
			v8::String::Utf8Value stack_trace(try_catch.StackTrace());
			sendLogMessage("Trace: %s", *stack_trace);
		}*/
	}

	void keyDown(Kore::KeyCode code) {
		/**v8::Locker locker{isolate};

		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, globalContext);
		Context::Scope context_scope(context);

		TryCatch try_catch(isolate);
		v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, keyboardDownFunction);
		Local<Value> result;
		const int argc = 1;
		Local<Value> argv[argc] = {Int32::New(isolate, (int)code)};
		if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
			v8::String::Utf8Value stack_trace(try_catch.StackTrace());
			sendLogMessage("Trace: %s", *stack_trace);
		}*/
	}

	void keyUp(Kore::KeyCode code) {
		/**v8::Locker locker{isolate};

		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, globalContext);
		Context::Scope context_scope(context);

		TryCatch try_catch(isolate);
		v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, keyboardUpFunction);
		Local<Value> result;
		const int argc = 1;
		Local<Value> argv[argc] = {Int32::New(isolate, (int)code)};
		if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
			v8::String::Utf8Value stack_trace(try_catch.StackTrace());
			sendLogMessage("Trace: %s", *stack_trace);
		}*/
	}

    void keyPress(wchar_t character) {
       /** v8::Locker locker{isolate};

        Isolate::Scope isolate_scope(isolate);
        HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, globalContext);
        Context::Scope context_scope(context);

        TryCatch try_catch(isolate);
        v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, keyboardPressFunction);
        Local<Value> result;
        const int argc = 1;
        Local<Value> argv[argc] = {Int32::New(isolate, (int)character)};
        if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
            v8::String::Utf8Value stack_trace(try_catch.StackTrace());
            sendLogMessage("Trace: %s", *stack_trace);
        }*/
    }

	void mouseMove(int window, int x, int y, int mx, int my) {
		/**v8::Locker locker{isolate};

		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, globalContext);
		Context::Scope context_scope(context);

		TryCatch try_catch(isolate);
		v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, mouseMoveFunction);
		Local<Value> result;
		const int argc = 4;
		Local<Value> argv[argc] = {Int32::New(isolate, x), Int32::New(isolate, y), Int32::New(isolate, mx), Int32::New(isolate, my)};
		if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
			v8::String::Utf8Value stack_trace(try_catch.StackTrace());
			sendLogMessage("Trace: %s", *stack_trace);
		}*/
	}

	void mouseDown(int window, int button, int x, int y) {
		/**v8::Locker locker{isolate};

		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, globalContext);
		Context::Scope context_scope(context);

		TryCatch try_catch(isolate);
		v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, mouseDownFunction);
		Local<Value> result;
		const int argc = 3;
		Local<Value> argv[argc] = {Int32::New(isolate, button), Int32::New(isolate, x), Int32::New(isolate, y)};
		if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
			v8::String::Utf8Value stack_trace(try_catch.StackTrace());
			sendLogMessage("Trace: %s", *stack_trace);
		}*/
	}

	void mouseUp(int window, int button, int x, int y) {
		/**v8::Locker locker{isolate};

		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, globalContext);
		Context::Scope context_scope(context);

		TryCatch try_catch(isolate);
		v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, mouseUpFunction);
		Local<Value> result;
		const int argc = 3;
		Local<Value> argv[argc] = {Int32::New(isolate, button), Int32::New(isolate, x), Int32::New(isolate, y)};
		if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
			v8::String::Utf8Value stack_trace(try_catch.StackTrace());
			sendLogMessage("Trace: %s", *stack_trace);
		}*/
	}

	void mouseWheel(int window, int delta) {
		/**v8::Locker locker{isolate};

		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, globalContext);
		Context::Scope context_scope(context);

		TryCatch try_catch(isolate);
		v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, mouseWheelFunction);
		Local<Value> result;
		const int argc = 1;
		Local<Value> argv[argc] = {Int32::New(isolate, delta)};
		if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
			v8::String::Utf8Value stack_trace(try_catch.StackTrace());
			sendLogMessage("Trace: %s", *stack_trace);
		}*/
	}

	void penDown(int window, int x, int y, float pressure) {
		/**v8::Locker locker{isolate};

		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, globalContext);
		Context::Scope context_scope(context);

		TryCatch try_catch(isolate);
		v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, penDownFunction);
		Local<Value> result;
		const int argc = 3;
		Local<Value> argv[argc] = {Int32::New(isolate, x), Int32::New(isolate, y), Number::New(isolate, pressure)};
		if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
			v8::String::Utf8Value stack_trace(try_catch.StackTrace());
			sendLogMessage("Trace: %s", *stack_trace);
		}*/
	}

	void penUp(int window, int x, int y, float pressure) {
		/**v8::Locker locker{isolate};

		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, globalContext);
		Context::Scope context_scope(context);

		TryCatch try_catch(isolate);
		v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, penUpFunction);
		Local<Value> result;
		const int argc = 3;
		Local<Value> argv[argc] = {Int32::New(isolate, x), Int32::New(isolate, y), Number::New(isolate, pressure)};
		if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
			v8::String::Utf8Value stack_trace(try_catch.StackTrace());
			sendLogMessage("Trace: %s", *stack_trace);
		}*/
	}

	void penMove(int window, int x, int y, float pressure) {
		/**v8::Locker locker{isolate};

		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, globalContext);
		Context::Scope context_scope(context);

		TryCatch try_catch(isolate);
		v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, penMoveFunction);
		Local<Value> result;
		const int argc = 3;
		Local<Value> argv[argc] = {Int32::New(isolate, x), Int32::New(isolate, y), Number::New(isolate, pressure)};
		if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
			v8::String::Utf8Value stack_trace(try_catch.StackTrace());
			sendLogMessage("Trace: %s", *stack_trace);
		}*/
	}

	void gamepadAxis(int gamepad, int axis, float value) {
		/**v8::Locker locker{isolate};

		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, globalContext);
		Context::Scope context_scope(context);

		TryCatch try_catch(isolate);
		v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, gamepadAxisFunction);
		Local<Value> result;
		const int argc = 3;
		Local<Value> argv[argc] = {Int32::New(isolate, gamepad), Int32::New(isolate, axis), Number::New(isolate, value)};
		if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
			v8::String::Utf8Value stack_trace(try_catch.StackTrace());
			sendLogMessage("Trace: %s", *stack_trace);
		}*/
	}

	void gamepadButton(int gamepad, int button, float value) {
		/**v8::Locker locker{isolate};

		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, globalContext);
		Context::Scope context_scope(context);

		TryCatch try_catch(isolate);
		v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, gamepadButtonFunction);
		Local<Value> result;
		const int argc = 3;
		Local<Value> argv[argc] = {Int32::New(isolate, gamepad), Int32::New(isolate, button), Number::New(isolate, value)};
		if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
			v8::String::Utf8Value stack_trace(try_catch.StackTrace());
			sendLogMessage("Trace: %s", *stack_trace);
		}*/
	}

	void gamepad1Axis(int axis, float value) {
		gamepadAxis(0, axis, value);
	}

	void gamepad1Button(int button, float value) {
		gamepadButton(0, button, value);
	}

	void gamepad2Axis(int axis, float value) {
		gamepadAxis(1, axis, value);
	}

	void gamepad2Button(int button, float value) {
		gamepadButton(1, button, value);
	}

	void gamepad3Axis(int axis, float value) {
		gamepadAxis(2, axis, value);
	}

	void gamepad3Button(int button, float value) {
		gamepadButton(2, button, value);
	}

	void gamepad4Axis(int axis, float value) {
		gamepadAxis(3, axis, value);
	}

	void gamepad4Button(int button, float value) {
		gamepadButton(3, button, value);
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

	std::string replaceAll(std::string str, const std::string& from, const std::string& to) {
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
		std::map<std::string, Func*> methods;
		std::map<std::string, Func*> functions;
	};

	std::map<std::string, Klass*> classes;

	enum ParseMode {
		ParseRegular,
		ParseMethods,
		ParseMethod,
		ParseFunction
	};

	void parseCode() {
		/**int types = 0;
		ParseMode mode = ParseRegular;
		Klass* currentClass = nullptr;
		Func* currentFunction = nullptr;
		std::string currentBody;
		int brackets = 1;

		std::ifstream infile(kromjs.c_str());
		std::string line;
		while (std::getline(infile, line)) {
			switch (mode) {
				case ParseRegular: {
					if (endsWith(line, ".prototype = {") || line.find(".prototype = $extend(") != std::string::npos) { // parse methods
						mode = ParseMethods;
					}
					else if (line.find(" = function(") != std::string::npos && line.find("var ") == std::string::npos) {
						size_t first = 0;
						size_t last = line.find(".");
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

							//printf("Found method %s.\n", methodname.c_str());
							currentClass->methods[methodname] = currentFunction;
						}
						else {
							currentFunction = currentClass->methods[methodname];
						}
						mode = ParseFunction;
						currentBody = "";
						brackets = 1;
					}
					// hxClasses["BigBlock"] = BigBlock;
					// var BigBlock = $hxClasses["BigBlock"] = function(xx,yy) {
					else if (line.find("$hxClasses[\"") != std::string::npos) { //(startsWith(line, "$hxClasses[\"")) {
						size_t first = line.find('\"');
						size_t last = line.find_last_of('\"');
						std::string name = line.substr(first + 1, last - first - 1);
						first = line.find(' ');
						last = line.find(' ', first + 1);
						std::string internal_name = line.substr(first + 1, last - first - 1);
						if (classes.find(internal_name) == classes.end()) {
							//printf("Found type %s.\n", internal_name.c_str());
							currentClass = new Klass;
							currentClass->name = name;
							currentClass->internal_name = internal_name;
							classes[internal_name] = currentClass;
							++types;
						}
						else {
							currentClass = classes[internal_name];
						}
					}
					break;
				}
				case ParseMethods: {
					// ,draw: function(g) {
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

							//printf("Found method %s.\n", methodname.c_str());
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
					if (line.find('{') != std::string::npos) ++brackets;
					if (line.find('}') != std::string::npos) --brackets;
					if (brackets > 0) {
						currentBody += line + " ";
					}
					else {
						if (currentFunction->body == "") {
							currentFunction->body = currentBody;
						}
						else if (currentFunction->body != currentBody) {
							currentFunction->body = currentBody;

							v8::Locker locker{isolate};

							Isolate::Scope isolate_scope(isolate);
							HandleScope handle_scope(isolate);
							v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, globalContext);
							Context::Scope context_scope(context);

							// BlocksFromHeaven.prototype.loadingFinished = new Function([a, b], "lots of text;");
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

							// Kore::log(Kore::Info, "Script:\n%s\n", script.c_str());
							sendLogMessage("Patching method %s in class %s.", currentFunction->name.c_str(), currentClass->name.c_str());

							Local<String> source = String::NewFromUtf8(isolate, script.c_str(), NewStringType::kNormal).ToLocalChecked();

							TryCatch try_catch(isolate);

							Local<Script> compiled_script;
							if (!Script::Compile(context, source).ToLocal(&compiled_script)) {
								v8::String::Utf8Value stack_trace(try_catch.StackTrace());
								sendLogMessage("Trace: %s", *stack_trace);
							}

							Local<Value> result;
							if (!compiled_script->Run(context).ToLocal(&result)) {
								v8::String::Utf8Value stack_trace(try_catch.StackTrace());
								sendLogMessage("Trace: %s", *stack_trace);
							}
						}
						mode = ParseMethods;
					}
					break;
				}
				case ParseFunction: {
					if (line.find('{') != std::string::npos) ++brackets;
					if (line.find('}') != std::string::npos) --brackets;
					if (brackets > 0) {
						currentBody += line + " ";
					}
					else {
						if (currentFunction->body == "") {
							currentFunction->body = currentBody;
						}
						else if (currentFunction->body != currentBody) {
							currentFunction->body = currentBody;

							v8::Locker locker{isolate};

							Isolate::Scope isolate_scope(isolate);
							HandleScope handle_scope(isolate);
							v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, globalContext);
							Context::Scope context_scope(context);

							// BlocksFromHeaven.prototype.loadingFinished = new Function([a, b], "lots of text;");
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

							// Kore::log(Kore::Info, "Script:\n%s\n", script.c_str());
							sendLogMessage("Patching function %s in class %s.", currentFunction->name.c_str(), currentClass->name.c_str());

							Local<String> source = String::NewFromUtf8(isolate, script.c_str(), NewStringType::kNormal).ToLocalChecked();

							TryCatch try_catch(isolate);

							Local<Script> compiled_script;
							if (!Script::Compile(context, source).ToLocal(&compiled_script)) {
								v8::String::Utf8Value stack_trace(try_catch.StackTrace());
								sendLogMessage("Trace: %s", *stack_trace);
							}

							Local<Value> result;
							if (!compiled_script->Run(context).ToLocal(&result)) {
								v8::String::Utf8Value stack_trace(try_catch.StackTrace());
								sendLogMessage("Trace: %s", *stack_trace);
							}
						}
						mode = ParseRegular;
					}
					break;
				}
			}
		}
		sendLogMessage("%i new types found.", types);
	}*/
}

extern "C" void watchDirectories(char* path1, char* path2);

extern "C" void filechanged(char* path) {
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

int kore(int argc, char** argv) {
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

	bool readStdoutPath = false;
	bool readConsolePid = false;
	bool readPort = false;
	int port = 0;
	for (int i = 3; i < argc; ++i) {
		if (readPort) {
			port = atoi(argv[i]);
			readPort = false;
		}
		else if (strcmp(argv[i], "--debug") == 0) {
			debugMode = true;
			readPort = true;
		}
		else if (strcmp(argv[i], "--watch") == 0) {
			//**watch = true;
		}
		else if (strcmp(argv[i], "--nosound") == 0) {
			nosound = true;
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
	}

	kromjs = assetsdir + "/krom.js";
	Kore::setFilesLocation(&assetsdir[0u]);

	Kore::FileReader reader;
	reader.open("krom.js");
	char* code = new char[reader.size() + 1];
	memcpy(code, reader.readAll(), reader.size());
	code[reader.size()] = 0;
	reader.close();

	#ifdef KORE_WINDOWS
	char dirsep = '\\';
	#else
	char dirsep = '/';
	#endif
	startV8((bindir + dirsep).c_str());

	if (watch) {
		parseCode();
	}

	Kore::threadsInit();

	if (watch) {
		watchDirectories(argv[1], argv[2]);
	}

	if (debugMode) {
		startDebugger(port);
		while (!tickDebugger()) {}
		//Sleep(1000);
	}

	startKrom(code);
	Kore::System::start();

	exit(0); // TODO

	endV8();

	return 0;
}
