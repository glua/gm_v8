#include <stdio.h>
#include <map>

#include "GarrysMod/Lua/Interface.h"

#include "Color.h"
#include "tier0/dbg.h"
#include "v8.h"

/// Lua Loadstring function -- Credit http://facepunch.com/showthread.php?t=1386454
#include <windows.h> //TODO make this work on other platforms

HMODULE module_lua = GetModuleHandle("lua_shared.dll");

typedef int(*f_loadstring)(lua_State* state, const char* code);
f_loadstring luaL_loadstring = (f_loadstring)GetProcAddress(module_lua,"luaL_loadstring");

///

using namespace std;
using namespace GarrysMod;
using namespace v8;

//Credit to gm_io, I am a dumb pleb and have no idea what reinterpret_cast does.
#define LOADINTERFACE(_module_, _version_, _out_) Sys_LoadInterface(_module_, _version_, NULL, reinterpret_cast<void**>(& _out_ ))

#define V8SCOPE Isolate::Scope isolate_scope(v8engine);HandleScope handle_scope(v8engine)
#define V8SCOPE_ESC Isolate::Scope isolate_scope(v8engine);EscapableHandleScope handle_scope(v8engine)
#define ESC handle_scope.Escape

#define COLOR_V8 Color(255,200,180,255)
#define COLOR_LOG Color(200,200,200,255)
#define COLOR_ERR Color(255,100,100,255)

#undef LUA
#define LUA glua_state->luabase

Isolate* v8engine;
Eternal<ObjectTemplate> template_libs;
Eternal<ObjectTemplate> template_luaProxy;
lua_State* glua_state;

map<int,UniquePersistent<Object>> lua_proxies;
int proxyTableRef;

char* jst_proxy = "LUA PROXY";

bool checkIsProxy(Local<Value> val) {
	V8SCOPE;
	if (val->IsObject()) {
		Local<Object> obj = val->ToObject();
		if (obj->InternalFieldCount()==2 && obj->GetAlignedPointerFromInternalField(0)==jst_proxy)
			return true;
	}
	return false;
}

void jsVal2lua(Local<Value> jsVal) {
	V8SCOPE;
	if (jsVal->IsNumber() || jsVal->IsNumberObject())
		LUA->PushNumber(jsVal->ToNumber()->Value());
	else if (jsVal->IsString() || jsVal->IsStringObject())
		LUA->PushString(*String::Utf8Value(jsVal));
	else if (jsVal->IsNull() || jsVal->IsUndefined())
		LUA->PushNil();
	else if (jsVal->IsBoolean() || jsVal->IsBooleanObject())
		LUA->PushBool(jsVal->BooleanValue());
	else if (checkIsProxy(jsVal))
		LUA->ReferencePush(jsVal->ToObject()->GetInternalField(1)->Uint32Value());
	else
		LUA->PushString("!JS TO LUA TYPE CONVERSION FAILED!");
	
}

//Removes a table from the top of the lua stack. Converts it to a js proxy.
Local<Object> luaTableOrFunction2proxy() {
	V8SCOPE_ESC;

	LUA->ReferencePush(proxyTableRef);
	LUA->Push(-2);
	LUA->GetTable(-2);

	int id;
	if (LUA->IsType(-1,Lua::Type::NUMBER)) {
		id = LUA->GetNumber();
		LUA->Pop();
	} else {
		LUA->Pop();
		LUA->Push(-2);
		id = LUA->ReferenceCreate();
		LUA->Push(-2);
		LUA->PushNumber(id);
		LUA->SetTable(-3);
	}
	LUA->Pop(2);

	//Done with lua stack fuckery. Yay!

	Local<Object> proxyHandle;
	
	if (lua_proxies.count(id)) {
		//ConColorMsg(COLOR_V8,"[V8] Reused proxy!\n");
		proxyHandle = Local<Object>::New(v8engine,lua_proxies[id]);
	} else {
		//ConColorMsg(COLOR_V8,"[V8] New proxy!\n");
		proxyHandle = template_luaProxy.Get(v8engine)->NewInstance();
		proxyHandle->SetAlignedPointerInInternalField(0,reinterpret_cast<void*>(jst_proxy));
		proxyHandle->SetInternalField(1,Number::New(v8engine,id));
		lua_proxies[id].Reset(v8engine,proxyHandle);
		//lua_proxies[id].SetWeak TO DO GC
	}

	return ESC(proxyHandle);
}

