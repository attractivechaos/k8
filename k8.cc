/* The MIT License

   Copyright (c) 2012-2013, 2016 by Attractive Chaos <attractor@live.co.uk>

   Permission is hereby granted, free of charge, to any person obtaining
   a copy of this software and associated documentation files (the
   "Software"), to deal in the Software without restriction, including
   without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to
   permit persons to whom the Software is furnished to do so, subject to
   the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/
#define K8_VERSION "0.3.0-r81"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include <zlib.h>

#include "include/v8-context.h"
#include "include/v8-exception.h"
#include "include/v8-initialization.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "include/v8-script.h"
#include "include/v8-container.h"
#include "include/v8-template.h"
#include "include/libplatform/libplatform.h"

/*******************************
 *** Fundamental V8 routines ***
 *******************************/

static inline const char *k8_cstr(const v8::String::Utf8Value &str) // Convert a V8 string to C string
{
	return *str? *str : "<N/A>";
}

static void k8_exception(v8::Isolate* isolate, v8::TryCatch* try_catch) // Exception handling. Adapted from v8/shell.cc
{
	v8::HandleScope handle_scope(isolate);
	v8::String::Utf8Value exception(isolate, try_catch->Exception());
	const char* exception_string = k8_cstr(exception);
	v8::Local<v8::Message> message = try_catch->Message();
	if (message.IsEmpty()) {
		// V8 didn't provide any extra information about this error; just print the exception.
		fprintf(stderr, "%s\n", exception_string);
	} else {
		// Print (filename):(line number): (message).
		v8::String::Utf8Value filename(isolate, message->GetScriptOrigin().ResourceName());
		v8::Local<v8::Context> context(isolate->GetCurrentContext());
		const char* filename_string = k8_cstr(filename);
		int linenum = message->GetLineNumber(context).FromJust();
		fprintf(stderr, "%s:%i: %s\n", filename_string, linenum, exception_string);
		// Print line of source code.
		v8::String::Utf8Value sourceline(isolate, message->GetSourceLine(context).ToLocalChecked());
		const char* sourceline_string = k8_cstr(sourceline);
		fprintf(stderr, "%s\n", sourceline_string);
		// Print wavy underline (GetUnderline is deprecated).
		int start = message->GetStartColumn(context).FromJust();
		for (int i = 0; i < start; i++) fputc(' ', stderr);
		int end = message->GetEndColumn(context).FromJust();
		for (int i = start; i < end; i++) fputc('^', stderr);
		fputc('\n', stderr);
		v8::Local<v8::Value> stack_trace_string;
		if (try_catch->StackTrace(context).ToLocal(&stack_trace_string) &&
				stack_trace_string->IsString() &&
				stack_trace_string.As<v8::String>()->Length() > 0)
		{
			v8::String::Utf8Value stack_trace(isolate, stack_trace_string);
			const char* err = k8_cstr(stack_trace);
			fputs(err, stderr); fputc('\n', stderr);
		}
	}
}

static bool k8_execute(v8::Isolate* isolate, v8::Local<v8::String> source, v8::Local<v8::Value> name, bool prt_rst) // Execute JS in a string. Adapted from v8/shell.cc
{
	v8::HandleScope handle_scope(isolate);
	v8::TryCatch try_catch(isolate);
	v8::ScriptOrigin origin(isolate, name);
	v8::Local<v8::Context> context(isolate->GetCurrentContext());
	v8::Local<v8::Script> script;
	if (!v8::Script::Compile(context, source, &origin).ToLocal(&script)) {
		k8_exception(isolate, &try_catch);
		return false;
	} else {
		v8::Local<v8::Value> result;
		if (!script->Run(context).ToLocal(&result)) {
			assert(try_catch.HasCaught());
			k8_exception(isolate, &try_catch);
			return false;
		} else {
			assert(!try_catch.HasCaught());
			if (prt_rst && !result->IsUndefined()) {
				// If all went well and the result wasn't undefined then print the returned value.
				v8::String::Utf8Value str(isolate, result);
				puts(k8_cstr(str));
			}
			return true;
		}
	}
}

v8::MaybeLocal<v8::String> k8_readfile(v8::Isolate* isolate, const char *name) // Read an entire file. Adapted from v8/shell.cc
{
	FILE* file = fopen(name, "rb");
	if (file == NULL) {
		fprintf(stderr, "ERROR: fail to open file '%s'.\n", name);
		return v8::Handle<v8::String>();
	}

	fseek(file, 0, SEEK_END);
	int size = ftell(file);
	rewind(file);

	char* chars = new char[size + 1];
	chars[size] = '\0';
	for (int i = 0; i < size;) {
		int read = static_cast<int>(fread(&chars[i], 1, size - i, file));
		i += read;
	}
	fclose(file);

	if (size > 2 && strncmp(chars, "#!", 2) == 0) { // then skip the "#!" line
		int i;
		for (i = 0; i < size; ++i)
			if (chars[i] == '\n') break;
		size -= i + 1;
		memmove(chars, &chars[i+1], size);
	}

	v8::MaybeLocal<v8::String> result = v8::String::NewFromUtf8(
			isolate, chars, v8::NewStringType::kNormal, static_cast<int>(size));
	delete[] chars;
	return result;
}

