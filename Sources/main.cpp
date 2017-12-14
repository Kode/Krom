#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pch.h"
#include <Kore/IO/FileReader.h>
#include <Kore/IO/FileWriter.h>
#include <Kore/Graphics4/Graphics.h>
#include <Kore/Graphics4/PipelineState.h>
#include <Kore/Graphics4/Shader.h>
#include <Kore/Input/Keyboard.h>
#include <Kore/Input/Mouse.h>
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

#include "../V8/include/libplatform/libplatform.h"
#include "../V8/include/v8.h"
#include <v8-inspector.h>

#include <stdio.h>
#include <stdarg.h>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace v8;

void sendMessage(const char* message);

#ifdef KORE_MACOS
const char* macgetresourcepath();
#endif

Global<Context> globalContext;
Isolate* isolate;

extern std::unique_ptr<v8_inspector::V8Inspector> v8inspector;

const char* getExeDir();

namespace {
	bool debugMode = false;
	bool watch = false;
	bool nosound = false;
	bool nowindow = false;

	Platform* plat;
	Global<Function> updateFunction;
	Global<Function> dropFilesFunction;
	Global<Function> keyboardDownFunction;
	Global<Function> keyboardUpFunction;
	Global<Function> keyboardPressFunction;
	Global<Function> mouseDownFunction;
	Global<Function> mouseUpFunction;
	Global<Function> mouseMoveFunction;
	Global<Function> mouseWheelFunction;
	Global<Function> gamepadAxisFunction;
	Global<Function> gamepadButtonFunction;
	Global<Function> audioFunction;
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
	void gamepad1Axis(int axis, float value);
	void gamepad1Button(int button, float value);
	void gamepad2Axis(int axis, float value);
	void gamepad2Button(int button, float value);
	void gamepad3Axis(int axis, float value);
	void gamepad3Button(int button, float value);
	void gamepad4Axis(int axis, float value);
	void gamepad4Button(int button, float value);

	void krom_init(const v8::FunctionCallbackInfo<v8::Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<Value> arg = args[0];
		String::Utf8Value title(arg);
		int width = args[1]->ToInt32()->Value();
		int height = args[2]->ToInt32()->Value();
		int samplesPerPixel = args[3]->ToInt32()->Value();
		bool vSync = args[4]->ToBoolean()->Value();
		int windowMode = args[5]->ToInt32()->Value();
		bool resizable = args[6]->ToBoolean()->Value();
		bool maximizable = args[7]->ToBoolean()->Value();
		bool minimizable = args[8]->ToBoolean()->Value();

		Kore::WindowOptions options;
		options.title = *title;
		options.width = width;
		options.height = height;
		options.x = -1;
		options.y = -1;
		options.targetDisplay = -1;
		options.showWindow = !nowindow;
		Kore::System::setShowWindowFlag(options.showWindow);
		options.vSync = vSync;
		options.mode = Kore::WindowMode(windowMode);
		options.resizable = resizable;
		options.maximizable = maximizable;
		options.minimizable = minimizable;
		options.rendererOptions.depthBufferBits = 16;
		options.rendererOptions.stencilBufferBits = 8;
		options.rendererOptions.textureFormat = 0;
		options.rendererOptions.antialiasing = samplesPerPixel;
		Kore::System::initWindow(options);

		//Mixer::init();
		//Audio::init();
		mutex.Create();
		if (!nosound) {
			Kore::Audio2::audioCallback = mix;
			Kore::Audio2::init();
			initAudioBuffer();
		}
		Kore::Random::init(Kore::System::time() * 1000);

		Kore::System::setCallback(update);
		Kore::System::setDropFilesCallback(dropFiles);

		Kore::Keyboard::the()->KeyDown = keyDown;
		Kore::Keyboard::the()->KeyUp = keyUp;
        Kore::Keyboard::the()->KeyPress = keyPress;
		Kore::Mouse::the()->Move = mouseMove;
		Kore::Mouse::the()->Press = mouseDown;
		Kore::Mouse::the()->Release = mouseUp;
		Kore::Mouse::the()->Scroll = mouseWheel;
		Kore::Gamepad::get(0)->Axis = gamepad1Axis;
		Kore::Gamepad::get(0)->Button = gamepad1Button;
		Kore::Gamepad::get(1)->Axis = gamepad2Axis;
		Kore::Gamepad::get(1)->Button = gamepad2Button;
		Kore::Gamepad::get(2)->Axis = gamepad3Axis;
		Kore::Gamepad::get(2)->Button = gamepad3Button;
		Kore::Gamepad::get(3)->Axis = gamepad4Axis;
		Kore::Gamepad::get(3)->Button = gamepad4Button;

		//Mixer::play(music);
	}

	void sendLogMessageArgs(const char* format, va_list args) {
		char message[4096];
		vsnprintf(message, sizeof(message) - 2, format, args);

		Kore::log(Kore::Info, "%s", message);
		char json[4096];
		strcpy(json, "{\"method\":\"Log.entryAdded\",\"params\":{\"entry\":{\"source\":\"javascript\",\"level\":\"log\",\"text\":\"");
		strcat(json, message);
		strcat(json, "\",\"timestamp\":0}}}");
		sendMessage(json);
	}

	void sendLogMessage(const char* format, ...) {
		va_list args;
		va_start(args, format);
		sendLogMessageArgs(format, args);
		va_end(args);
	}