//Removes something from top of lua stack. Converts it to js handle.
Local<Value> luaVal2js() {
	V8SCOPE_ESC;

	Local<Value> result;

	switch (LUA->GetType(-1)) {
	//Basic Types
	case Lua::Type::NUMBER:
		result = Number::New(v8engine,LUA->GetNumber());
		LUA->Pop();
		break;
	case Lua::Type::STRING:
		result = String::NewFromUtf8(v8engine,LUA->GetString());
		LUA->Pop();
		break;
	//Primitive Types
	case Lua::Type::BOOL:
		result = Boolean::New(v8engine,LUA->GetBool());
		LUA->Pop();
		break;
	case Lua::Type::NIL:
		result = Null(v8engine);
		LUA->Pop();
		break;
	//Proxy Types
	case Lua::Type::TABLE:
	case Lua::Type::FUNCTION:
		result = luaTableOrFunction2proxy();
		break;
	//Non-Convertable Types (userdata)
	default:
		result = String::NewFromUtf8(v8engine,"!LUA TO JS TYPE CONVERSION FAILED!");
		LUA->Pop();
	}

	return ESC(result);
}

int luaf_v8_run(lua_State* state) {
	LUA->CheckType(1, Lua::Type::STRING);

	Isolate::Scope isolate_scope(v8engine);
	HandleScope handle_scope(v8engine);

	const char* source_raw = LUA->GetString(1);

	Local<Context> context = Context::New(v8engine,NULL,template_libs.Get(v8engine));
	Context::Scope context_scope(context);

	Local<Object> lualib = context->Global()->Get(String::NewFromUtf8(v8engine,"glua"))->ToObject();

	//Global proxy
	LUA->PushSpecial(Lua::SPECIAL_GLOB);
	Local<Object> globalProxy = luaTableOrFunction2proxy();
	lualib->Set(String::NewFromUtf8(v8engine,"G"),globalProxy);

	//Global proxy shortcut
	context->Global()->Set(String::NewFromUtf8(v8engine,"_G"),globalProxy);

	//Input
	LUA->Push(2);
	lualib->Set(String::NewFromUtf8(v8engine,"input"),luaVal2js());

	// Create a string containing the JavaScript source code, then compile it.
	Local<String> source = String::NewFromUtf8(v8engine,source_raw);
	
	TryCatch trycatch;
	Local<Script> script = Script::Compile(source);
	
	if (script.IsEmpty()) {
		String::Utf8Value trace_str(trycatch.StackTrace());

		ConColorMsg(COLOR_V8,"[V8]");
		ConColorMsg(COLOR_ERR,"[COMPILE ERROR] ");
		ConColorMsg(COLOR_ERR,*trace_str);
		ConColorMsg(COLOR_ERR,"\n");
	} else {
		// Run the script to get the result.
		Local<Value> result = script->Run();

		if (result.IsEmpty()) {
			String::Utf8Value trace_str(trycatch.StackTrace());

			ConColorMsg(COLOR_V8,"[V8]");
			ConColorMsg(COLOR_ERR,"[ERROR] ");
			ConColorMsg(COLOR_ERR,*trace_str);
			ConColorMsg(COLOR_ERR,"\n");
		} else {
			Local<Value> out = lualib->Get(String::NewFromUtf8(v8engine,"output"));
			jsVal2lua(out);

			return 1;
		}
	}

	return 0;
}

void jsf_glua_run(const FunctionCallbackInfo<Value>& call_info) {
	V8SCOPE;

	if (call_info[0]->IsString()) {
		String::Utf8Value code_str(call_info[0]);
		luaL_loadstring(glua_state,*code_str);
	} else
		return;
	
	if (LUA->IsType(-1, Lua::Type::FUNCTION))
		LUA->Call(0,1);
	else
		return;

	call_info.GetReturnValue().Set(luaVal2js());
}

/*Expects stack=
	Thetable
	LuaIndex

	pops index only
*/