/******************************
 *** New built-in functions ***
 ******************************/

static void k8_print(const v8::FunctionCallbackInfo<v8::Value> &args) // print(): print to stdout; TAB demilited if multiple arguments are provided
{
	for (int i = 0; i < args.Length(); i++) {
		v8::HandleScope handle_scope(args.GetIsolate());
		if (i) putchar('\t');
		v8::String::Utf8Value str(args.GetIsolate(), args[i]);
		fputs(k8_cstr(str), stdout);
	}
	putchar('\n');
}

static void k8_warn(const v8::FunctionCallbackInfo<v8::Value> &args) // warn(): similar print() but print to stderr
{
	for (int i = 0; i < args.Length(); i++) {
		v8::HandleScope handle_scope(args.GetIsolate());
		if (i) putchar('\t');
		v8::String::Utf8Value str(args.GetIsolate(), args[i]);
		fputs(k8_cstr(str), stderr);
	}
	fputc('\n', stderr);
}

static void k8_exit(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	// If not arguments are given args[0] will yield undefined which converts to the integer value 0.
	int exit_code = args[0]->Int32Value(args.GetIsolate()->GetCurrentContext()).FromMaybe(0);
	fflush(stdout); fflush(stderr);
	exit(exit_code);
}

static void k8_load(const v8::FunctionCallbackInfo<v8::Value> &args) // load(): Load and execute a JS file. It also searches ONE path in $K8_PATH
{
	char buf[1024], *path = getenv("K8_PATH");
	FILE *fp;
	for (int i = 0; i < args.Length(); i++) {
		v8::HandleScope handle_scope(args.GetIsolate());
		v8::String::Utf8Value file(args.GetIsolate(), args[i]);
		buf[0] = 0;
		if ((fp = fopen(*file, "r")) != 0) {
			fclose(fp);
			assert(strlen(*file) < 1023);
			strcpy(buf, *file);
		} else if (path) { // TODO: to allow multiple paths separated by ":"
			assert(strlen(path) + strlen(*file) + 1 < 1023);
			strcpy(buf, path); strcat(buf, "/"); strcat(buf, *file);
			if ((fp = fopen(buf, "r")) == 0) buf[0] = 0;
			else fclose(fp);
		}
		if (buf[0] == 0) {
			args.GetIsolate()->ThrowError("[load] fail to locate the file");
			return;
		}
		v8::Local<v8::String> source;
		if (!k8_readfile(args.GetIsolate(), buf).ToLocal(&source)) {
			args.GetIsolate()->ThrowError("[load] fail to read the file");
			return;
		}
		if (!k8_execute(args.GetIsolate(), source, args[i], false)) {
			args.GetIsolate()->ThrowError("[load] fail to execute the file");
			return;
		}
	}
}
#if 0
/********************
 *** Bytes object ***
 ********************/

typedef struct {
	int32_t n, m, tshift;
	v8::ExternalArrayType eta;
	uint8_t *a;
} kvec8_t;

static inline void set_length(const v8::Handle<v8::Object> &obj, const kvec8_t *v)
{
	obj->SetIndexedPropertiesToExternalArrayData(v->a, v->eta, v->n >> v->tshift);
}

static inline void kv_set_type(kvec8_t *v, const char *type)
{
	if (type == 0) v->tshift = 0, v->eta = v8::kExternalUnsignedByteArray;
	else if (strcmp(type, "int8_t") == 0) v->tshift = 0, v->eta = v8::kExternalByteArray;
	else if (strcmp(type, "uint8_t") == 0) v->tshift = 0, v->eta = v8::kExternalUnsignedByteArray;
	else if (strcmp(type, "int16_t") == 0) v->tshift = 1, v->eta = v8::kExternalShortArray;
	else if (strcmp(type, "uint16_t") == 0) v->tshift = 1, v->eta = v8::kExternalUnsignedShortArray;
	else if (strcmp(type, "int32_t") == 0) v->tshift = 2, v->eta = v8::kExternalIntArray;
	else if (strcmp(type, "uint32_t") == 0) v->tshift = 2, v->eta = v8::kExternalUnsignedIntArray;
	else if (strcmp(type, "float") == 0) v->tshift = 2, v->eta = v8::kExternalFloatArray;
	else if (strcmp(type, "double") == 0) v->tshift = 3, v->eta = v8::kExternalDoubleArray;
	else v->tshift = 0, v->eta = v8::kExternalUnsignedByteArray;
}

