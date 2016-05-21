#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pch.h"
#include <Kore/IO/FileReader.h>
#include <Kore/Graphics/Graphics.h>
#include <Kore/Graphics/Shader.h>
#include <Kore/Input/Keyboard.h>
#include <Kore/Input/Mouse.h>
#include <Kore/Audio/Audio.h>
#include <Kore/Audio/Mixer.h>
#include <Kore/Audio/Sound.h>
#include <Kore/Audio/SoundStream.h>
#include <Kore/Math/Random.h>
#include <Kore/System.h>
#include <stdio.h>

#include "../V8/include/libplatform/libplatform.h"
#include "../V8/include/v8.h"

using namespace v8;

class ArrayBufferAllocator : public v8::ArrayBuffer::Allocator {
 public:
  virtual void* Allocate(size_t length) {
    void* data = AllocateUninitialized(length);
    return data == NULL ? data : memset(data, 0, length);
  }
  virtual void* AllocateUninitialized(size_t length) { return malloc(length); }
  virtual void Free(void* data, size_t) { free(data); }
};

#ifdef SYS_OSX
const char* macgetresourcepath();
#endif

namespace {
	Platform* plat;
	Isolate* isolate;
	Global<Context> globalContext;
	Global<Function> updateFunction;

	void LogCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
		if (args.Length() < 1) return;
		HandleScope scope(args.GetIsolate());
		Local<Value> arg = args[0];
		String::Utf8Value value(arg);
		printf("%s\n", *value);
	}
	
	void graphics_clear(const v8::FunctionCallbackInfo<v8::Value>& args) {
		Kore::Graphics::clear(Kore::Graphics::ClearColorFlag);
	}

	static bool startV8(char* scriptfile) {
		V8::InitializeICU();
	
#ifdef SYS_OSX
		char filepath[256];
		strcpy(filepath, macgetresourcepath());
		strcat(filepath, "/");
		strcat(filepath, "Deployment");
		strcat(filepath, "/");
		V8::InitializeExternalStartupData(filepath);
#else
		V8::InitializeExternalStartupData("./");
#endif
	
		plat = platform::CreateDefaultPlatform();
		V8::InitializePlatform(plat);
		V8::Initialize();

		ArrayBufferAllocator allocator;
		Isolate::CreateParams create_params;
		create_params.array_buffer_allocator = &allocator;
		isolate = Isolate::New(create_params);
	
		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
	
		Local<ObjectTemplate> global = ObjectTemplate::New(isolate);
		global->Set(String::NewFromUtf8(isolate, "log"), FunctionTemplate::New(isolate, LogCallback));
		global->Set(String::NewFromUtf8(isolate, "clear"), FunctionTemplate::New(isolate, graphics_clear));
		
		Local<Context> context = Context::New(isolate, NULL, global);
		globalContext.Reset(isolate, context);
	
		Context::Scope context_scope(context);
		Local<String> source = String::NewFromUtf8(isolate, scriptfile, NewStringType::kNormal).ToLocalChecked();
		Local<Script> script = Script::Compile(context, source).ToLocalChecked();
		script->Run(context).ToLocalChecked();
	
		Local<String> update_name = String::NewFromUtf8(isolate, "update", NewStringType::kNormal).ToLocalChecked();
		Local<Value> update_val;
		if (!context->Global()->Get(context, update_name).ToLocal(&update_val) || !update_val->IsFunction()) {
			return false;
		}
		updateFunction.Reset(isolate, Local<Function>::Cast(update_val));
	
		return true;
	}

	void runV8() {
		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, globalContext);
		Context::Scope context_scope(context);
		v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, updateFunction);
		func->Call(context, context->Global(), 0, NULL);
	}

	void endV8() {
		updateFunction.Reset();
		globalContext.Reset();
		isolate->Dispose();
		V8::Dispose();
		V8::ShutdownPlatform();
		delete plat;
	}

	void update() {
		Kore::Graphics::begin();
		runV8();
		Kore::Graphics::end();
		Kore::Graphics::swapBuffers();
	}
}

int kore(int argc, char** argv) {
	int w = 1024;
	int h = 768;
	
	Kore::System::setName("Krom");
	Kore::System::setup();
	Kore::WindowOptions options;
	options.title = "Krom";
	options.width = w;
	options.height = h;
	options.x = 100;
	options.y = 100;
	options.targetDisplay = 0;
	options.mode = Kore::WindowModeWindow;
	options.rendererOptions.depthBufferBits = 16;
	options.rendererOptions.stencilBufferBits = 8;
	options.rendererOptions.textureFormat = 0;
	options.rendererOptions.antialiasing = 0;
	Kore::System::initWindow(options);
	
	//Mixer::init();
	//Audio::init();
	Kore::Random::init(Kore::System::time() * 1000);
	
	Kore::System::setCallback(update);
	
	//Kore::Keyboard::the()->KeyDown = keyDown;
	//Kore::Keyboard::the()->KeyUp = keyUp;
	//Kore::Mouse::the()->Release = mouseUp;
	
	//Mixer::play(music);
	
	Kore::FileReader reader;
	reader.open("krom.js");
	
	startV8((char*)reader.readAll());
	
	reader.close();
	
	Kore::System::start();
	
	endV8();
	
	return 0;
}