void convert_js2lua_r(Local<Object> obj,Local<Array> jsIndex) {
	V8SCOPE;
	
	//Add to indexes
	LUA->PushNumber(jsIndex->Length());
	LUA->Push(-3);
	LUA->SetTable(-3);

	jsIndex->Set(jsIndex->Length(),obj);

	Local<Array> keys = obj->GetOwnPropertyNames();
	for (int i=0;i< ((int)keys->Length());i++) {
		//key
		Local<Value> key = keys->Get(i);
		if (key->IsNumber() || key->IsNumberObject()) {
			LUA->PushNumber(key->ToNumber()->Value());
		} else {
			String::Utf8Value raw_key(key);
			LUA->PushString(*raw_key);
		}
		//value
		Local<Value> value = obj->Get(key);
		if (value->IsObject()) {
			//Check our index!
			int prevIndex = -1;
			for (int j=0;j<(int)jsIndex->Length();j++) {
				if (jsIndex->Get(j)->SameValue(value)) {
					prevIndex=j;
					break;
				}
			}

			if (prevIndex>-1) {
				//Circular ref, reuse table...
				LUA->PushNumber(prevIndex);
				LUA->GetTable(-3);
			} else { //TODO check for functions and convert them correctly
				LUA->CreateTable();
				LUA->Push(-3);
				convert_js2lua_r(value->ToObject(),jsIndex);
			}
		} else {
			jsVal2lua(value);
		}
		//Set
		LUA->SetTable(-4);
	}

	//Pop the index!
	LUA->Pop();
}

void jsf_glua_Table(const FunctionCallbackInfo<Value>& call_info) {
	V8SCOPE;

	if (!call_info.IsConstructCall()) {
		//must be construct call. error?
		return;
	}

	LUA->CreateTable();
	if (call_info[0]->IsObject()) {
		LUA->CreateTable();
		
		Local<Array> jsIndex=Array::New(v8engine);
		//int i = 1;

		convert_js2lua_r(call_info[0]->ToObject(),jsIndex);
	}

	call_info.GetReturnValue().Set(luaTableOrFunction2proxy());
}

int luaf_internal_runJsFunc(lua_State* state) { //our real function is at index -10003
	V8SCOPE;

	Lua::UserData* ud = (Lua::UserData*)LUA->GetUserdata(-10003);
	
	Persistent<Function> *fHandle = (Persistent<Function>*)ud->data;
	
	Local<Function> f = Local<Function>::New(v8engine,*fHandle);
	
	Context::Scope context_scope(f->CreationContext());
	
	f->Call(f,0,NULL);
	
	return 0;
}

void jsf_glua_Function(const FunctionCallbackInfo<Value>& call_info) {
	V8SCOPE;

	if (!call_info.IsConstructCall()) {
		//must be construct call. error?
		return;
	}

	//LUA->CreateTable();
	if (call_info[0]->IsFunction()) {
		Persistent<Function> *fHandle = new Persistent<Function>(v8engine,Local<Function>::Cast(call_info[0]));
		
		Lua::UserData* ud = (Lua::UserData*)LUA->NewUserdata(sizeof(GarrysMod::Lua::UserData));
		ud->data= fHandle;
		ud->type= Lua::Type::USERDATA;

		LUA->PushCClosure(&luaf_internal_runJsFunc,1);

		call_info.GetReturnValue().Set(luaTableOrFunction2proxy());


		//LUA->CreateTable();
		
		//Local<Array> jsIndex=Array::New(v8engine);
		//int i = 1;

		//convert_js2lua_r(call_info[0]->ToObject(),jsIndex);
	}

	//call_info.GetReturnValue().Set(luaTableOrFunction2proxy());
}

void jsf_console_log(const FunctionCallbackInfo<Value>& call_info) {
	V8SCOPE;

	if (call_info.Length()>0) {
		ConColorMsg(COLOR_V8,"[V8]");
		ConColorMsg(COLOR_LOG,"[LOG] ");

		for (int i=0;i<call_info.Length();i++) {
			String::Utf8Value print_str(call_info[i]);
			ConColorMsg(COLOR_LOG,*print_str);
			if (i+1<call_info.Length())
				ConColorMsg(COLOR_LOG," ");
		}

		ConColorMsg(COLOR_LOG,"\n");
	}
}

void jsc_proxy(const FunctionCallbackInfo<Value>& call_info) {
	V8SCOPE;

	int id = call_info.This()->GetInternalField(1)->Uint32Value();
	LUA->ReferencePush(id);
	
	if (LUA->IsType(-1,Lua::Type::FUNCTION)) {
		for (int i=0;i<call_info.Length();i++) {
			jsVal2lua(call_info[i]);
		}
		LUA->Call(call_info.Length(),1);

		//Convert to js value
		Local<Value> val = luaVal2js();
		call_info.GetReturnValue().Set(val); 
	} else {
		//Trying to call table or something... error?
	}
}