JS_METHOD(k8_bytes, args)
{
	v8::HandleScope scope;
	ASSERT_CONSTRUCTOR(args);
	kvec8_t *a;
	a = (kvec8_t*)calloc(1, sizeof(kvec8_t));
	kv_set_type(a, 0);
	if (args.Length() > 1 && args[1]->IsString()) {
		v8::String::AsciiValue type(args[1]);
		kv_set_type(a, *type);
	}
	if (args.Length()) {
		a->m = a->n = args[0]->Int32Value() << a->tshift;
		a->a = (uint8_t*)calloc(a->n, 1); // NB: we are expecting malloc/calloc/realloc() only allocate aligned memory
	}
	set_length(args.This(), a);
	SAVE_PTR(args, 0, a);
	return args.This();
}

static inline void kv_recapacity(kvec8_t *a, int32_t m)
{
	kroundup32(m);
	if (a->m != m) {
		a->a = (uint8_t*)realloc(a->a, m);
		if (a->m < m) memset(&a->a[a->m], 0, m - a->m);
		v8::V8::AdjustAmountOfExternalAllocatedMemory(m - a->m);
		a->m = m;
	}
	if (a->n > a->m) a->n = a->m;
}

JS_METHOD(k8_bytes_cast, args)
{
	v8::HandleScope scope;
	kvec8_t *v = LOAD_PTR(args, 0, kvec8_t*);
	if (args.Length()) {
		v8::String::AsciiValue type(args[0]);
		kv_set_type(v, *type);
	} else kv_set_type(v, 0);
	set_length(args.This(), v);
	return v8::Undefined();
}

JS_METHOD(k8_bytes_destroy, args)
{
	v8::HandleScope scope;
	kvec8_t *a = LOAD_PTR(args, 0, kvec8_t*);
	free(a->a);
	v8::V8::AdjustAmountOfExternalAllocatedMemory(-a->m);
	a->a = 0; a->n = a->m = 0;
	set_length(args.This(), a);
	free(a);
	SAVE_PTR(args, 0, 0);
	return v8::Undefined();
}

JS_METHOD(k8_bytes_set, args)
{
#define _extend_vec_(_l_) do { \
		if (pos + (int32_t)(_l_) >= a->m) \
			kv_recapacity(a, pos + (_l_)); \
		if (pos + (int32_t)(_l_) >= a->n >> a->tshift << a->tshift) { \
			a->n = pos + (_l_); \
			set_length(args.This(), a); \
		} \
		cnt = (_l_); \
	} while (0)

	v8::HandleScope scope;
	kvec8_t *a = LOAD_PTR(args, 0, kvec8_t*);
	if (args.Length() == 0) return v8::Undefined();
	int cnt = 0;
	int32_t pos = args.Length() >= 2? args[1]->Int32Value() : a->n>>a->tshift;
	if (args[0]->IsNumber()) {
		_extend_vec_(1<<a->tshift);
		if (a->eta == v8::kExternalUnsignedByteArray) a->a[pos] = args[0]->Uint32Value();
		else if (a->eta == v8::kExternalDoubleArray) ((double*)a->a)[pos] = args[0]->NumberValue();
		else if (a->eta == v8::kExternalFloatArray) ((float*)a->a)[pos] = args[0]->NumberValue();
		else if (a->eta == v8::kExternalByteArray) ((int8_t*)a->a)[pos] = args[0]->Int32Value();
		else if (a->eta == v8::kExternalIntArray) ((int32_t*)a->a)[pos] = args[0]->Int32Value();
		else if (a->eta == v8::kExternalUnsignedIntArray) ((uint32_t*)a->a)[pos] = args[0]->Uint32Value();
		else if (a->eta == v8::kExternalShortArray) ((int16_t*)a->a)[pos] = args[0]->Int32Value();
		else if (a->eta == v8::kExternalUnsignedShortArray) ((uint16_t*)a->a)[pos] = args[0]->Uint32Value();
	} else if (args[0]->IsString()) {
		v8::String::AsciiValue str(args[0]);
		const char *cstr = *str;
		_extend_vec_(str.length());
		for (int i = 0; i < str.length(); ++i) a->a[i+pos] = uint8_t(cstr[i]);
	} else if (args[0]->IsArray()) {
		unsigned i;
		v8::Handle<v8::Array> array = v8::Handle<v8::Array>::Cast(args[0]);
		_extend_vec_(array->Length()<<a->tshift);
		if (a->eta == v8::kExternalUnsignedByteArray) for (i = 0; i < array->Length(); ++i) a->a[pos + i] = array->Get(v8::Integer::New(i))->Uint32Value();
		else if (a->eta == v8::kExternalDoubleArray) for (i = 0; i < array->Length(); ++i) ((double*)a->a)[pos + i] = array->Get(v8::Integer::New(i))->NumberValue();
		else if (a->eta == v8::kExternalFloatArray) for (i = 0; i < array->Length(); ++i) ((float*)a->a)[pos + i] = array->Get(v8::Integer::New(i))->NumberValue();
		else if (a->eta == v8::kExternalByteArray) for (i = 0; i < array->Length(); ++i) ((int8_t*)a->a)[pos + i] = array->Get(v8::Integer::New(i))->Int32Value();
		else if (a->eta == v8::kExternalIntArray) for (i = 0; i < array->Length(); ++i) ((int32_t*)a->a)[pos + i] = array->Get(v8::Integer::New(i))->Int32Value();
		else if (a->eta == v8::kExternalUnsignedIntArray) for (i = 0; i < array->Length(); ++i) ((uint32_t*)a->a)[pos + i] = array->Get(v8::Integer::New(i))->Uint32Value();
		else if (a->eta == v8::kExternalShortArray) for (i = 0; i < array->Length(); ++i) ((int16_t*)a->a)[pos + i] = array->Get(v8::Integer::New(i))->Int32Value();
		else if (a->eta == v8::kExternalUnsignedShortArray) for (i = 0; i < array->Length(); ++i) ((uint16_t*)a->a)[pos + i] = array->Get(v8::Integer::New(i))->Uint32Value();
	} else if (args[0]->IsObject()) {
		v8::Handle<v8::Object> b = v8::Handle<v8::Object>::Cast(args[0]); // TODO: check b is a 'Bytes' instance
		kvec8_t *a2 = reinterpret_cast<kvec8_t*>(b->GetAlignedPointerFromInternalField(0));
		_extend_vec_(a2->n);
		memcpy(a->a + (pos << a->tshift), a2->a, a2->n);
	}
	return scope.Close(v8::Integer::New(cnt));
}

