// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void runv8(char* script) {
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
	
	Platform* platform = platform::CreateDefaultPlatform();
	V8::InitializePlatform(platform);
	V8::Initialize();

	ArrayBufferAllocator allocator;
	Isolate::CreateParams create_params;
	create_params.array_buffer_allocator = &allocator;
	Isolate* isolate = Isolate::New(create_params);
	{
		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		Local<Context> context = Context::New(isolate);
		Context::Scope context_scope(context);
		Local<String> source = String::NewFromUtf8(isolate, script, //"'Hello' + ', World!'",
                            NewStringType::kNormal).ToLocalChecked();
		Local<Script> script = Script::Compile(context, source).ToLocalChecked();
		Local<Value> result = script->Run(context).ToLocalChecked();
		String::Utf8Value utf8(result);
		printf("%s\n", *utf8);
	}
	isolate->Dispose();
	V8::Dispose();
	V8::ShutdownPlatform();
	delete platform;
}

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

namespace {
	void update() {
		
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
	
	runv8((char*)reader.readAll());
	
	reader.close();
	
	Kore::System::start();
	
	return 0;
}
