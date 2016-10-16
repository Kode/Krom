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
#include <Kore/Log.h>

#include "../V8/include/libplatform/libplatform.h"
#include "../V8/include/v8.h"

#include <stdio.h>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace v8;

#ifdef SYS_OSX
const char* macgetresourcepath();
#endif

namespace {
	Platform* plat;
	Isolate* isolate;
	Global<Context> globalContext;
	Global<Function> updateFunction;
	Global<Function> keyboardDownFunction;
	Global<Function> keyboardUpFunction;
	Global<Function> mouseDownFunction;
	Global<Function> mouseUpFunction;
	Global<Function> mouseMoveFunction;
	std::map<std::string, bool> imageChanges;
	std::map<std::string, bool> shaderChanges;
	std::map<std::string, std::string> shaderFileNames;

	void LogCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
		if (args.Length() < 1) return;
		HandleScope scope(args.GetIsolate());
		Local<Value> arg = args[0];
		String::Utf8Value value(arg);
		printf("%s\n", *value);
	}
	
	void graphics_clear(const v8::FunctionCallbackInfo<v8::Value>& args) {
		Kore::Graphics::clear(Kore::Graphics::ClearColorFlag, 0);
	}
	
	void krom_set_callback(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<Value> arg = args[0];
		Local<Function> func = Local<Function>::Cast(arg);
		updateFunction.Reset(isolate, func);
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
		Kore::VertexBuffer* buffer = (Kore::VertexBuffer*)field->Value();
		Kore::Graphics::setVertexBuffer(*buffer);
	}
	
	void krom_draw_indexed_vertices(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		int start = args[0]->ToInt32()->Value();
		int count = args[1]->ToInt32()->Value();
		if (count < 0) Kore::Graphics::drawIndexedVertices();
		else Kore::Graphics::drawIndexedVertices(start, count);
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
		ArrayBuffer::Contents content = buffer->Externalize();
		Kore::Shader* shader = new Kore::Shader(content.Data(), (int)content.ByteLength(), Kore::VertexShader);
		
		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);
		
		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, shader));
		obj->Set(String::NewFromUtf8(isolate, "name"), args[1]);
		args.GetReturnValue().Set(obj);
	}
	
	void krom_create_fragment_shader(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(args[0]);
		ArrayBuffer::Contents content = buffer->Externalize();
		Kore::Shader* shader = new Kore::Shader(content.Data(), (int)content.ByteLength(), Kore::FragmentShader);
		
		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);
		
		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, shader));
		obj->Set(String::NewFromUtf8(isolate, "name"), args[1]);
		args.GetReturnValue().Set(obj);
	}
	
	void krom_create_program(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Kore::Program* program = new Kore::Program();
		
		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(4);
		
		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, program));
		args.GetReturnValue().Set(obj);
	}
	
	void recompileProgram(Local<Object> projobj) {
		Local<External> structfield = Local<External>::Cast(projobj->GetInternalField(1));
		Kore::VertexStructure* structure = (Kore::VertexStructure*)structfield->Value();
		
		Local<External> vsfield = Local<External>::Cast(projobj->GetInternalField(2));
		Kore::Shader* vs = (Kore::Shader*)vsfield->Value();
		
		Local<External> fsfield = Local<External>::Cast(projobj->GetInternalField(3));
		Kore::Shader* fs = (Kore::Shader*)fsfield->Value();
		
		Kore::Program* program = new Kore::Program();
		program->setVertexShader(vs);
		program->setFragmentShader(fs);
		program->link(*structure);
		
		projobj->SetInternalField(0, External::New(isolate, program));
	}
	
	void krom_compile_program(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		
		Local<Object> progobj = args[0]->ToObject();
		
		Local<External> progfield = Local<External>::Cast(progobj->GetInternalField(0));
		Kore::Program* program = (Kore::Program*)progfield->Value();
		
		Local<Object> jsstructure = args[1]->ToObject();
		int32_t length = jsstructure->Get(String::NewFromUtf8(isolate, "length"))->ToInt32()->Value();
		Kore::VertexStructure* structure = new Kore::VertexStructure;
		for (int32_t i = 0; i < length; ++i) {
			Local<Object> element = jsstructure->Get(i)->ToObject();
			Local<Value> str = element->Get(String::NewFromUtf8(isolate, "name"));
			String::Utf8Value utf8_value(str);
			Local<Object> dataobj = element->Get(String::NewFromUtf8(isolate, "data"))->ToObject();
			int32_t data = dataobj->Get(1)->ToInt32()->Value();
			char* name = new char[32]; // TODO
			strcpy(name, *utf8_value);
			structure->add(name, convertVertexData(data));
		}
		
		progobj->SetInternalField(1, External::New(isolate, structure));
		
		Local<External> vsfield = Local<External>::Cast(args[2]->ToObject()->GetInternalField(0));
		Kore::Shader* vertexShader = (Kore::Shader*)vsfield->Value();
		progobj->SetInternalField(2, External::New(isolate, vertexShader));
		progobj->Set(String::NewFromUtf8(isolate, "vsname"), args[2]->ToObject()->Get(String::NewFromUtf8(isolate, "name")));
		
		Local<External> fsfield = Local<External>::Cast(args[3]->ToObject()->GetInternalField(0));
		Kore::Shader* fragmentShader = (Kore::Shader*)fsfield->Value();
		progobj->SetInternalField(3, External::New(isolate, fragmentShader));
		progobj->Set(String::NewFromUtf8(isolate, "fsname"), args[3]->ToObject()->Get(String::NewFromUtf8(isolate, "name")));
		
		program->setVertexShader(vertexShader);
		program->setFragmentShader(fragmentShader);
		program->link(*structure);
	}
	
	void krom_set_program(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<Object> progobj = args[0]->ToObject();
		Local<External> progfield = Local<External>::Cast(progobj->GetInternalField(0));
		Kore::Program* program = (Kore::Program*)progfield->Value();
		
		Local<Value> vsnameobj = progobj->Get(String::NewFromUtf8(isolate, "vsname"));
		String::Utf8Value vsname(vsnameobj);
		
		Local<Value> fsnameobj = progobj->Get(String::NewFromUtf8(isolate, "fsname"));
		String::Utf8Value fsname(fsnameobj);
		
		bool shaderChanged = false;
		
		if (shaderChanges[*vsname]) {
			shaderChanged = true;
			printf("Reloading shader %s.\n", *vsname);
			std::string filename = shaderFileNames[*vsname];
			std::ifstream input(filename.c_str(), std::ios::binary );
			std::vector<char> buffer((std::istreambuf_iterator<char>(input)), (std::istreambuf_iterator<char>()));
			Kore::Shader* vertexShader = new Kore::Shader(buffer.data(), (int)buffer.size(), Kore::VertexShader);
			progobj->SetInternalField(2, External::New(isolate, vertexShader));
			shaderChanges[*vsname] = false;
		}
		
		if (shaderChanges[*fsname]) {
			shaderChanged = true;
			printf("Reloading shader %s.\n", *fsname);
			std::string filename = shaderFileNames[*fsname];
			std::ifstream input(filename.c_str(), std::ios::binary );
			std::vector<char> buffer((std::istreambuf_iterator<char>(input)), (std::istreambuf_iterator<char>()));
			Kore::Shader* fragmentShader = new Kore::Shader(buffer.data(), (int)buffer.size(), Kore::FragmentShader);
			progobj->SetInternalField(3, External::New(isolate, fragmentShader));
			shaderChanges[*fsname] = false;
		}
		
		if (shaderChanged) {
			recompileProgram(progobj);
			Local<External> progfield = Local<External>::Cast(progobj->GetInternalField(0));
			program = (Kore::Program*)progfield->Value();
		}
		
		program->set();
	}
	
	void krom_load_image(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		String::Utf8Value utf8_value(args[0]);
		Kore::Texture* texture = new Kore::Texture(*utf8_value);
		
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
	
	void krom_load_sound(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		
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
		Kore::Program* program = (Kore::Program*)progfield->Value();
		
		String::Utf8Value utf8_value(args[1]);
		Kore::ConstantLocation location = program->getConstantLocation(*utf8_value);
		
		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);
		
		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, new Kore::ConstantLocation(location)));
		args.GetReturnValue().Set(obj);
	}
	
	void krom_get_texture_unit(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> progfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::Program* program = (Kore::Program*)progfield->Value();
		
		String::Utf8Value utf8_value(args[1]);
		Kore::TextureUnit unit = program->getTextureUnit(*utf8_value);
		
		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);
		
		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, new Kore::TextureUnit(unit)));
		args.GetReturnValue().Set(obj);
	}
	
	void krom_set_texture(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> unitfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::TextureUnit* unit = (Kore::TextureUnit*)unitfield->Value();
		
		if (args[1]->IsNull() || args[1]->IsUndefined()) return;

		Local<Object> image = args[1]->ToObject();
		Local<Value> tex = image->Get(String::NewFromUtf8(isolate, "texture_"));
		Local<Value> rt = image->Get(String::NewFromUtf8(isolate, "renderTarget_"));

		if (tex->IsObject()) {
			Kore::Texture* texture;
			String::Utf8Value filename(tex->ToObject()->Get(String::NewFromUtf8(isolate, "filename")));
			if (imageChanges[*filename]) {
				imageChanges[*filename] = false;
				Kore::log(Kore::Info, "Image %s changed.", *filename);
				texture = new Kore::Texture(*filename);
				tex->ToObject()->SetInternalField(0, External::New(isolate, texture));
			}
			else {
				Local<External> texfield = Local<External>::Cast(tex->ToObject()->GetInternalField(0));
				texture = (Kore::Texture*)texfield->Value();
			}
			Kore::Graphics::setTexture(*unit, texture);
		}
		else if (rt->IsObject()) {
			Local<External> rtfield = Local<External>::Cast(rt->ToObject()->GetInternalField(0));
			Kore::RenderTarget* renderTarget = (Kore::RenderTarget*)rtfield->Value();
			renderTarget->useColorAsTexture(*unit);
		}
	}

	Kore::TextureAddressing convertTextureAddressing(int index) {
		switch (index) {
		default:
		case 0: // Repeat
			return Kore::Repeat;
		case 1: // Mirror
			return Kore::Mirror;
		case 2: // Clamp
			return Kore::Clamp;
		}
	}

	Kore::TextureFilter convertTextureFilter(int index) {
		switch (index) {
		default:
		case 0: // PointFilter
			return Kore::PointFilter;
		case 1: // LinearFilter
			return Kore::LinearFilter;
		case 2: // AnisotropicFilter
			return Kore::AnisotropicFilter;
		}
	}

	Kore::MipmapFilter convertMipmapFilter(int index) {
		switch (index) {
		default:
		case 0: // NoMipFilter
			return Kore::NoMipFilter;
		case 1: // PointMipFilter
			return Kore::PointMipFilter;
		case 2: // LinearMipFilter
			return Kore::LinearMipFilter;
		}
	}
	
	void krom_set_texture_parameters(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> unitfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::TextureUnit* unit = (Kore::TextureUnit*)unitfield->Value();
		Kore::Graphics::setTextureAddressing(*unit, Kore::U, convertTextureAddressing(args[1]->ToInt32()->Int32Value()));
		Kore::Graphics::setTextureAddressing(*unit, Kore::V, convertTextureAddressing(args[2]->ToInt32()->Int32Value()));
		Kore::Graphics::setTextureMinificationFilter(*unit, convertTextureFilter(args[3]->ToInt32()->Int32Value()));
		Kore::Graphics::setTextureMagnificationFilter(*unit, convertTextureFilter(args[4]->ToInt32()->Int32Value()));
		Kore::Graphics::setTextureMipmapFilter(*unit, convertMipmapFilter(args[5]->ToInt32()->Int32Value()));
	}

	void krom_set_bool(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::ConstantLocation* location = (Kore::ConstantLocation*)locationfield->Value();
		int32_t value = args[1]->ToInt32()->Value();
		Kore::Graphics::setBool(*location, value != 0);
	}
	
	void krom_set_int(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::ConstantLocation* location = (Kore::ConstantLocation*)locationfield->Value();
		int32_t value = args[1]->ToInt32()->Value();
		Kore::Graphics::setInt(*location, value);
	}
	
	void krom_set_float(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::ConstantLocation* location = (Kore::ConstantLocation*)locationfield->Value();
		float value = (float)args[1]->ToNumber()->Value();
		Kore::Graphics::setFloat(*location, value);
	}
	
	void krom_set_float2(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::ConstantLocation* location = (Kore::ConstantLocation*)locationfield->Value();
		float value1 = (float)args[1]->ToNumber()->Value();
		float value2 = (float)args[2]->ToNumber()->Value();
		Kore::Graphics::setFloat2(*location, value1, value2);
	}
	
	void krom_set_float3(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::ConstantLocation* location = (Kore::ConstantLocation*)locationfield->Value();
		float value1 = (float)args[1]->ToNumber()->Value();
		float value2 = (float)args[2]->ToNumber()->Value();
		float value3 = (float)args[3]->ToNumber()->Value();
		Kore::Graphics::setFloat3(*location, value1, value2, value3);
	}
	
	void krom_set_float4(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::ConstantLocation* location = (Kore::ConstantLocation*)locationfield->Value();
		float value1 = (float)args[1]->ToNumber()->Value();
		float value2 = (float)args[2]->ToNumber()->Value();
		float value3 = (float)args[3]->ToNumber()->Value();
		float value4 = (float)args[4]->ToNumber()->Value();
		Kore::Graphics::setFloat4(*location, value1, value2, value3, value4);
	}
	
	void krom_set_matrix(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Local<External> locationfield = Local<External>::Cast(args[0]->ToObject()->GetInternalField(0));
		Kore::ConstantLocation* location = (Kore::ConstantLocation*)locationfield->Value();
		
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
		
		Kore::Graphics::setMatrix(*location, m);
	}
	
	void krom_get_time(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		args.GetReturnValue().Set(Number::New(isolate, Kore::System::time()));
	}
	
	void krom_create_render_target(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		Kore::RenderTarget* renderTarget = new Kore::RenderTarget(args[0]->ToInt32()->Value(), args[1]->ToInt32()->Value(), 0);
		
		Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
		templ->SetInternalFieldCount(1);
		
		Local<Object> obj = templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
		obj->SetInternalField(0, External::New(isolate, renderTarget));
		obj->Set(String::NewFromUtf8(isolate, "width"), Int32::New(isolate, renderTarget->width));
		obj->Set(String::NewFromUtf8(isolate, "height"), Int32::New(isolate, renderTarget->height));
		args.GetReturnValue().Set(obj);
	}
	
	void krom_viewport(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());

		int x = args[0]->ToInt32()->Int32Value();
		int y = args[1]->ToInt32()->Int32Value();
		int w = args[2]->ToInt32()->Int32Value();
		int h = args[3]->ToInt32()->Int32Value();

		Kore::Graphics::viewport(x, y, w, h);
	}

	void krom_set_depth_mode(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());

		bool write = args[0]->ToBoolean()->Value();
		int mode = args[1]->ToInt32()->Int32Value();

		switch (mode) {
		case 0:
			write ? Kore::Graphics::setRenderState(Kore::DepthTest, true) : Kore::Graphics::setRenderState(Kore::DepthTest, false);
			Kore::Graphics::setRenderState(Kore::DepthTestCompare, Kore::ZCompareAlways);
			break;
		case 1:
			Kore::Graphics::setRenderState(Kore::DepthTest, true);
			Kore::Graphics::setRenderState(Kore::DepthTestCompare, Kore::ZCompareNever);
			break;
		case 2:
			Kore::Graphics::setRenderState(Kore::DepthTest, true);
			Kore::Graphics::setRenderState(Kore::DepthTestCompare, Kore::ZCompareEqual);
			break;
		case 3:
			Kore::Graphics::setRenderState(Kore::DepthTest, true);
			Kore::Graphics::setRenderState(Kore::DepthTestCompare, Kore::ZCompareNotEqual);
			break;
		case 4:
			Kore::Graphics::setRenderState(Kore::DepthTest, true);
			Kore::Graphics::setRenderState(Kore::DepthTestCompare, Kore::ZCompareLess);
			break;
		case 5:
			Kore::Graphics::setRenderState(Kore::DepthTest, true);
			Kore::Graphics::setRenderState(Kore::DepthTestCompare, Kore::ZCompareLessEqual);
			break;
		case 6:
			Kore::Graphics::setRenderState(Kore::DepthTest, true);
			Kore::Graphics::setRenderState(Kore::DepthTestCompare, Kore::ZCompareGreater);
			break;
		case 7:
			Kore::Graphics::setRenderState(Kore::DepthTest, true);
			Kore::Graphics::setRenderState(Kore::DepthTestCompare, Kore::ZCompareGreaterEqual);
			break;
		}
		Kore::Graphics::setRenderState(Kore::DepthWrite, write);
	}

	void krom_render_targets_inverted_y(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		
#ifdef OPENGL
		args.GetReturnValue().Set(Boolean::New(isolate, true));
#else
		args.GetReturnValue().Set(Boolean::New(isolate, false));
#endif
	}
	
	void krom_begin(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		if (args[0]->IsNull() || args[0]->IsUndefined()) {
			Kore::Graphics::restoreRenderTarget();
		}
		else {
			Local<Object> obj = args[0]->ToObject()->Get(String::NewFromUtf8(isolate, "renderTarget_"))->ToObject();
			Local<External> rtfield = Local<External>::Cast(obj->GetInternalField(0));
			Kore::RenderTarget* renderTarget = (Kore::RenderTarget*)rtfield->Value();
			Kore::Graphics::setRenderTarget(renderTarget, 0, 0);
		}
	}
	
	void krom_end(const FunctionCallbackInfo<Value>& args) {
		HandleScope scope(args.GetIsolate());
		
	}
	
	bool startV8(char* scriptfile) {
#ifdef SYS_OSX
		char filepath[256];
		strcpy(filepath, macgetresourcepath());
		strcat(filepath, "/");
		strcat(filepath, "macos");
		strcat(filepath, "/");
		V8::InitializeICUDefaultLocation(filepath);
		V8::InitializeExternalStartupData(filepath);
#else
		V8::InitializeICUDefaultLocation("./");
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
		krom->Set(String::NewFromUtf8(isolate, "log"), FunctionTemplate::New(isolate, LogCallback));
		krom->Set(String::NewFromUtf8(isolate, "clear"), FunctionTemplate::New(isolate, graphics_clear));
		krom->Set(String::NewFromUtf8(isolate, "setCallback"), FunctionTemplate::New(isolate, krom_set_callback));
		krom->Set(String::NewFromUtf8(isolate, "setKeyboardDownCallback"), FunctionTemplate::New(isolate, krom_set_keyboard_down_callback));
		krom->Set(String::NewFromUtf8(isolate, "setKeyboardUpCallback"), FunctionTemplate::New(isolate, krom_set_keyboard_up_callback));
		krom->Set(String::NewFromUtf8(isolate, "setMouseDownCallback"), FunctionTemplate::New(isolate, krom_set_mouse_down_callback));
		krom->Set(String::NewFromUtf8(isolate, "setMouseUpCallback"), FunctionTemplate::New(isolate, krom_set_mouse_up_callback));
		krom->Set(String::NewFromUtf8(isolate, "setMouseMoveCallback"), FunctionTemplate::New(isolate, krom_set_mouse_move_callback));
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
		krom->Set(String::NewFromUtf8(isolate, "loadImage"), FunctionTemplate::New(isolate, krom_load_image));
		krom->Set(String::NewFromUtf8(isolate, "loadSound"), FunctionTemplate::New(isolate, krom_load_sound));
		krom->Set(String::NewFromUtf8(isolate, "loadBlob"), FunctionTemplate::New(isolate, krom_load_blob));
		krom->Set(String::NewFromUtf8(isolate, "getConstantLocation"), FunctionTemplate::New(isolate, krom_get_constant_location));
		krom->Set(String::NewFromUtf8(isolate, "getTextureUnit"), FunctionTemplate::New(isolate, krom_get_texture_unit));
		krom->Set(String::NewFromUtf8(isolate, "setTexture"), FunctionTemplate::New(isolate, krom_set_texture));
		krom->Set(String::NewFromUtf8(isolate, "setTextureParameters"), FunctionTemplate::New(isolate, krom_set_texture_parameters));
		krom->Set(String::NewFromUtf8(isolate, "setBool"), FunctionTemplate::New(isolate, krom_set_bool));
		krom->Set(String::NewFromUtf8(isolate, "setInt"), FunctionTemplate::New(isolate, krom_set_int));
		krom->Set(String::NewFromUtf8(isolate, "setFloat"), FunctionTemplate::New(isolate, krom_set_float));
		krom->Set(String::NewFromUtf8(isolate, "setFloat2"), FunctionTemplate::New(isolate, krom_set_float2));
		krom->Set(String::NewFromUtf8(isolate, "setFloat3"), FunctionTemplate::New(isolate, krom_set_float3));
		krom->Set(String::NewFromUtf8(isolate, "setFloat4"), FunctionTemplate::New(isolate, krom_set_float4));
		krom->Set(String::NewFromUtf8(isolate, "setMatrix"), FunctionTemplate::New(isolate, krom_set_matrix));
		krom->Set(String::NewFromUtf8(isolate, "getTime"), FunctionTemplate::New(isolate, krom_get_time));
		krom->Set(String::NewFromUtf8(isolate, "createRenderTarget"), FunctionTemplate::New(isolate, krom_create_render_target));
		krom->Set(String::NewFromUtf8(isolate, "viewport"), FunctionTemplate::New(isolate, krom_viewport));
		krom->Set(String::NewFromUtf8(isolate, "setDepthMode"), FunctionTemplate::New(isolate, krom_set_depth_mode));
		krom->Set(String::NewFromUtf8(isolate, "renderTargetsInvertedY"), FunctionTemplate::New(isolate, krom_render_targets_inverted_y));
		krom->Set(String::NewFromUtf8(isolate, "begin"), FunctionTemplate::New(isolate, krom_begin));
		krom->Set(String::NewFromUtf8(isolate, "end"), FunctionTemplate::New(isolate, krom_end));
		
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
	
	bool codechanged = false;

	void parseCode();
	
	void runV8() {
		if (codechanged) {
			parseCode();
			codechanged = false;
		}
		
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
	
	void keyDown(Kore::KeyCode code, wchar_t character) {
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
			printf("Trace: %s\n", *stack_trace);
		}
	}
	
	void keyUp(Kore::KeyCode code, wchar_t character) {
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
			printf("Trace: %s\n", *stack_trace);
		}
	}
	
	void mouseMove(int window, int x, int y, int mx, int my) {
		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, globalContext);
		Context::Scope context_scope(context);
		
		TryCatch try_catch(isolate);
		v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, mouseMoveFunction);
		Local<Value> result;
		const int argc = 2;
		Local<Value> argv[argc] = {Int32::New(isolate, x), Int32::New(isolate, y)};
		if (!func->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
			v8::String::Utf8Value stack_trace(try_catch.StackTrace());
			printf("Trace: %s\n", *stack_trace);
		}
	}
	
	void mouseDown(int window, int button, int x, int y) {
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
			printf("Trace: %s\n", *stack_trace);
		}
	}
	
	void mouseUp(int window, int button, int x, int y) {
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
			printf("Trace: %s\n", *stack_trace);
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
		ParseMethod
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
					if (endsWith(line, ".prototype = {")) { // parse methods
						mode = ParseMethods;
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
					else if (endsWith(line, "};")) {
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
							script += currentFunction->body;
							script += "\");";
							
							//printf("Script:\n%s\n", script.c_str());
							printf("Patching method %s in class %s.\n", currentFunction->name.c_str(), currentClass->name.c_str());
							
							Local<String> source = String::NewFromUtf8(isolate, script.c_str(), NewStringType::kNormal).ToLocalChecked();
							
							TryCatch try_catch(isolate);
							
							Local<Script> compiled_script;
							if (!Script::Compile(context, source).ToLocal(&compiled_script)) {
								v8::String::Utf8Value stack_trace(try_catch.StackTrace());
								printf("Trace: %s\n", *stack_trace);
							}

							Local<Value> result;
							if (!compiled_script->Run(context).ToLocal(&result)) {
								v8::String::Utf8Value stack_trace(try_catch.StackTrace());
								printf("Trace: %s\n", *stack_trace);
							}
						}
						mode = ParseMethods;
					}
					break;
				}
			}
		}
		printf("%i types found.\n", types);
	}
}