JS_METHOD(k8_bytes_toString, args)
{
	v8::HandleScope scope;
	kvec8_t *a = LOAD_PTR(args, 0, kvec8_t*);
	return scope.Close(v8::String::New((char*)a->a, a->n));
}

v8::Handle<v8::Value> k8_bytes_length_getter(v8::Local<v8::String> property, const v8::AccessorInfo &info)
{
	kvec8_t *a = LOAD_PTR(info, 0, kvec8_t*);
	return v8::Integer::New(a->n >> a->tshift);
}

void k8_bytes_length_setter(v8::Local<v8::String> property, v8::Local<v8::Value> value, const v8::AccessorInfo &info)
{
	kvec8_t *a = LOAD_PTR(info, 0, kvec8_t*);
	int32_t n_old = a->n;
	a->n = value->Int32Value() << a->tshift;
	if (a->n > a->m) kv_recapacity(a, a->n);
	if (n_old != a->n) set_length(info.This(), a);
}

v8::Handle<v8::Value> k8_bytes_capacity_getter(v8::Local<v8::String> property, const v8::AccessorInfo &info)
{
	kvec8_t *a = LOAD_PTR(info, 0, kvec8_t*);
	return v8::Integer::New(a->m >> a->tshift);
}

void k8_bytes_capacity_setter(v8::Local<v8::String> property, v8::Local<v8::Value> value, const v8::AccessorInfo &info)
{
	kvec8_t *a = LOAD_PTR(info, 0, kvec8_t*);
	int32_t n_old = a->n;
	kv_recapacity(a, value->Int32Value() << a->tshift);
	if (n_old != a->n) set_length(info.This(), a);
}

/**********************************************
 *** Generic stream buffer from klib/kseq.h ***
 **********************************************/

#define KS_SEP_SPACE 0 // isspace(): \t, \n, \v, \f, \r
#define KS_SEP_TAB   1 // isspace() && !' '
#define KS_SEP_LINE  2 // line separator: " \n" (Unix) or "\r\n" (Windows)
#define KS_SEP_MAX   2

typedef struct __kstream_t {
	unsigned char *buf;
	int begin, end, is_eof, buf_size;
} kstream_t;

#define ks_eof(ks) ((ks)->is_eof && (ks)->begin >= (ks)->end)
#define ks_rewind(ks) ((ks)->is_eof = (ks)->begin = (ks)->end = 0)

static inline kstream_t *ks_init(int __bufsize)
{
	kstream_t *ks = (kstream_t*)calloc(1, sizeof(kstream_t));
	ks->buf_size = __bufsize;
	ks->buf = (unsigned char*)malloc(__bufsize);
	return ks;
}

static inline void ks_destroy(kstream_t *ks)
{ 
	if (ks) { free(ks->buf); free(ks); }
}