	void LogCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
		if (args.Length() < 1) return;
		HandleScope scope(args.GetIsolate());
		Local<Value> arg = args[0];
		String::Utf8Value value(arg);
		sendLogMessage(*value);
	}

	void graphics_clear(const v8::FunctionCallbackInfo<v8::Value>& args) {
		HandleScope scope(args.GetIsolate());
		int flags = args[0]->ToInt32()->Value();
		int color = args[1]->ToInt32()->Value();
		float depth = args[2]->ToNumber()->Value();
		int stencil = args[3]->ToInt32()->Value();
		Kore::Graphics4::clear(flags, color, depth, stencil);
	}

	void krom_set_callback(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<Value> arg = args[0];
		Local<Function> func = Local<Function>::Cast(arg);
		updateFunction.Reset(isolate, func);
	}

	void krom_set_drop_files_callback(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<Value> arg = args[0];
		Local<Function> func = Local<Function>::Cast(arg);
		dropFilesFunction.Reset(isolate, func);
	}

	void krom_set_keyboard_down_callback(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<Value> arg = args[0];
		Local<Function> func = Local<Function>::Cast(arg);
		keyboardDownFunction.Reset(isolate, func);
	}

	void krom_set_keyboard_up_callback(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<Value> arg = args[0];
		Local<Function> func = Local<Function>::Cast(arg);
		keyboardUpFunction.Reset(isolate, func);
	}

	void krom_set_keyboard_press_callback(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<Value> arg = args[0];
		Local<Function> func = Local<Function>::Cast(arg);
		keyboardPressFunction.Reset(isolate, func);
	}

	void krom_set_mouse_down_callback(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<Value> arg = args[0];
		Local<Function> func = Local<Function>::Cast(arg);
		mouseDownFunction.Reset(isolate, func);
	}

	void krom_set_mouse_up_callback(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<Value> arg = args[0];
		Local<Function> func = Local<Function>::Cast(arg);
		mouseUpFunction.Reset(isolate, func);
	}

	void krom_set_mouse_move_callback(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<Value> arg = args[0];
		Local<Function> func = Local<Function>::Cast(arg);
		mouseMoveFunction.Reset(isolate, func);
	}

	void krom_set_mouse_wheel_callback(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<Value> arg = args[0];
		Local<Function> func = Local<Function>::Cast(arg);
		mouseWheelFunction.Reset(isolate, func);
	}

	void krom_set_gamepad_axis_callback(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<Value> arg = args[0];
		Local<Function> func = Local<Function>::Cast(arg);
		gamepadAxisFunction.Reset(isolate, func);
	}

	void krom_set_gamepad_button_callback(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<Value> arg = args[0];
		Local<Function> func = Local<Function>::Cast(arg);
		gamepadButtonFunction.Reset(isolate, func);
	}

	void krom_lock_mouse(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Kore::Mouse::the()->lock(0);
	}

	void krom_unlock_mouse(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Kore::Mouse::the()->unlock(0);
	}

	void krom_can_lock_mouse(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Kore::Mouse::the()->canLock(0);
	}

	void krom_is_mouse_locked(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Kore::Mouse::the()->isLocked(0);
	}

	void krom_set_audio_callback(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<Value> arg = args[0];
		Local<Function> func = Local<Function>::Cast(arg);
		audioFunction.Reset(isolate, func);
	}

	// TODO: krom_audio_lock
	void audio_thread(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		bool lock = args[0]->ToBoolean()->Value();


		if (lock) mutex.Lock();    //v8::Locker::Locker(isolate);
		else mutex.Unlock();       //v8::Unlocker(args.GetIsolate());


	}

	void krom_create_indexbuffer(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, new Kore::Graphics4::IndexBuffer(args[0]->Int32Value())));
		args.GetReturnValue().Set(obj);
	}

	void krom_delete_indexbuffer(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> field = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::IndexBuffer* buffer = (Kore::Graphics4::IndexBuffer*)field->Value();
		delete buffer;
	}

	void krom_set_indices(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());

		Local<External> field = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::IndexBuffer* buffer = (Kore::Graphics4::IndexBuffer*)field->Value();

		Local<Uint32Array> u32array = Local<Uint32Array>::Cast(args[1]);
		ArrayBuffer::Contents content;
		if (u32array->Buffer()->IsExternal()) content = u32array->Buffer()->GetContents();
		else content = u32array->Buffer()->Externalize();

		int* from = (int*)content.Data();
		int* indices = buffer->lock();
		for (int32_t i = 0; i < buffer->count(); ++i) {
			indices[i] = from[i];
		}
		buffer->unlock();
	}

	void krom_set_indexbuffer(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> field = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::IndexBuffer* buffer = (Kore::Graphics4::IndexBuffer*)field->Value();
		Kore::Graphics4::setIndexBuffer(*buffer);
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

	void krom_create_vertexbuffer(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());

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

		obj->SetInternalField(0, External::New(isolate, new Kore::Graphics4::VertexBuffer(args[0]->Int32Value(), structure, args[2]->Int32Value())));
		args.GetReturnValue().Set(obj);
	}

	void krom_delete_vertexbuffer(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> field = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::VertexBuffer* buffer = (Kore::Graphics4::VertexBuffer*)field->Value();
		delete buffer;
	}

	void krom_set_vertices(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());

		Local<External> field = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::VertexBuffer* buffer = (Kore::Graphics4::VertexBuffer*)field->Value();

		Local<Float32Array> f32array = Local<Float32Array>::Cast(args[1]);
		ArrayBuffer::Contents content;
		if (f32array->Buffer()->IsExternal()) content = f32array->Buffer()->GetContents();
		else content = f32array->Buffer()->Externalize();

		float* from = (float*)content.Data();
		float* vertices = buffer->lock();
		for (int32_t i = 0; i < buffer->count() * buffer->stride() / 4; ++i) {
			vertices[i] = from[i];
		}
		buffer->unlock();
	}

	void krom_set_vertexbuffer(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> field = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::VertexBuffer* buffer = (Kore::Graphics4::VertexBuffer*)field->Value();
		Kore::Graphics4::setVertexBuffer(*buffer);
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

	void krom_draw_indexed_vertices(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		int start = args[0]->ToInt32()->Value();
		int count = args[1]->ToInt32()->Value();
		if (count < 0) Kore::Graphics4::drawIndexedVertices();
		else Kore::Graphics4::drawIndexedVertices(start, count);
	}

	void krom_draw_indexed_vertices_instanced(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		int instanceCount = args[0]->ToInt32()->Value();
		int start = args[1]->ToInt32()->Value();
		int count = args[2]->ToInt32()->Value();
		if (count < 0) Kore::Graphics4::drawIndexedVerticesInstanced(instanceCount);
		else Kore::Graphics4::drawIndexedVerticesInstanced(instanceCount, start, count);
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

	void krom_delete_shader(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> field = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::Shader* shader = (Kore::Graphics4::Shader*)field->Value();
		delete shader;
	}

	void krom_create_pipeline(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Kore::Graphics4::PipelineState* pipeline = new Kore::Graphics4::PipelineState;

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(8);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, pipeline));
		args.GetReturnValue().Set(obj);
	}

	void krom_delete_pipeline(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<Object> progobj = args[0]->ToObject();
		Local<External> progfield = Local<External>::Cast(progobj->GetInternalField(0));
		Kore::Graphics4::PipelineState* pipeline = (Kore::Graphics4::PipelineState*)progfield->Value();
		delete pipeline;
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

		Local<Value> vsnameobj = progobj->Get(String::NewFromUtf8(isolate, "vsname"));
		String::Utf8Value vsname(vsnameobj);

		Local<Value> fsnameobj = progobj->Get(String::NewFromUtf8(isolate, "fsname"));
		String::Utf8Value fsname(fsnameobj);

		bool shaderChanged = false;

		if (shaderChanges[*vsname]) {
			shaderChanged = true;
			sendLogMessage("Reloading shader %s.", *vsname);
			std::string filename = shaderFileNames[*vsname];
			std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary );
			std::vector<char> buffer((std::istreambuf_iterator<char>(input)), (std::istreambuf_iterator<char>()));
			Kore::Graphics4::Shader* vertexShader = new Kore::Graphics4::Shader(buffer.data(), (int)buffer.size(), Kore::Graphics4::VertexShader);
			progobj->SetInternalField(3, External::New(isolate, vertexShader));
			shaderChanges[*vsname] = false;
		}

		if (shaderChanges[*fsname]) {
			shaderChanged = true;
			sendLogMessage("Reloading shader %s.", *fsname);
			std::string filename = shaderFileNames[*fsname];
			std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary );
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
				std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary );
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
				std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary );
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
				std::ifstream input((shadersdir + "/" + filename).c_str(), std::ios::binary );
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

		Kore::Graphics4::setPipeline(pipeline);
	}

	void krom_load_image(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		String::Utf8Value utf8_value(args[0]);
		bool readable = args[1]->ToBoolean()->Value();
		Kore::Graphics4::Texture* texture = new Kore::Graphics4::Texture(*utf8_value, readable);

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, texture));
		obj->Set(String::NewFromUtf8(isolate, "width"), Int32::New(isolate, texture->width));
		obj->Set(String::NewFromUtf8(isolate, "height"), Int32::New(isolate, texture->height));
		obj->Set(String::NewFromUtf8(isolate, "realWidth"), Int32::New(isolate, texture->texWidth));
		obj->Set(String::NewFromUtf8(isolate, "realHeight"), Int32::New(isolate, texture->texHeight));
		obj->Set(String::NewFromUtf8(isolate, "filename"), args[0]);
		args.GetReturnValue().Set(obj);
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

		Kore::log(Kore::Info, "Load Sound %s", *utf8_value);

		Local<ArrayBuffer> buffer;
		ArrayBuffer::Contents content;

		if (sound->format.bitsPerSample == 8) { // TODO: test
			buffer = ArrayBuffer::New(isolate, sound->size * sizeof(float));
			content = buffer->Externalize();
			float* to = (float*)content.Data();

			for (int i = 0; i < sound->size; i += 2) {
				to[i + 0] = sound->left[i / 2] / 255.0 * 2.0 - 1.0;
				to[i + 1] = sound->right[i / 2] / 255.0 * 2.0 - 1.0;
			}
		}
		else if (sound->format.bitsPerSample == 16) {
			buffer = ArrayBuffer::New(isolate, (sound->size / 2) * sizeof(float));
			content = buffer->Externalize();
			float* to = (float*)content.Data();

			Kore::s16* left  = (Kore::s16*)&sound->left[0];
			Kore::s16* right = (Kore::s16*)&sound->right[0];
			for (int i = 0; i < sound->size / 2; i += 2) {
				to[i + 0] = left[i / 2] / 32767.0f;
				to[i + 1] = right[i / 2] / 32767.0f;

				/*if(i < 10) {
					Kore::log(Kore::Info, "to[%i] = %f",i+0, to[i + 0]);
					Kore::log(Kore::Info, "to[%i] = %f",i+1, to[i + 1]);
				}*/
			}
		}

		args.GetReturnValue().Set(buffer);
	}


	void write_audio_buffer(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		float value = (float)args[0]->ToNumber()->Value();

		*(float*)&Kore::Audio2::buffer.data[Kore::Audio2::buffer.writeLocation] = value;
		Kore::Audio2::buffer.writeLocation += 4;
		if (Kore::Audio2::buffer.writeLocation >= Kore::Audio2::buffer.dataSize) Kore::Audio2::buffer.writeLocation = 0;
	}

	void krom_load_blob(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		String::Utf8Value utf8_value(args[0]);
		Kore::FileReader reader;
		reader.open(*utf8_value);

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

	void krom_get_constant_location(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> progfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::PipelineState* pipeline = (Kore::Graphics4::PipelineState*)progfield->Value();

		String::Utf8Value utf8_value(args[1]);
		Kore::Graphics4::ConstantLocation location = pipeline->getConstantLocation(*utf8_value);

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, new Kore::Graphics4::ConstantLocation(location)));
		args.GetReturnValue().Set(obj);
	}

	void krom_get_texture_unit(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> progfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::PipelineState* pipeline = (Kore::Graphics4::PipelineState*)progfield->Value();

		String::Utf8Value utf8_value(args[1]);
		Kore::Graphics4::TextureUnit unit = pipeline->getTextureUnit(*utf8_value);

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, new Kore::Graphics4::TextureUnit(unit)));
		args.GetReturnValue().Set(obj);
	}

	void krom_set_texture(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> unitfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::TextureUnit* unit = (Kore::Graphics4::TextureUnit*)unitfield->Value();

		if (args[1]->IsNull() || args[1]->IsUndefined()) return;

		Local<Object> image = args[1]->ToObject();
		Local<Value> tex = image->Get(String::NewFromUtf8(isolate, "texture_"));
		Local<Value> rt = image->Get(String::NewFromUtf8(isolate, "renderTarget_"));

		if (tex->IsObject()) {
			Kore::Graphics4::Texture* texture;
			String::Utf8Value filename(tex->ToObject()->Get(String::NewFromUtf8(isolate, "filename")));
			if (imageChanges[*filename]) {
				imageChanges[*filename] = false;
				sendLogMessage("Image %s changed.", *filename);
				texture = new Kore::Graphics4::Texture(*filename);
				tex->ToObject()->SetInternalField(0, External::New(isolate, texture));
			}
			else {
				Local<External> texfield = Local<External>::Cast(tex->ToObject()->GetInternalField(0));
				texture = (Kore::Graphics4::Texture*)texfield->Value();
			}
			Kore::Graphics4::setTexture(*unit, texture);
		}
		else if (rt->IsObject()) {
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

	void krom_set_texture_parameters(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> unitfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::TextureUnit* unit = (Kore::Graphics4::TextureUnit*)unitfield->Value();
		Kore::Graphics4::setTextureAddressing(*unit, Kore::Graphics4::U, convertTextureAddressing(args[1]->ToInt32()->Int32Value()));
		Kore::Graphics4::setTextureAddressing(*unit, Kore::Graphics4::V, convertTextureAddressing(args[2]->ToInt32()->Int32Value()));
		Kore::Graphics4::setTextureMinificationFilter(*unit, convertTextureFilter(args[3]->ToInt32()->Int32Value()));
		Kore::Graphics4::setTextureMagnificationFilter(*unit, convertTextureFilter(args[4]->ToInt32()->Int32Value()));
		Kore::Graphics4::setTextureMipmapFilter(*unit, convertMipmapFilter(args[5]->ToInt32()->Int32Value()));
	}

	void krom_set_texture_3d_parameters(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> unitfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::TextureUnit* unit = (Kore::Graphics4::TextureUnit*)unitfield->Value();
		Kore::Graphics4::setTexture3DAddressing(*unit, Kore::Graphics4::U, convertTextureAddressing(args[1]->ToInt32()->Int32Value()));
		Kore::Graphics4::setTexture3DAddressing(*unit, Kore::Graphics4::V, convertTextureAddressing(args[2]->ToInt32()->Int32Value()));
		Kore::Graphics4::setTexture3DAddressing(*unit, Kore::Graphics4::W, convertTextureAddressing(args[3]->ToInt32()->Int32Value()));
		Kore::Graphics4::setTexture3DMinificationFilter(*unit, convertTextureFilter(args[4]->ToInt32()->Int32Value()));
		Kore::Graphics4::setTexture3DMagnificationFilter(*unit, convertTextureFilter(args[5]->ToInt32()->Int32Value()));
		Kore::Graphics4::setTexture3DMipmapFilter(*unit, convertMipmapFilter(args[6]->ToInt32()->Int32Value()));
	}

	void krom_set_bool(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::ConstantLocation* location = (Kore::Graphics4::ConstantLocation*)locationfield->Value();
		int32_t value = args[1]->ToInt32()->Value();
		Kore::Graphics4::setBool(*location, value != 0);
	}

	void krom_set_int(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::ConstantLocation* location = (Kore::Graphics4::ConstantLocation*)locationfield->Value();
		int32_t value = args[1]->ToInt32()->Value();
		Kore::Graphics4::setInt(*location, value);
	}

	void krom_set_float(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::ConstantLocation* location = (Kore::Graphics4::ConstantLocation*)locationfield->Value();
		float value = (float)args[1]->ToNumber()->Value();
		Kore::Graphics4::setFloat(*location, value);
	}

	void krom_set_float2(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::ConstantLocation* location = (Kore::Graphics4::ConstantLocation*)locationfield->Value();
		float value1 = (float)args[1]->ToNumber()->Value();
		float value2 = (float)args[2]->ToNumber()->Value();
		Kore::Graphics4::setFloat2(*location, value1, value2);
	}

	void krom_set_float3(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::ConstantLocation* location = (Kore::Graphics4::ConstantLocation*)locationfield->Value();
		float value1 = (float)args[1]->ToNumber()->Value();
		float value2 = (float)args[2]->ToNumber()->Value();
		float value3 = (float)args[3]->ToNumber()->Value();
		Kore::Graphics4::setFloat3(*location, value1, value2, value3);
	}

	void krom_set_float4(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::ConstantLocation* location = (Kore::Graphics4::ConstantLocation*)locationfield->Value();
		float value1 = (float)args[1]->ToNumber()->Value();
		float value2 = (float)args[2]->ToNumber()->Value();
		float value3 = (float)args[3]->ToNumber()->Value();
		float value4 = (float)args[4]->ToNumber()->Value();
		Kore::Graphics4::setFloat4(*location, value1, value2, value3, value4);
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

		Local<Object> matrix = args[1]->ToObject();
		float _00 = matrix->Get(String::NewFromUtf8(isolate, "_00"))->ToNumber()->Value();
		float _01 = matrix->Get(String::NewFromUtf8(isolate, "_01"))->ToNumber()->Value();
		float _02 = matrix->Get(String::NewFromUtf8(isolate, "_02"))->ToNumber()->Value();
		float _03 = matrix->Get(String::NewFromUtf8(isolate, "_03"))->ToNumber()->Value();
		float _10 = matrix->Get(String::NewFromUtf8(isolate, "_10"))->ToNumber()->Value();
		float _11 = matrix->Get(String::NewFromUtf8(isolate, "_11"))->ToNumber()->Value();
		float _12 = matrix->Get(String::NewFromUtf8(isolate, "_12"))->ToNumber()->Value();
		float _13 = matrix->Get(String::NewFromUtf8(isolate, "_13"))->ToNumber()->Value();
		float _20 = matrix->Get(String::NewFromUtf8(isolate, "_20"))->ToNumber()->Value();
		float _21 = matrix->Get(String::NewFromUtf8(isolate, "_21"))->ToNumber()->Value();
		float _22 = matrix->Get(String::NewFromUtf8(isolate, "_22"))->ToNumber()->Value();
		float _23 = matrix->Get(String::NewFromUtf8(isolate, "_23"))->ToNumber()->Value();
		float _30 = matrix->Get(String::NewFromUtf8(isolate, "_30"))->ToNumber()->Value();
		float _31 = matrix->Get(String::NewFromUtf8(isolate, "_31"))->ToNumber()->Value();
		float _32 = matrix->Get(String::NewFromUtf8(isolate, "_32"))->ToNumber()->Value();
		float _33 = matrix->Get(String::NewFromUtf8(isolate, "_33"))->ToNumber()->Value();

		Kore::mat4 m;
		m.Set(0, 0, _00); m.Set(1, 0, _01); m.Set(2, 0, _02); m.Set(3, 0, _03);
		m.Set(0, 1, _10); m.Set(1, 1, _11); m.Set(2, 1, _12); m.Set(3, 1, _13);
		m.Set(0, 2, _20); m.Set(1, 2, _21); m.Set(2, 2, _22); m.Set(3, 2, _23);
		m.Set(0, 3, _30); m.Set(1, 3, _31); m.Set(2, 3, _32); m.Set(3, 3, _33);

		Kore::Graphics4::setMatrix(*location, m);
	}

	void krom_set_matrix3(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::ConstantLocation* location = (Kore::Graphics4::ConstantLocation*)locationfield->Value();

		Local<Object> matrix = args[1]->ToObject();
		float _00 = matrix->Get(String::NewFromUtf8(isolate, "_00"))->ToNumber()->Value();
		float _01 = matrix->Get(String::NewFromUtf8(isolate, "_01"))->ToNumber()->Value();
		float _02 = matrix->Get(String::NewFromUtf8(isolate, "_02"))->ToNumber()->Value();
		float _10 = matrix->Get(String::NewFromUtf8(isolate, "_10"))->ToNumber()->Value();
		float _11 = matrix->Get(String::NewFromUtf8(isolate, "_11"))->ToNumber()->Value();
		float _12 = matrix->Get(String::NewFromUtf8(isolate, "_12"))->ToNumber()->Value();
		float _20 = matrix->Get(String::NewFromUtf8(isolate, "_20"))->ToNumber()->Value();
		float _21 = matrix->Get(String::NewFromUtf8(isolate, "_21"))->ToNumber()->Value();
		float _22 = matrix->Get(String::NewFromUtf8(isolate, "_22"))->ToNumber()->Value();

		Kore::mat3 m;
		m.Set(0, 0, _00); m.Set(1, 0, _01); m.Set(2, 0, _02);
		m.Set(0, 1, _10); m.Set(1, 1, _11); m.Set(2, 1, _12);
		m.Set(0, 2, _20); m.Set(1, 2, _21); m.Set(2, 2, _22);

		Kore::Graphics4::setMatrix(*location, m);
	}

	void krom_get_time(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		args.GetReturnValue().Set(Number::New(isolate, Kore::System::time()));
	}

	void krom_window_width(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		int windowId = args[0]->ToInt32()->Value();
		args.GetReturnValue().Set(Int32::New(isolate, Kore::System::windowWidth(windowId)));
	}

	void krom_window_height(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		int windowId = args[0]->ToInt32()->Value();
		args.GetReturnValue().Set(Int32::New(isolate, Kore::System::windowHeight(windowId)));
	}

	void krom_screen_dpi(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		//int windowId = args[0]->ToInt32()->Value();
		args.GetReturnValue().Set(Int32::New(isolate, Kore::System::screenDpi()));
	}

	void krom_system_id(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		args.GetReturnValue().Set(String::NewFromUtf8(isolate, Kore::System::systemId()));
	}

	void krom_request_shutdown(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Kore::System::stop();
	}

	void krom_display_count(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		args.GetReturnValue().Set(Int32::New(isolate, Kore::Display::count()));
	}

	void krom_display_width(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		int index = args[0]->ToInt32()->Value();
		args.GetReturnValue().Set(Int32::New(isolate, Kore::Display::width(index)));
	}

	void krom_display_height(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		int index = args[0]->ToInt32()->Value();
		args.GetReturnValue().Set(Int32::New(isolate, Kore::Display::height(index)));
	}

	void krom_display_x(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		int index = args[0]->ToInt32()->Value();
		args.GetReturnValue().Set(Int32::New(isolate, Kore::Display::x(index)));
	}

	void krom_display_y(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		int index = args[0]->ToInt32()->Value();
		args.GetReturnValue().Set(Int32::New(isolate, Kore::Display::y(index)));
	}

	void krom_display_is_primary(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		int index = args[0]->ToInt32()->Value();
		args.GetReturnValue().Set(Boolean::New(isolate, Kore::Display::isPrimary(index)));
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

	void krom_create_render_target(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Kore::Graphics4::RenderTarget* renderTarget = new Kore::Graphics4::RenderTarget(args[0]->ToInt32()->Value(), args[1]->ToInt32()->Value(), args[2]->ToInt32()->Value(), false, (Kore::Graphics4::RenderTargetFormat)args[3]->ToInt32()->Value(), args[4]->ToInt32()->Value());

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, renderTarget));
		obj->Set(String::NewFromUtf8(isolate, "width"), Int32::New(isolate, renderTarget->width));
		obj->Set(String::NewFromUtf8(isolate, "height"), Int32::New(isolate, renderTarget->height));
		args.GetReturnValue().Set(obj);
	}

	void krom_create_render_target_cube_map(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Kore::Graphics4::RenderTarget* renderTarget = new Kore::Graphics4::RenderTarget(args[0]->ToInt32()->Value(), args[1]->ToInt32()->Value(), false, (Kore::Graphics4::RenderTargetFormat)args[2]->ToInt32()->Value(), args[3]->ToInt32()->Value());

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);

		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, renderTarget));
		obj->Set(String::NewFromUtf8(isolate, "width"), Int32::New(isolate, renderTarget->width));
		obj->Set(String::NewFromUtf8(isolate, "height"), Int32::New(isolate, renderTarget->height));
		args.GetReturnValue().Set(obj);
	}

	void krom_create_texture(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Kore::Graphics4::Texture* texture = new Kore::Graphics4::Texture(args[0]->ToInt32()->Value(), args[1]->ToInt32()->Value(), (Kore::Graphics4::Image::Format)args[2]->ToInt32()->Value(), false);

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

	void krom_unlock_texture(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());

		Local<External> field = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::Texture* texture = (Kore::Graphics4::Texture*)field->Value();

		Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[1]);
		ArrayBuffer::Contents content = buffer->Externalize();

		Kore::u8* b = (Kore::u8*)content.Data();
		Kore::u8* tex = texture->lock();
		for (int32_t i = 0; i < ((texture->format == Kore::Graphics4::Image::RGBA32) ? (4 * texture->width * texture->height) : (texture->width * texture->height)); ++i) {
			tex[i] = b[i];
		}
		texture->unlock();
	}

	void krom_clear_texture(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> field = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::Texture* texture = (Kore::Graphics4::Texture*)field->Value();
		int x = args[1]->ToInt32()->Value();
		int y = args[2]->ToInt32()->Value();
		int z = args[3]->ToInt32()->Value();
		int width = args[4]->ToInt32()->Value();
		int height = args[5]->ToInt32()->Value();
		int depth = args[6]->ToInt32()->Value();
		int color = args[7]->ToInt32()->Value();
		texture->clear(x, y, z, width, height, depth, color);
	}

	void krom_generate_texture_mipmaps(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> field = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::Texture* texture = (Kore::Graphics4::Texture*)field->Value();
		texture->generateMipmaps(args[0]->ToInt32()->Value());
	}

	void krom_generate_render_target_mipmaps(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> field = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Graphics4::RenderTarget* rt = (Kore::Graphics4::RenderTarget*)field->Value();
		rt->generateMipmaps(args[0]->ToInt32()->Value());
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

	void krom_viewport(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());

		int x = args[0]->ToInt32()->Int32Value();
		int y = args[1]->ToInt32()->Int32Value();
		int w = args[2]->ToInt32()->Int32Value();
		int h = args[3]->ToInt32()->Int32Value();

		Kore::Graphics4::viewport(x, y, w, h);
	}

	void krom_scissor(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());

		int x = args[0]->ToInt32()->Int32Value();
		int y = args[1]->ToInt32()->Int32Value();
		int w = args[2]->ToInt32()->Int32Value();
		int h = args[3]->ToInt32()->Int32Value();

		Kore::Graphics4::scissor(x, y, w, h);
	}

	void krom_disable_scissor(const FunctionCallbackInfo<Value>& args) {
		Kore::Graphics4::disableScissor();
	}

	void krom_render_targets_inverted_y(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		args.GetReturnValue().Set(Boolean::New(isolate, Kore::Graphics4::renderTargetsInvertedY()));
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

	void krom_end(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());

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

	void krom_sys_command(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		String::Utf8Value utf8_cmd(args[0]);
		int result = system(*utf8_cmd);
		args.GetReturnValue().Set(Int32::New(isolate, result));
	}

	void krom_save_path(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		args.GetReturnValue().Set(String::NewFromUtf8(isolate, Kore::System::savePath()));
	}

	void startV8(const char* bindir) {
#if defined(KORE_MACOS)
		char filepath[256];
		strcpy(filepath, macgetresourcepath());
		strcat(filepath, "/");
		strcat(filepath, "macos");
		strcat(filepath, "/");
		V8::InitializeExternalStartupData(filepath);
#else
		V8::InitializeExternalStartupData(bindir);
		V8::InitializeExternalStartupData("./");
#endif

		plat = platform::CreateDefaultPlatform();
		V8::InitializePlatform(plat);
		V8::Initialize();

		Isolate::CreateParams create_params;
		create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
		isolate = Isolate::New(create_params);

		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);

		Local<ObjectTemplate> krom = ObjectTemplate::New(isolate);
		krom->Set(String::NewFromUtf8(isolate, "init"), FunctionTemplate::New(isolate, krom_init));
		krom->Set(String::NewFromUtf8(isolate, "log"), FunctionTemplate::New(isolate, LogCallback));
		krom->Set(String::NewFromUtf8(isolate, "clear"), FunctionTemplate::New(isolate, graphics_clear));
		krom->Set(String::NewFromUtf8(isolate, "setCallback"), FunctionTemplate::New(isolate, krom_set_callback));
		krom->Set(String::NewFromUtf8(isolate, "setDropFilesCallback"), FunctionTemplate::New(isolate, krom_set_drop_files_callback));
		krom->Set(String::NewFromUtf8(isolate, "setKeyboardDownCallback"), FunctionTemplate::New(isolate, krom_set_keyboard_down_callback));
		krom->Set(String::NewFromUtf8(isolate, "setKeyboardUpCallback"), FunctionTemplate::New(isolate, krom_set_keyboard_up_callback));
		krom->Set(String::NewFromUtf8(isolate, "setKeyboardPressCallback"), FunctionTemplate::New(isolate, krom_set_keyboard_press_callback));
		krom->Set(String::NewFromUtf8(isolate, "setMouseDownCallback"), FunctionTemplate::New(isolate, krom_set_mouse_down_callback));
		krom->Set(String::NewFromUtf8(isolate, "setMouseUpCallback"), FunctionTemplate::New(isolate, krom_set_mouse_up_callback));
		krom->Set(String::NewFromUtf8(isolate, "setMouseMoveCallback"), FunctionTemplate::New(isolate, krom_set_mouse_move_callback));
		krom->Set(String::NewFromUtf8(isolate, "setMouseWheelCallback"), FunctionTemplate::New(isolate, krom_set_mouse_wheel_callback));
		krom->Set(String::NewFromUtf8(isolate, "setGamepadAxisCallback"), FunctionTemplate::New(isolate, krom_set_gamepad_axis_callback));
		krom->Set(String::NewFromUtf8(isolate, "setGamepadButtonCallback"), FunctionTemplate::New(isolate, krom_set_gamepad_button_callback));
		krom->Set(String::NewFromUtf8(isolate, "lockMouse"), FunctionTemplate::New(isolate, krom_lock_mouse));
		krom->Set(String::NewFromUtf8(isolate, "unlockMouse"), FunctionTemplate::New(isolate, krom_unlock_mouse));
		krom->Set(String::NewFromUtf8(isolate, "canLockMouse"), FunctionTemplate::New(isolate, krom_can_lock_mouse));
		krom->Set(String::NewFromUtf8(isolate, "isMouseLocked"), FunctionTemplate::New(isolate, krom_is_mouse_locked));
		krom->Set(String::NewFromUtf8(isolate, "createIndexBuffer"), FunctionTemplate::New(isolate, krom_create_indexbuffer));
		krom->Set(String::NewFromUtf8(isolate, "deleteIndexBuffer"), FunctionTemplate::New(isolate, krom_delete_indexbuffer));
		krom->Set(String::NewFromUtf8(isolate, "setIndices"), FunctionTemplate::New(isolate, krom_set_indices));
		krom->Set(String::NewFromUtf8(isolate, "setIndexBuffer"), FunctionTemplate::New(isolate, krom_set_indexbuffer));
		krom->Set(String::NewFromUtf8(isolate, "createVertexBuffer"), FunctionTemplate::New(isolate, krom_create_vertexbuffer));
		krom->Set(String::NewFromUtf8(isolate, "deleteVertexBuffer"), FunctionTemplate::New(isolate, krom_delete_vertexbuffer));
		krom->Set(String::NewFromUtf8(isolate, "setVertices"), FunctionTemplate::New(isolate, krom_set_vertices));
		krom->Set(String::NewFromUtf8(isolate, "setVertexBuffer"), FunctionTemplate::New(isolate, krom_set_vertexbuffer));
		krom->Set(String::NewFromUtf8(isolate, "setVertexBuffers"), FunctionTemplate::New(isolate, krom_set_vertexbuffers));
		krom->Set(String::NewFromUtf8(isolate, "drawIndexedVertices"), FunctionTemplate::New(isolate, krom_draw_indexed_vertices));
		krom->Set(String::NewFromUtf8(isolate, "drawIndexedVerticesInstanced"), FunctionTemplate::New(isolate, krom_draw_indexed_vertices_instanced));
		krom->Set(String::NewFromUtf8(isolate, "createVertexShader"), FunctionTemplate::New(isolate, krom_create_vertex_shader));
		krom->Set(String::NewFromUtf8(isolate, "createVertexShaderFromSource"), FunctionTemplate::New(isolate, krom_create_vertex_shader_from_source));
		krom->Set(String::NewFromUtf8(isolate, "createFragmentShader"), FunctionTemplate::New(isolate, krom_create_fragment_shader));
		krom->Set(String::NewFromUtf8(isolate, "createFragmentShaderFromSource"), FunctionTemplate::New(isolate, krom_create_fragment_shader_from_source));
		krom->Set(String::NewFromUtf8(isolate, "createGeometryShader"), FunctionTemplate::New(isolate, krom_create_geometry_shader));
		krom->Set(String::NewFromUtf8(isolate, "createTessellationControlShader"), FunctionTemplate::New(isolate, krom_create_tessellation_control_shader));
		krom->Set(String::NewFromUtf8(isolate, "createTessellationEvaluationShader"), FunctionTemplate::New(isolate, krom_create_tessellation_evaluation_shader));
		krom->Set(String::NewFromUtf8(isolate, "deleteShader"), FunctionTemplate::New(isolate, krom_delete_shader));
		krom->Set(String::NewFromUtf8(isolate, "createPipeline"), FunctionTemplate::New(isolate, krom_create_pipeline));
		krom->Set(String::NewFromUtf8(isolate, "deletePipeline"), FunctionTemplate::New(isolate, krom_delete_pipeline));
		krom->Set(String::NewFromUtf8(isolate, "compilePipeline"), FunctionTemplate::New(isolate, krom_compile_pipeline));
		krom->Set(String::NewFromUtf8(isolate, "setPipeline"), FunctionTemplate::New(isolate, krom_set_pipeline));
		krom->Set(String::NewFromUtf8(isolate, "loadImage"), FunctionTemplate::New(isolate, krom_load_image));
		krom->Set(String::NewFromUtf8(isolate, "unloadImage"), FunctionTemplate::New(isolate, krom_unload_image));
		krom->Set(String::NewFromUtf8(isolate, "loadSound"), FunctionTemplate::New(isolate, krom_load_sound));
		krom->Set(String::NewFromUtf8(isolate, "setAudioCallback"), FunctionTemplate::New(isolate, krom_set_audio_callback));
		krom->Set(String::NewFromUtf8(isolate, "audioThread"), FunctionTemplate::New(isolate, audio_thread));
		krom->Set(String::NewFromUtf8(isolate, "writeAudioBuffer"), FunctionTemplate::New(isolate, write_audio_buffer));
		krom->Set(String::NewFromUtf8(isolate, "loadBlob"), FunctionTemplate::New(isolate, krom_load_blob));
		krom->Set(String::NewFromUtf8(isolate, "getConstantLocation"), FunctionTemplate::New(isolate, krom_get_constant_location));
		krom->Set(String::NewFromUtf8(isolate, "getTextureUnit"), FunctionTemplate::New(isolate, krom_get_texture_unit));
		krom->Set(String::NewFromUtf8(isolate, "setTexture"), FunctionTemplate::New(isolate, krom_set_texture));
		krom->Set(String::NewFromUtf8(isolate, "setTextureDepth"), FunctionTemplate::New(isolate, krom_set_texture_depth));
		krom->Set(String::NewFromUtf8(isolate, "setImageTexture"), FunctionTemplate::New(isolate, krom_set_image_texture));
		krom->Set(String::NewFromUtf8(isolate, "setTextureParameters"), FunctionTemplate::New(isolate, krom_set_texture_parameters));
		krom->Set(String::NewFromUtf8(isolate, "setTexture3DParameters"), FunctionTemplate::New(isolate, krom_set_texture_3d_parameters));
		krom->Set(String::NewFromUtf8(isolate, "setBool"), FunctionTemplate::New(isolate, krom_set_bool));
		krom->Set(String::NewFromUtf8(isolate, "setInt"), FunctionTemplate::New(isolate, krom_set_int));
		krom->Set(String::NewFromUtf8(isolate, "setFloat"), FunctionTemplate::New(isolate, krom_set_float));
		krom->Set(String::NewFromUtf8(isolate, "setFloat2"), FunctionTemplate::New(isolate, krom_set_float2));
		krom->Set(String::NewFromUtf8(isolate, "setFloat3"), FunctionTemplate::New(isolate, krom_set_float3));
		krom->Set(String::NewFromUtf8(isolate, "setFloat4"), FunctionTemplate::New(isolate, krom_set_float4));
		krom->Set(String::NewFromUtf8(isolate, "setFloats"), FunctionTemplate::New(isolate, krom_set_floats));
		krom->Set(String::NewFromUtf8(isolate, "setMatrix"), FunctionTemplate::New(isolate, krom_set_matrix));
		krom->Set(String::NewFromUtf8(isolate, "setMatrix3"), FunctionTemplate::New(isolate, krom_set_matrix3));
		krom->Set(String::NewFromUtf8(isolate, "getTime"), FunctionTemplate::New(isolate, krom_get_time));
		krom->Set(String::NewFromUtf8(isolate, "windowWidth"), FunctionTemplate::New(isolate, krom_window_width));
		krom->Set(String::NewFromUtf8(isolate, "windowHeight"), FunctionTemplate::New(isolate, krom_window_height));
		krom->Set(String::NewFromUtf8(isolate, "screenDpi"), FunctionTemplate::New(isolate, krom_screen_dpi));
		krom->Set(String::NewFromUtf8(isolate, "systemId"), FunctionTemplate::New(isolate, krom_system_id));
		krom->Set(String::NewFromUtf8(isolate, "requestShutdown"), FunctionTemplate::New(isolate, krom_request_shutdown));
		krom->Set(String::NewFromUtf8(isolate, "displayCount"), FunctionTemplate::New(isolate, krom_display_count));
		krom->Set(String::NewFromUtf8(isolate, "displayWidth"), FunctionTemplate::New(isolate, krom_display_width));
		krom->Set(String::NewFromUtf8(isolate, "displayHeight"), FunctionTemplate::New(isolate, krom_display_height));
		krom->Set(String::NewFromUtf8(isolate, "displayX"), FunctionTemplate::New(isolate, krom_display_x));
		krom->Set(String::NewFromUtf8(isolate, "displayY"), FunctionTemplate::New(isolate, krom_display_y));
		krom->Set(String::NewFromUtf8(isolate, "displayIsPrimary"), FunctionTemplate::New(isolate, krom_display_is_primary));
		krom->Set(String::NewFromUtf8(isolate, "writeStorage"), FunctionTemplate::New(isolate, krom_write_storage));
		krom->Set(String::NewFromUtf8(isolate, "readStorage"), FunctionTemplate::New(isolate, krom_read_storage));
		krom->Set(String::NewFromUtf8(isolate, "createRenderTarget"), FunctionTemplate::New(isolate, krom_create_render_target));
		krom->Set(String::NewFromUtf8(isolate, "createRenderTargetCubeMap"), FunctionTemplate::New(isolate, krom_create_render_target_cube_map));
		krom->Set(String::NewFromUtf8(isolate, "createTexture"), FunctionTemplate::New(isolate, krom_create_texture));
		krom->Set(String::NewFromUtf8(isolate, "createTexture3D"), FunctionTemplate::New(isolate, krom_create_texture_3d));
		krom->Set(String::NewFromUtf8(isolate, "createTextureFromBytes"), FunctionTemplate::New(isolate, krom_create_texture_from_bytes));
		krom->Set(String::NewFromUtf8(isolate, "createTextureFromBytes3D"), FunctionTemplate::New(isolate, krom_create_texture_from_bytes_3d));
		krom->Set(String::NewFromUtf8(isolate, "getRenderTargetPixels"), FunctionTemplate::New(isolate, krom_get_render_target_pixels));
		krom->Set(String::NewFromUtf8(isolate, "unlockTexture"), FunctionTemplate::New(isolate, krom_unlock_texture));
		krom->Set(String::NewFromUtf8(isolate, "clearTexture"), FunctionTemplate::New(isolate, krom_clear_texture));
		krom->Set(String::NewFromUtf8(isolate, "generateTextureMipmaps"), FunctionTemplate::New(isolate, krom_generate_texture_mipmaps));
		krom->Set(String::NewFromUtf8(isolate, "generateRenderTargetMipmaps"), FunctionTemplate::New(isolate, krom_generate_render_target_mipmaps));
		krom->Set(String::NewFromUtf8(isolate, "setMipmaps"), FunctionTemplate::New(isolate, krom_set_mipmaps));
		krom->Set(String::NewFromUtf8(isolate, "setDepthStencilFrom"), FunctionTemplate::New(isolate, krom_set_depth_stencil_from));
		krom->Set(String::NewFromUtf8(isolate, "viewport"), FunctionTemplate::New(isolate, krom_viewport));
		krom->Set(String::NewFromUtf8(isolate, "scissor"), FunctionTemplate::New(isolate, krom_scissor));
		krom->Set(String::NewFromUtf8(isolate, "disableScissor"), FunctionTemplate::New(isolate, krom_disable_scissor));
		krom->Set(String::NewFromUtf8(isolate, "renderTargetsInvertedY"), FunctionTemplate::New(isolate, krom_render_targets_inverted_y));
		krom->Set(String::NewFromUtf8(isolate, "begin"), FunctionTemplate::New(isolate, krom_begin));
		krom->Set(String::NewFromUtf8(isolate, "beginFace"), FunctionTemplate::New(isolate, krom_begin_face));
		krom->Set(String::NewFromUtf8(isolate, "end"), FunctionTemplate::New(isolate, krom_end));
		krom->Set(String::NewFromUtf8(isolate, "fileSaveBytes"), FunctionTemplate::New(isolate, krom_file_save_bytes));
		krom->Set(String::NewFromUtf8(isolate, "sysCommand"), FunctionTemplate::New(isolate, krom_sys_command));
		krom->Set(String::NewFromUtf8(isolate, "savePath"), FunctionTemplate::New(isolate, krom_save_path));

		Local<ObjectTemplate> global = ObjectTemplate::New(isolate);
		global->Set(String::NewFromUtf8(isolate, "Krom"), krom);

		Local<Context> context = Context::New(isolate, NULL, global);
		globalContext.Reset(isolate, context);
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
		updateFunction.Reset();
		globalContext.Reset();
		isolate->Dispose();
		V8::Dispose();
		V8::ShutdownPlatform();
		delete plat;
	}

	void initAudioBuffer() {
		for (int i = 0; i < Kore::Audio2::buffer.dataSize; i++) {
			*(float*)&Kore::Audio2::buffer.data[i] = 0;
		}
	}

	void updateAudio(int samples) {
		v8::Locker locker{isolate};

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
		}
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
		v8::Locker locker{isolate};

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
		}
	}

	void keyDown(Kore::KeyCode code) {
		v8::Locker locker{isolate};

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
		}
	}

	void keyUp(Kore::KeyCode code) {
		v8::Locker locker{isolate};

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
		}
	}

    void keyPress(wchar_t character) {
        v8::Locker locker{isolate};

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
        }
    }

	void mouseMove(int window, int x, int y, int mx, int my) {
		v8::Locker locker{isolate};

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
		}
	}

	void mouseDown(int window, int button, int x, int y) {
		v8::Locker locker{isolate};

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
		}
	}

	void mouseUp(int window, int button, int x, int y) {
		v8::Locker locker{isolate};

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
		}
	}

	void mouseWheel(int window, int delta) {
		v8::Locker locker{isolate};

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
		}
	}

	void gamepadAxis(int gamepad, int axis, float value) {
		v8::Locker locker{isolate};

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
		}
	}

	void gamepadButton(int gamepad, int button, float value) {
		v8::Locker locker{isolate};

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
		}
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

	struct Function {
		std::string name;
		std::vector<std::string> parameters;
		std::string body;
	};

	struct Klass {
		std::string name;
		std::string internal_name;
		std::map<std::string, Function*> methods;
		std::map<std::string, Function*> functions;
	};

	std::map<std::string, Klass*> classes;

	enum ParseMode {
		ParseRegular,
		ParseMethods,
		ParseMethod,
		ParseFunction
	};

	void parseCode() {
		int types = 0;
		ParseMode mode = ParseRegular;
		Klass* currentClass = nullptr;
		Function* currentFunction = nullptr;
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
							currentFunction = new Function;
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
							currentFunction = new Function;
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
	}
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
	std::string bindir(argv[0]);
#ifdef KORE_WINDOWS
	bindir = bindir.substr(0, bindir.find_last_of("\\"));
#else
	bindir = bindir.substr(0, bindir.find_last_of("/"));
#endif
	assetsdir = argc > 1 ? argv[1] : bindir;
	shadersdir = argc > 2 ? argv[2] : bindir;

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
			watch = true;
		}
		else if (strcmp(argv[i], "--nosound") == 0) {
			nosound = true;
		}
		else if (strcmp(argv[i], "--nowindow") == 0) {
			nowindow = true;
		}
	}

	kromjs = assetsdir + "/krom.js";

	Kore::setFilesLocation(&assetsdir[0u]);
	Kore::System::setName("Krom");
	Kore::System::setup();

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
		startDebugger(isolate, port);
		while (!tickDebugger()) {}
		//Sleep(1000);
	}

	startKrom(code);
	Kore::System::start();

	exit(0); // TODO

	endV8();

	return 0;
}