void jsf_proxy_toString(const FunctionCallbackInfo<Value>& call_info) {
	V8SCOPE;

	int id = call_info.This()->GetInternalField(1)->Uint32Value();
	LUA->ReferencePush(id);

	if (LUA->IsType(-1,Lua::Type::TABLE)) {
		call_info.GetReturnValue().Set(String::NewFromUtf8(v8engine,"[LUA TABLE PROXY]"));
	} else if (LUA->IsType(-1,Lua::Type::FUNCTION)) {
		call_info.GetReturnValue().Set(String::NewFromUtf8(v8engine,"[LUA FUNCTION PROXY]"));
	}

	LUA->Pop();
}

/*Expects stack=
	Thetable
	LuaIndex

	pops both
*/

Local<Object> convert_lua2js_r(Local<Array> &jsIndex) {
	V8SCOPE_ESC;

	Local<Object> jsObject = Object::New(v8engine);

	//Add to indexes
	LUA->Push(-2);
	LUA->PushNumber(jsIndex->Length());
	LUA->SetTable(-3);

	jsIndex->Set(jsIndex->Length(),jsObject);

	LUA->PushNil();
	while (LUA->Next(-3)!=0) {
		//We need to push the key AGAIN because GetString fucks with the key on the stack if it isn't a string already.
		LUA->Push(-2);
		//We can only use the pair if the key is an string (allow numbers as well)
		int keytype = LUA->GetType(-1);
		if (keytype==Lua::Type::NUMBER||keytype==Lua::Type::STRING) {
			Local<String> jsKey = String::NewFromUtf8(v8engine,LUA->GetString());
			LUA->Pop();
			
			if (LUA->IsType(-1,Lua::Type::TABLE)) {
				//Detect circular references or recurse into table.
				LUA->Push(-1);
				LUA->GetTable(-4);
				if (LUA->IsType(-1,Lua::Type::NUMBER)) {
					jsObject->Set(jsKey,jsIndex->Get(LUA->GetNumber()));
					LUA->Pop(2);
				} else {
					LUA->Pop();
					LUA->Push(-3);
					jsObject->Set(jsKey,convert_lua2js_r(jsIndex));
				}
			} else {
				jsObject->Set(jsKey,luaVal2js());
			}
		} else {
			LUA->Pop(2);
		}
	}
	LUA->Pop(2);
	return ESC(jsObject);
}

void jsf_proxy_convert(const FunctionCallbackInfo<Value>& call_info) {
	V8SCOPE;

	int id = call_info.This()->GetInternalField(1)->Uint32Value();
	LUA->ReferencePush(id);
	
	if (LUA->IsType(-1,Lua::Type::TABLE)) {
		LUA->CreateTable();
		Local<Array> jsIndex = Array::New(v8engine);
		
		Local<Object> jsObject = convert_lua2js_r(jsIndex);

		call_info.GetReturnValue().Set(jsObject);
	} else {
		LUA->Pop();
		//only tables can be converted. Error?
	}
}

void jsf_proxy_rawget(const FunctionCallbackInfo<Value>& call_info) {
	V8SCOPE;

	int id = call_info.This()->GetInternalField(1)->Uint32Value();
	LUA->ReferencePush(id);
	
	if (LUA->IsType(-1,Lua::Type::TABLE)) {
		jsVal2lua(call_info[0]);
		LUA->GetTable(-2);

		//Convert to js value
		Local<Value> val = luaVal2js();

		if (!val->IsNull())
			call_info.GetReturnValue().Set(val); 
	}

	LUA->Pop();
}

void jsf_proxy_rawset(const FunctionCallbackInfo<Value>& call_info) {
	V8SCOPE;

	int id = call_info.This()->GetInternalField(1)->Uint32Value();
	LUA->ReferencePush(id);
	
	if (LUA->IsType(-1,Lua::Type::TABLE)) {
		jsVal2lua(call_info[0]);
		jsVal2lua(call_info[1]);
		LUA->SetTable(-3);
	}

	LUA->Pop();
}

void jsi_proxy_gets(Local<String> key,const PropertyCallbackInfo<Value>& info) {
	V8SCOPE;

	String::Utf8Value raw_key(key);
	if (strcmp(*raw_key,"toString")==0 || strcmp(*raw_key,"$get")==0 || strcmp(*raw_key,"$set")==0)
		return;

	int id = info.This()->GetInternalField(1)->Uint32Value();
	LUA->ReferencePush(id);
	
	if (LUA->IsType(-1,Lua::Type::TABLE)) {
		LUA->GetField(-1,*raw_key);

		//Convert to js value
		Local<Value> val = luaVal2js();

		if (!val->IsNull())
			info.GetReturnValue().Set(val); 
	}

	LUA->Pop();
}