template<typename file_t, typename reader_t>
static int ks_getc(file_t &fp, kstream_t *ks, reader_t reader)
{
	if (ks->is_eof && ks->begin >= ks->end) return -1;
	if (ks->begin >= ks->end) {
		ks->begin = 0;
		ks->end = reader(fp, ks->buf, ks->buf_size);
		if (ks->end < ks->buf_size) ks->is_eof = 1;
		if (ks->end == 0) return -1;
	}
	return (int)ks->buf[ks->begin++];
}

template<typename file_t, typename reader_t>
static size_t ks_read(file_t &fp, kstream_t *ks, uint8_t *buf, long len, reader_t reader)
{
	long offset = 0;
	if (ks->is_eof && ks->begin >= ks->end) return -1;
	while (len > ks->end - ks->begin) {
		int l = ks->end - ks->begin;
		memcpy(buf + offset, ks->buf + ks->begin, l);
		len -= l; offset += l;
		ks->begin = 0;
		ks->end = reader(fp, ks->buf, ks->buf_size);
		if (ks->end < ks->buf_size) ks->is_eof = 1;
		if (ks->end == 0) return offset;
	}
	memcpy(buf + offset, ks->buf + ks->begin, len);
	ks->begin += len;
	return offset + len;
}

template<typename file_t, typename reader_t>
static int ks_getuntil(file_t &fp, kstream_t *ks, kvec8_t *kv, int delimiter, int *dret, int offset, reader_t reader)
{
	int gotany = 0;
	if (dret) *dret = 0;
	kv->n = offset >= 0? offset : 0;
	if (ks->begin >= ks->end && ks->is_eof) return -1;
	for (;;) {
		int i;
		if (ks->begin >= ks->end) {
			if (!ks->is_eof) {
				ks->begin = 0;
				ks->end = reader(fp, ks->buf, ks->buf_size);
				if (ks->end == 0) { ks->is_eof = 1; break; }
				if (ks->end < 0)  { ks->is_eof = 1; return -3; }
			} else break;
		}
		if (delimiter == KS_SEP_LINE) {
			for (i = ks->begin; i < ks->end; ++i)
				if (ks->buf[i] == '\n') break;
		} else if (delimiter > KS_SEP_MAX) {
			for (i = ks->begin; i < ks->end; ++i)
				if (ks->buf[i] == delimiter) break;
		} else if (delimiter == KS_SEP_SPACE) {
			for (i = ks->begin; i < ks->end; ++i)
				if (isspace(ks->buf[i])) break;
		} else if (delimiter == KS_SEP_TAB) {
			for (i = ks->begin; i < ks->end; ++i)
				if (isspace(ks->buf[i]) && ks->buf[i] != ' ') break;
		} else i = 0; /* never come to here! */
		if (kv->m - kv->n < i - ks->begin + 1) {
			kv->m = kv->n + (i - ks->begin) + 1;
			kroundup32(kv->m);
			kv->a = (uint8_t*)realloc(kv->a, kv->m);
		}
		gotany = 1;
		memcpy(kv->a + kv->n, ks->buf + ks->begin, i - ks->begin);
		kv->n = kv->n + (i - ks->begin);
		ks->begin = i + 1;
		if (i < ks->end) {
			if (dret) *dret = ks->buf[i];
			break;
		}
	}
	if (!gotany && ks_eof(ks)) return -1;
	if (kv->a == 0) {
		kv->m = 1;
		kv->a = (uint8_t*)calloc(1, 1);
	} else if (delimiter == KS_SEP_LINE && kv->n > 1 && kv->a[kv->n-1] == '\r') --kv->n;
	kv->a[kv->n] = '\0';
	return kv->n;
}

#define KS_BUF_SIZE 0x10000

/*******************
 *** File object ***
 *******************/

JS_METHOD(k8_file, args) // new File(fileName=stdin, mode="r").
{
	v8::HandleScope scope;
	ASSERT_CONSTRUCTOR(args);
	int fd = -1;
	FILE *fpw = 0;  // write ordinary files
	gzFile fpr = 0; // zlib transparently reads both ordinary and zlib/gzip files
	if (args.Length()) {
		SAVE_VALUE(args, 0, args[0]); // InternalField[0] keeps the file name
		v8::String::AsciiValue file(args[0]);
		if (args[0]->IsUint32()) fd = args[0]->Int32Value();
		if (args.Length() >= 2) {
			SAVE_VALUE(args, 1, args[1]); // InternalField[1] keeps the mode
			v8::String::AsciiValue mode(args[1]);
			const char *cstr = k8_cstr(mode);
			if (strchr(cstr, 'w')) fpw = fd >= 0? fdopen(fd, cstr) : fopen(k8_cstr(file), cstr);
			else fpr = fd >= 0? gzdopen(fd, cstr) : gzopen(k8_cstr(file), cstr);
		} else {
			SAVE_VALUE(args, 1, JS_STR("r"));
			fpr = fd >= 0? gzdopen(fd, "r") : gzopen(*file, "r");
		}
		if (fpr == 0 && fpw == 0)
			return JS_THROW(Error, "[File] Fail to open the file");
	} else {
		SAVE_VALUE(args, 0, JS_STR("-"));
		SAVE_VALUE(args, 1, JS_STR("r"));
		fpr = gzdopen(fileno(stdin), "r");
	}
	SAVE_PTR(args, 2, fpr); // InternalField[2] keeps the reading file pointer
	SAVE_PTR(args, 3, fpw); // InternalField[3] keeps the writing file pointer
	if (fpr) {
		kstream_t *ks = ks_init(KS_BUF_SIZE);
		v8::V8::AdjustAmountOfExternalAllocatedMemory(KS_BUF_SIZE);
		SAVE_PTR(args, 4, ks);
	} else SAVE_PTR(args, 4, 0);
	return args.This();
}

