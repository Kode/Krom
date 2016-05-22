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
	
	unsigned char g = 0;
	
	void graphics_clear(const v8::FunctionCallbackInfo<v8::Value>& args) {
		Kore::Graphics::clear(Kore::Graphics::ClearColorFlag, 0xff004400 | g);
		++g;
	}
	
	void krom_set_callback(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<Value> arg = args[0];
		Local<Function> func = Local<Function>::Cast(arg);
		updateFunction.Reset(isolate, func);
	}
	
	void krom_create_indexbuffer(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());

		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);
		
		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, new Kore::IndexBuffer(args[0]->Int32Value())));
		args.GetReturnValue().Set(obj);
	}
	
	void krom_set_indices(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		
		Local<External> field = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::IndexBuffer* buffer = (Kore::IndexBuffer*)field->Value();
		
		Local<Object> array = args[1]->ToObject();
		
		int* indices = buffer->lock();
		for (int32_t i = 0; i < buffer->count(); ++i) {
			indices[i] = array->Get(i)->ToInt32()->Value();
		}
		buffer->unlock();
	}
	
	void krom_set_indexbuffer(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> field = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::IndexBuffer* buffer = (Kore::IndexBuffer*)field->Value();
		Kore::Graphics::setIndexBuffer(*buffer);
	}
	
	Kore::VertexData convertVertexData(int num) {
		switch (num) {
			case 0:
				return Kore::Float1VertexData;
			case 1:
				return Kore::Float2VertexData;
			case 2:
				return Kore::Float3VertexData;
			case 3:
				return Kore::Float4VertexData;
			case 4:
				return Kore::Float4x4VertexData;
		}
		return Kore::Float1VertexData;
	}
	
	void krom_create_vertexbuffer(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		
		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);
		
		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();

		Local<Object> jsstructure = args[1]->ToObject();
		int32_t length = jsstructure->Get(String::NewFromUtf8(isolate, "length"))->ToInt32()->Value();
		Kore::VertexStructure structure;
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
		
		obj->SetInternalField(0, External::New(isolate, new Kore::VertexBuffer(args[0]->Int32Value(), structure)));
		args.GetReturnValue().Set(obj);
	}
	
	void krom_set_vertices(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		
		Local<External> field = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::VertexBuffer* buffer = (Kore::VertexBuffer*)field->Value();
		
		Local<Float32Array> f32array = Local<Float32Array>::Cast(args[1]);
		ArrayBuffer::Contents content = f32array->Buffer()->GetContents();

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
		Kore::VertexBuffer* buffer = (Kore::VertexBuffer*)field->Value();
		Kore::Graphics::setVertexBuffer(*buffer);
	}
	
	void krom_draw_indexed_vertices(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Kore::Graphics::drawIndexedVertices();
	}
	
	void krom_create_vertex_shader(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
		ArrayBuffer::Contents content = buffer->GetContents();
		Kore::Shader* shader = new Kore::Shader(content.Data(), (int)content.ByteLength(), Kore::VertexShader);
		
		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);
		
		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, shader));
		args.GetReturnValue().Set(obj);
	}
	
	void krom_create_fragment_shader(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
		ArrayBuffer::Contents content = buffer->GetContents();
		Kore::Shader* shader = new Kore::Shader(content.Data(), (int)content.ByteLength(), Kore::FragmentShader);
		
		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);
		
		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, shader));
		args.GetReturnValue().Set(obj);
	}
	
	void krom_create_program(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Kore::Program* program = new Kore::Program();
		
		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);
		
		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, program));
		args.GetReturnValue().Set(obj);
	}
	
	void krom_compile_program(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		
		Local<External> progfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Program* program = (Kore::Program*)progfield->Value();
		
		Local<Object> jsstructure = args[1]->ToObject();
		int32_t length = jsstructure->Get(String::NewFromUtf8(isolate, "length"))->ToInt32()->Value();
		Kore::VertexStructure structure;
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
		
		Local<External> vsfield = Local<External>::Cast(args[2]->ToObject()->GetInternalField(0));
		Kore::Shader* vertexShader = (Kore::Shader*)vsfield->Value();
		
		Local<External> fsfield = Local<External>::Cast(args[3]->ToObject()->GetInternalField(0));
		Kore::Shader* fragmentShader = (Kore::Shader*)fsfield->Value();
		
		program->setVertexShader(vertexShader);
		program->setFragmentShader(fragmentShader);
		program->link(structure);
	}
	
	void krom_set_program(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> progfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Program* program = (Kore::Program*)progfield->Value();
		program->set();
	}
	
	bool startV8(char* scriptfile) {
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
	
		Local<ObjectTemplate> krom = ObjectTemplate::New(isolate);
		krom->Set(String::NewFromUtf8(isolate, "log"), FunctionTemplate::New(isolate, LogCallback));
		krom->Set(String::NewFromUtf8(isolate, "clear"), FunctionTemplate::New(isolate, graphics_clear));
		krom->Set(String::NewFromUtf8(isolate, "setCallback"), FunctionTemplate::New(isolate, krom_set_callback));
		krom->Set(String::NewFromUtf8(isolate, "createIndexBuffer"), FunctionTemplate::New(isolate, krom_create_indexbuffer));
		krom->Set(String::NewFromUtf8(isolate, "setIndices"), FunctionTemplate::New(isolate, krom_set_indices));
		krom->Set(String::NewFromUtf8(isolate, "setIndexBuffer"), FunctionTemplate::New(isolate, krom_set_indexbuffer));
		krom->Set(String::NewFromUtf8(isolate, "createVertexBuffer"), FunctionTemplate::New(isolate, krom_create_vertexbuffer));
		krom->Set(String::NewFromUtf8(isolate, "setVertices"), FunctionTemplate::New(isolate, krom_set_vertices));
		krom->Set(String::NewFromUtf8(isolate, "setVertexBuffer"), FunctionTemplate::New(isolate, krom_set_vertexbuffer));
		krom->Set(String::NewFromUtf8(isolate, "drawIndexedVertices"), FunctionTemplate::New(isolate, krom_draw_indexed_vertices));
		krom->Set(String::NewFromUtf8(isolate, "createVertexShader"), FunctionTemplate::New(isolate, krom_create_vertex_shader));
		krom->Set(String::NewFromUtf8(isolate, "createFragmentShader"), FunctionTemplate::New(isolate, krom_create_fragment_shader));
		krom->Set(String::NewFromUtf8(isolate, "createProgram"), FunctionTemplate::New(isolate, krom_create_program));
		krom->Set(String::NewFromUtf8(isolate, "compileProgram"), FunctionTemplate::New(isolate, krom_compile_program));
		krom->Set(String::NewFromUtf8(isolate, "setProgram"), FunctionTemplate::New(isolate, krom_set_program));
		
		Local<ObjectTemplate> global = ObjectTemplate::New(isolate);
		global->Set(String::NewFromUtf8(isolate, "Krom"), krom);
		
		Local<Context> context = Context::New(isolate, NULL, global);
		globalContext.Reset(isolate, context);
	
		Context::Scope context_scope(context);
		Local<String> source = String::NewFromUtf8(isolate, scriptfile, NewStringType::kNormal).ToLocalChecked();
		Local<String> filename = String::NewFromUtf8(isolate, "krom.js", NewStringType::kNormal).ToLocalChecked();

		TryCatch try_catch(isolate);
		
		Local<Script> compiled_script = Script::Compile(source, filename);
		
		Local<Value> result;
		if (!compiled_script->Run(context).ToLocal(&result)) {
			v8::String::Utf8Value stack_trace(try_catch.StackTrace());
			printf("Trace: %s\n", *stack_trace);
			return false;
		}
	
		return true;
	}

	void runV8() {
		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, globalContext);
		Context::Scope context_scope(context);
		
		TryCatch try_catch(isolate);
		v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, updateFunction);
		Local<Value> result;
		if (!func->Call(context, context->Global(), 0, NULL).ToLocal(&result)) {
			v8::String::Utf8Value stack_trace(try_catch.StackTrace());
			printf("Trace: %s\n", *stack_trace);
		}
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
	
	bool started = startV8((char*)reader.readAll());
	
	reader.close();
	
	if (started) Kore::System::start();
	
	endV8();
	
	return 0;
}