void jsi_proxy_sets(Local<String> key,Local<Value> value,const PropertyCallbackInfo<Value>& info) {
	V8SCOPE;

	String::Utf8Value raw_key(key);

	int id = info.This()->GetInternalField(1)->Uint32Value();
	LUA->ReferencePush(id);
	
	if (LUA->IsType(-1,Lua::Type::TABLE)) {
		jsVal2lua(value);
		LUA->SetField(-2,*raw_key);
	}

	LUA->Pop();
}

void jsi_proxy_getn(uint32_t key,const PropertyCallbackInfo<Value>& info) {
	V8SCOPE;

	int id = info.This()->GetInternalField(1)->Uint32Value();
	LUA->ReferencePush(id);
	
	if (LUA->IsType(-1,Lua::Type::TABLE)) {
		LUA->PushNumber(key);
		LUA->GetTable(-2);

		//Convert to js value
		Local<Value> val = luaVal2js();

		if (!val->IsNull())
			info.GetReturnValue().Set(val); 
	}

	LUA->Pop();
}

void jsi_proxy_setn(uint32_t key,Local<Value> value,const PropertyCallbackInfo<Value>& info) {
	V8SCOPE;

	int id = info.This()->GetInternalField(1)->Uint32Value();
	LUA->ReferencePush(id);
	
	if (LUA->IsType(-1,Lua::Type::TABLE)) {
		LUA->PushNumber(key);
		jsVal2lua(value);
		LUA->SetTable(-3);
	}

	LUA->Pop();
}

GMOD_MODULE_OPEN()
{
	v8engine= Isolate::New();
	
	Isolate::Scope isolate_scope(v8engine);
	HandleScope handle_scope(v8engine);

	Local<ObjectTemplate> libs = ObjectTemplate::New();
	template_libs.Set(v8engine,libs);
		
		Local<ObjectTemplate> libGLua = ObjectTemplate::New();
		libs->Set(v8engine,"glua",libGLua);

			libGLua->Set(v8engine,"run",FunctionTemplate::New(v8engine,&jsf_glua_run));

			libGLua->Set(v8engine,"Table",FunctionTemplate::New(v8engine,&jsf_glua_Table));
			libGLua->Set(v8engine,"Function",FunctionTemplate::New(v8engine,&jsf_glua_Function));

		Local<ObjectTemplate> libConsole = ObjectTemplate::New();
		libs->Set(v8engine,"console",libConsole);

			libConsole->Set(v8engine,"log",FunctionTemplate::New(v8engine,&jsf_console_log));

	//Lua proxy crap
	Local<ObjectTemplate> luaProxy = ObjectTemplate::New();
	template_luaProxy.Set(v8engine,luaProxy);

		luaProxy->SetInternalFieldCount(2);

		luaProxy->SetCallAsFunctionHandler(&jsc_proxy);

		luaProxy->SetNamedPropertyHandler(&jsi_proxy_gets,&jsi_proxy_sets);
		luaProxy->SetIndexedPropertyHandler(&jsi_proxy_getn,&jsi_proxy_setn);
		luaProxy->Set(v8engine,"$get",FunctionTemplate::New(v8engine,&jsf_proxy_rawget));
		luaProxy->Set(v8engine,"$set",FunctionTemplate::New(v8engine,&jsf_proxy_rawset));

		luaProxy->Set(v8engine,"$convert",FunctionTemplate::New(v8engine,&jsf_proxy_convert));

		luaProxy->Set(v8engine,"toString",FunctionTemplate::New(v8engine,&jsf_proxy_toString));
		 
	//Lua setup

	glua_state= state;

	LUA->CreateTable();
	proxyTableRef = LUA->ReferenceCreate();

	LUA->PushSpecial(Lua::SPECIAL_GLOB);
	LUA->PushString("v8");
	LUA->CreateTable();
	LUA->PushString("run");
	LUA->PushCFunction(luaf_v8_run);
	LUA->SetTable(-3);
	LUA->SetTable(-3);

	//LUA->PushSpecial(Lua::SPECIAL_GLOB);
	//table2proxy();

	ConColorMsg(COLOR_V8,"[V8] Module Initialized!\n");

	return 0;
}

//Called when the goddamn module is closed
//Not sure if this ever gets called since we're running in the menu context
GMOD_MODULE_CLOSE()
{
	lua_proxies.clear(); //Fun Fact: If you don't do this, V8 will break itself and hang 2-3 minutes trying to figure out what to do with the leftover handles.
	v8engine->Dispose();
	
	ConColorMsg(COLOR_V8,"[V8] Module Stopped!\n");
	
	return 0;
}