JS_METHOD(k8_file_close, args) // File::close(). Close the file.
{
	gzFile fpr = LOAD_PTR(args, 2, gzFile);
	FILE  *fpw = LOAD_PTR(args, 3, FILE*);
	if (fpr) {
		gzclose(fpr);
		kstream_t *ks = LOAD_PTR(args, 4, kstream_t*);
		ks_destroy(ks);
		v8::V8::AdjustAmountOfExternalAllocatedMemory(-KS_BUF_SIZE);
	}
	if (fpw) fclose(fpw);
	SAVE_PTR(args, 2, 0); SAVE_PTR(args, 3, 0); SAVE_PTR(args, 4, 0);
	return v8::Undefined();
}

JS_METHOD(k8_file_read, args) // File::read(), read(buf, offset, length)
{
	v8::HandleScope scope;
	gzFile fp = LOAD_PTR(args, 2, gzFile);
	kstream_t *ks = LOAD_PTR(args, 4, kstream_t*);
	if (fp == 0) return JS_ERROR("file is not open for read");
	if (args.Length() == 0) { // read()
		int c = ks_getc(fp, ks, gzread);
		return scope.Close(v8::Integer::New(c));
	} else if (args.Length() == 3 && args[0]->IsObject() && args[1]->IsUint32() && args[2]->IsUint32()) { // read(buf, offset, length)
		long off = args[1]->Int32Value(), len = args[2]->Int32Value();
		v8::Handle<v8::Object> b = v8::Handle<v8::Object>::Cast(args[0]); // TODO: check b is a 'Bytes' instance
		kvec8_t *kv = reinterpret_cast<kvec8_t*>(b->GetAlignedPointerFromInternalField(0));
		if (len + off > kv->m) kv_recapacity(kv, len + off);
		len = ks_read(fp, ks, kv->a + off, len, gzread);
		if (len + off > kv->n) {
			kv->n = len + off;
			set_length(b, kv);
		}
		return scope.Close(v8::Integer::New(len));
	}
	return JS_ERROR("misused File.prototype.read()");
}

JS_METHOD(k8_file_write, args) // File::write(str). Write $str and return the number of written characters
{
	v8::HandleScope scope;
	FILE *fp = LOAD_PTR(args, 3, FILE*);
	if (fp == 0) return JS_ERROR("file is not open for write");
	if (args.Length() == 0) return scope.Close(v8::Integer::New(0));
	long len = 0;
	if (args[0]->IsString()) {
		v8::String::AsciiValue vbuf(args[0]);
		len = fwrite(*vbuf, 1, vbuf.length(), fp);
	} else if (args[0]->IsObject()) {
		v8::Handle<v8::Object> b = v8::Handle<v8::Object>::Cast(args[0]); // TODO: check b is a 'Bytes' instance
		kvec8_t *kv = reinterpret_cast<kvec8_t*>(b->GetAlignedPointerFromInternalField(0));
		len = fwrite(kv->a, 1, kv->n, fp);
	}
	return scope.Close(v8::Integer::New(len));
}