extern "C" void watchDirectories(char* path1, char* path2);

extern "C" void filechanged(char* path) {
	std::string strpath = path;
	if (endsWith(strpath, ".png")) {
		std::string name = strpath.substr(strpath.find_last_of('/') + 1);
		imageChanges[name] = true;
	}
	else if (endsWith(strpath, ".essl")) {
		std::string name = strpath.substr(strpath.find_last_of('/') + 1);
		name = name.substr(0, name.find_last_of('.'));
		name = replace(name, '.', '_');
		name = replace(name, '-', '_');
		printf("Shader changed: %s.\n", name.c_str());
		shaderFileNames[name] = strpath;
		shaderChanges[name] = true;
	}
	else if (endsWith(strpath, "krom.js")) {
		printf("Code changed.\n");
		codechanged = true;
	}
}

void startDebugger(v8::Isolate* isolate);

int kore(int argc, char** argv) {
	int w = 1024;
	int h = 768;
	
	assetsdir = argv[1];
	kromjs = assetsdir + "/krom.js";
	
	Kore::setFilesLocation(argv[1]);
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
	
	Kore::Graphics::setRenderState(Kore::DepthTest, false);
	//Mixer::init();
	//Audio::init();
	Kore::Random::init(Kore::System::time() * 1000);
	
	Kore::System::setCallback(update);
	
	Kore::Keyboard::the()->KeyDown = keyDown;
	Kore::Keyboard::the()->KeyUp = keyUp;
	Kore::Mouse::the()->Move = mouseMove;
	Kore::Mouse::the()->Press = mouseDown;
	Kore::Mouse::the()->Release = mouseUp;
	
	//Mixer::play(music);
	
	Kore::FileReader reader;
	reader.open("krom.js");
	char* code = new char[reader.size() + 1];
	memcpy(code, reader.readAll(), reader.size());
	code[reader.size()] = 0;
	reader.close();

	bool started = startV8(code);

	parseCode();

	watchDirectories(argv[1], argv[2]);
	
	if (started) {
		//**startDebugger(isolate);
		Kore::System::start();
	}
	
	exit(0); // TODO
	
	endV8();
	
	return 0;
}