JS_METHOD(k8_file_readline, args) // see iStream::readline(sep=line) for details
{
	v8::HandleScope scope;
	gzFile fpr = LOAD_PTR(args, 2, gzFile);
	kstream_t *ks = LOAD_PTR(args, 4, kstream_t*);
	if (fpr == 0) return JS_ERROR("file is not open for read");
	if (!args.Length() || !args[0]->IsObject()) return v8::Null(); // TODO: when there are no parameters, skip a line
	v8::Handle<v8::Object> b = v8::Handle<v8::Object>::Cast(args[0]); // TODO: check b is a 'Bytes' instance
	kvec8_t *kv = reinterpret_cast<kvec8_t*>(b->GetAlignedPointerFromInternalField(0));
	int dret, ret, sep = KS_SEP_LINE;
	if (args.Length() > 1) { // by default, the delimitor is new line
		if (args[1]->IsString()) { // if 1st argument is a string, set the delimitor to the 1st charactor of the string
			v8::String::AsciiValue str(args[1]);
			sep = int(k8_cstr(str)[0]);
		} else if (args[1]->IsInt32()) // if 1st argument is an integer, set the delimitor to the integer: 0=>isspace(); 1=>isspace()&&!' '; 2=>newline
			sep = args[1]->Int32Value();
	}
	int offset = 0;
	if (args.Length() > 2) {
		if (args[2]->IsUint32()) offset = args[2]->Uint32Value();
		else if (args[2]->IsBoolean()) offset = args[2]->BooleanValue()? kv->n : 0;
	}
	ret = ks_getuntil(fpr, ks, kv, sep, &dret, offset, gzread);
	set_length(b, kv);
	return ret >= 0? scope.Close(v8::Integer::New(dret)) : scope.Close(v8::Integer::New(ret));
}

/******************
 *** Set object ***
 ******************/

#include "khash.h"
KHASH_MAP_INIT_STR(str, kh_cstr_t)
typedef khash_t(str) *strset_t;

static const char *k8_empty_str = "";

JS_METHOD(k8_map, args)
{
	v8::HandleScope scope;
	ASSERT_CONSTRUCTOR(args);
	strset_t h = kh_init(str);
	SAVE_PTR(args, 0, h);
	return args.This();
}

JS_METHOD(k8_map_put, args)
{
	v8::HandleScope scope;
	strset_t h = LOAD_PTR(args, 0, strset_t);
	if (args.Length()) {
		v8::String::AsciiValue s(args[0]);
		const char *cstr = k8_cstr(s);
		int absent;
		khint_t k = kh_put(str, h, cstr, &absent);
		if (absent) {
			kh_key(h, k) = strdup(cstr);
			kh_val(h, k) = k8_empty_str;
		}
		if (args.Length() > 1) {
			v8::String::AsciiValue v(args[1]);
			if (kh_val(h, k) != k8_empty_str)
				free((char*)kh_val(h, k));
			kh_val(h, k) = strdup(k8_cstr(v));
		}
	} else return JS_ERROR("misused Map.prototype.put()");
	return v8::Undefined();
}

JS_METHOD(k8_map_get, args)
{
	v8::HandleScope scope;
	strset_t h = LOAD_PTR(args, 0, strset_t);
	if (args.Length()) {
		v8::String::AsciiValue s(args[0]);
		const char *cstr = k8_cstr(s);
		khint_t k = kh_get(str, h, cstr);
		return k == kh_end(h)? v8::Null() : scope.Close(v8::String::New(kh_val(h, k), strlen(kh_val(h, k))));
	} else return JS_ERROR("misused Map.prototype.get()");
}

JS_METHOD(k8_map_del, args)
{
	v8::HandleScope scope;
	strset_t h = LOAD_PTR(args, 0, strset_t);
	if (args.Length()) {
		v8::String::AsciiValue s(args[0]);
		const char *cstr = k8_cstr(s);
		khint_t k = kh_get(str, h, cstr);
		if (k < kh_end(h)) {
			free((char*)kh_key(h, k));
			if (kh_val(h, k) != k8_empty_str)
				free((char*)kh_val(h, k));
			kh_del(str, h, k);
		}
	} else return JS_ERROR("misused Map.prototype.del()");
	return v8::Undefined();
}

JS_METHOD(k8_map_destroy, args)
{
	v8::HandleScope scope;
	strset_t h = LOAD_PTR(args, 0, strset_t);
	khint_t k;
	for (k = 0; k != kh_end(h); ++k)
		if (kh_exist(h, k)) {
			free((char*)kh_key(h, k));
			if (kh_val(h, k) != k8_empty_str)
				free((char*)kh_val(h, k));
		}
	kh_destroy(str, h);
	SAVE_PTR(args, 0, 0);
	return v8::Undefined();
}
#endif
/***********************
 *** Getopt from BSD ***
 ***********************/

// Modified from getopt.c from BSD, 3-clause BSD license. Copyright (c) 1987-2002 The Regents of the University of California.
// We do not use system getopt() because it may parse "-v" in "k8 prog.js -v".

int opterr = 1, optind = 1, optopt, optreset;
char *optarg;

int getopt(int nargc, char * const *nargv, const char *ostr)
{
	static char *place = 0;
	const char *oli;
	if (optreset || !place || !*place) {
		optreset = 0;
		if (optind >= nargc || *(place = nargv[optind]) != '-') {
			place = 0;
			return -1;
		}
		if (place[1] && *++place == '-') {
			++optind, place = 0;
			return -1;
		}
	}
	if ((optopt = *place++) == ':' || !(oli = strchr(ostr, optopt))) {
		if (optopt == '-') return -1;
		if (!*place) ++optind;
		if (opterr && *ostr != ':') fprintf(stderr, "%s: illegal option -- %c\n", __FILE__, optopt);
		return '?';
	}
	if (*++oli != ':') {
		optarg = 0;
		if (!*place) ++optind;
	} else {
		if (*place) optarg = place;
		else if (nargc <= ++optind) {
			place = 0;
			if (*ostr == ':') return ':';
			if (opterr) fprintf(stderr, "%s: option requires an argument -- %c\n", __FILE__, optopt);
			return '?';
		} else optarg = nargv[optind];
		place = 0;
		++optind;
	}
	return optopt;
}

/*********************
 *** Main function ***
 *********************/

static v8::Local<v8::Context> k8_create_shell_context(v8::Isolate* isolate)
{
	v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);
	global->Set(isolate, "print", v8::FunctionTemplate::New(isolate, k8_print));
	global->Set(isolate, "warn", v8::FunctionTemplate::New(isolate, k8_warn));
	global->Set(isolate, "exit", v8::FunctionTemplate::New(isolate, k8_exit));
	global->Set(isolate, "load", v8::FunctionTemplate::New(isolate, k8_load));
	return v8::Context::New(isolate, NULL, global);
}

static int k8_main(v8::Isolate *isolate, v8::Platform *platform, v8::Local<v8::Context> &context, int argc, char* argv[])
{
	// parse command-line options
	int c;
	while ((c = getopt(argc, argv, "e:E:v")) >= 0) {
		if (c == 'e' || c == 'E') { // execute a string
			v8::Local<v8::String> file_name = v8::String::NewFromUtf8Literal(isolate, "unnamed");
			v8::Local<v8::String> source;
			if (!v8::String::NewFromUtf8(isolate, optarg).ToLocal(&source))
				return 1;
			bool success = k8_execute(isolate, source, file_name, (c == 'E'));
			while (v8::platform::PumpMessageLoop(platform, isolate)) continue;
			return success? 0 : 1;
		} else if (c == 'v') {
			printf("V8: %s\nK8: %s\n", v8::V8::GetVersion(), K8_VERSION);
			return 0;
		} else {
			fprintf(stderr, "ERROR: unrecognized option\n");
			return 1;
		}
	}
	if (optind == argc) {
		fprintf(stderr, "Usage: k8 [options] <script.js> [arguments]\n");
		fprintf(stderr, "Options:\n");
		fprintf(stderr, "  -e STR      execute STR\n");
		fprintf(stderr, "  -E STR      execute STR and print results\n");
		fprintf(stderr, "  -v          print version number\n");
		return 0;
	}

	// pass command-line arguments though the "arguments" array
	v8::Local<v8::Array> args = v8::Array::New(isolate, argc - optind - 1);
	for (int i = optind + 1; i < argc; ++i) {
		v8::Local<v8::String> arg = v8::String::NewFromUtf8(isolate, argv[i]).ToLocalChecked();
		v8::Local<v8::Number> index = v8::Number::New(isolate, i - optind - 1);
		args->Set(context, index, arg).FromJust();
	}
    v8::Local<v8::String> name = v8::String::NewFromUtf8Literal(isolate, "arguments", v8::NewStringType::kInternalized);
	context->Global()->Set(context, name, args).FromJust();

	// load and evaluate the source file
	v8::Local<v8::String> file_name = v8::String::NewFromUtf8(isolate, argv[optind]).ToLocalChecked();
	v8::Local<v8::String> source;
	if (!k8_readfile(isolate, argv[optind]).ToLocal(&source)) {
		fprintf(stderr, "ERROR: failed to read file '%s'\n", argv[optind]);
		return 1;
	}
	bool success = k8_execute(isolate, source, file_name, false);
	while (v8::platform::PumpMessageLoop(platform, isolate)) continue;
	return success? 0 : 1;
}

int main(int argc, char* argv[])
{
	int c, ret = 0;
	v8::V8::InitializeICUDefaultLocation(argv[0]);
	v8::V8::InitializeExternalStartupData(argv[0]);
	std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
	v8::V8::InitializePlatform(platform.get());
	v8::V8::SetFlagsFromCommandLine(&argc, argv, true);
	v8::V8::Initialize();
	v8::Isolate::CreateParams create_params;
	create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
	v8::Isolate* isolate = v8::Isolate::New(create_params);
	{
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Local<v8::Context> context = k8_create_shell_context(isolate);
		if (context.IsEmpty()) {
			fprintf(stderr, "Error creating context\n");
			return 1;
		}
		v8::Context::Scope context_scope(context);
		ret = k8_main(isolate, platform.get(), context, argc, argv);
	}
	isolate->Dispose();
	v8::V8::Dispose();
	v8::V8::DisposePlatform();
	delete create_params.array_buffer_allocator;
	return ret;
}
