/* The MIT License

   Copyright (c) 2023 by Heng Li <lh3@me.com>

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
#define K8_VERSION "0.3.0-r91-dirty"

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
#include "include/v8-external.h"
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

static void k8_version(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	args.GetReturnValue().Set(v8::String::NewFromUtf8(args.GetIsolate(), K8_VERSION).ToLocalChecked());
}

/****************
 * File reading *
 ****************/

#define KS_SEP_LINE  (-1)
#define KS_SEP_SPACE (-2)

#define K8_CALLOC(type, cnt) ((type*)calloc((cnt), sizeof(type)))
#define K8_REALLOC(type, ptr, cnt) ((type*)realloc((ptr), (cnt) * sizeof(type)))

#define K8_GROW(type, ptr, __i, __m) do { \
		if ((__i) >= (__m)) { \
			(__m) = (__i) + 1; \
			(__m) += ((__m)>>1) + 16; \
			(ptr) = K8_REALLOC(type, ptr, (__m)); \
		} \
	} while (0)

#define K8_GROW0(type, ptr, __i, __m) do { \
		if ((__i) >= (__m)) { \
			size_t old_m = (__m); \
			(__m) = (__i) + 1; \
			(__m) += ((__m)>>1) + 16; \
			(ptr) = K8_REALLOC(type, ptr, (__m)); \
			memset((ptr) + old_m, 0, ((__m) - old_m) * sizeof(type)); \
		} \
	} while (0)

typedef struct {
	int64_t l, m;
	uint8_t *s;
} kstring_t;

typedef struct {
	gzFile fp;
	FILE *fpw;
	int32_t st, en, buf_size, enc, last_char;
	int32_t is_eof:16, is_fastq:16;
	uint8_t *buf;
	kstring_t str, name, seq, qual, comment;
} k8_file_t;

#define ks_err(ks) ((ks)->en < 0)
#define ks_eof(ks) ((ks)->is_eof && (ks)->st >= (ks)->en)

static k8_file_t *ks_open(const char *fn, bool write_file)
{
	gzFile fp = 0;
	FILE *fpw = 0;
	if (fn) {
		if (write_file) fpw = strcmp(fn, "-")? fopen(fn, "wb") : stdout;
		else fp = strcmp(fn, "-")? gzopen(fn, "r") : gzdopen(0, "r");
	} else {
		if (write_file) fpw = stdout;
		else fp = gzdopen(0, "r");
	}
	if (fp == 0 && fpw == 0) return 0;
	k8_file_t *ks = K8_CALLOC(k8_file_t, 1);
	ks->fp = fp, ks->fpw = fpw;
	if (fp) {
		ks->buf_size = 0x40000;
		ks->buf = K8_CALLOC(uint8_t, ks->buf_size);
	}
	return ks;
}

static inline int32_t ks_getc(k8_file_t *ks)
{
	if (ks_err(ks)) return -3;
	if (ks_eof(ks)) return -1;
	if (ks->st >= ks->en) {
		ks->st = 0;
		ks->en = gzread(ks->fp, ks->buf, ks->buf_size);
		if (ks->en == 0) { ks->is_eof = 1; return -1; }
		else if (ks->en < 0) { ks->is_eof = 1; return -3; }
	}
	return (int32_t)ks->buf[ks->st++];
}

static int64_t ks_read(k8_file_t *ks, uint8_t *buf, int64_t len)
{
	int64_t off = 0;
	if (ks->is_eof && ks->st >= ks->en) return -1;
	while (len > ks->en - ks->st) {
		int l = ks->en - ks->st;
		memcpy(buf + off, ks->buf + ks->st, l);
		len -= l; off += l;
		ks->st = 0;
		ks->en = gzread(ks->fp, ks->buf, ks->buf_size);
		if (ks->en < ks->buf_size) ks->is_eof = 1;
		if (ks->en == 0) return off;
	}
	memcpy(buf + off, ks->buf + ks->st, len);
	ks->st += len;
	return off + len;
}

static int64_t ks_getuntil2(k8_file_t *ks, int delimiter, kstring_t *str, int *dret, int appen)
{
	int gotany = 0;
	if (dret) *dret = 0;
	str->l = appen? str->l : 0;
	for (;;) {
		int i;
		if (ks_err(ks)) return -3;
		if (ks->st >= ks->en) {
			if (!ks->is_eof) {
				ks->st = 0;
				ks->en = gzread(ks->fp, ks->buf, ks->buf_size);
				if (ks->en == 0) { ks->is_eof = 1; break; }
				if (ks->en == -1) { ks->is_eof = 1; return -3; }
			} else break;
		}
		if (delimiter == KS_SEP_LINE) {
			unsigned char *sep = (unsigned char*)memchr(ks->buf + ks->st, '\n', ks->en - ks->st);
			i = sep != NULL ? sep - ks->buf : ks->en;
		} else if (delimiter == KS_SEP_SPACE) {
			for (i = ks->st; i < ks->en; ++i)
				if (ks->buf[i] == ' ' || ks->buf[i] == '\t' || ks->buf[i] == '\n') break;
		} else if (delimiter > 0) {
			for (i = ks->st; i < ks->en; ++i)
				if (ks->buf[i] == delimiter) break;
		} else i = 0; /* never come to here! */
		K8_GROW(uint8_t, str->s, str->l + (i - ks->st), str->m);
		gotany = 1;
		memcpy(str->s + str->l, ks->buf + ks->st, i - ks->st);
		str->l = str->l + (i - ks->st);
		ks->st = i + 1;
		if (i < ks->en) {
			if (dret) *dret = ks->buf[i];
			break;
		}
	}
	if (!gotany && ks_eof(ks)) return -1;
	if (str->s == 0) {
		str->m = 1;
		str->s = K8_CALLOC(uint8_t, 1);
	} else if (delimiter == KS_SEP_LINE && str->l > 1 && str->s[str->l-1] == '\r') {
		--str->l;
	}
	str->s[str->l] = '\0';
	return str->l;
}

static int64_t ks_readfastx(k8_file_t *ks)
{
	int c, r;
	if (ks->last_char == 0) { /* then jump to the next header line */
		while ((c = ks_getc(ks)) >= 0 && c != '>' && c != '@');
		if (c < 0) return c; /* end of file or error*/
		ks->last_char = c;
	} /* else: the first header char has been read in the previous call */
	ks->comment.l = ks->seq.l = ks->qual.l = 0; /* reset all members */
	if ((r=ks_getuntil2(ks, -2, &ks->name, &c, 0)) < 0) return r;  /* normal exit: EOF or error */
	if (c != '\n') ks_getuntil2(ks, -1, &ks->comment, 0, 0); /* read FASTA/Q comment */
	if (ks->seq.s == 0) { /* we can do this in the loop below, but that is slower */
		ks->seq.m = 256;
		ks->seq.s = (uint8_t*)malloc(ks->seq.m);
	}
	while ((c = ks_getc(ks)) >= 0 && c != '>' && c != '+' && c != '@') {
		if (c == '\n') continue; /* skip empty lines */
		ks->seq.s[ks->seq.l++] = c; /* this is safe: we always have enough space for 1 char */
		ks_getuntil2(ks, -1, &ks->seq, 0, 1); /* read the rest of the line */
	}
	if (c == '>' || c == '@') ks->last_char = c; /* the first header char has been read */
	K8_GROW(uint8_t, ks->seq.s, ks->seq.l, ks->seq.m); // ks->seq.s[ks->seq.l] below may be out of boundary
	ks->seq.s[ks->seq.l] = 0;	/* null terminated string */
	ks->is_fastq = (c == '+');
	if (!ks->is_fastq) return ks->seq.l; /* FASTA */
	if (ks->qual.m < ks->seq.m) {	/* allocate memory for qual in case insufficient */
		ks->qual.m = ks->seq.m;
		ks->qual.s = (uint8_t*)realloc(ks->qual.s, ks->qual.m);
	}
	while ((c = ks_getc(ks)) >= 0 && c != '\n'); /* skip the rest of '+' line */
	if (c == -1) return -2; /* error: no quality string */
	while ((c = ks_getuntil2(ks, -1, &ks->qual, 0, 1) >= 0 && ks->qual.l < ks->seq.l));
	if (c == -3) return -3; /* stream error */
	ks->last_char = 0;	/* we have not come to the next header line */
	if (ks->seq.l != ks->qual.l) return -2; /* error: qual string is of a different length */
	return ks->seq.l;
}

/*****************************
 * K8 file reading functions *
 *****************************/

static void k8_open(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::Isolate *isolate = args.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	bool write_file = args.Length() >= 2? args[1]->BooleanValue(isolate) : false;
	k8_file_t *ks = 0;
	if (args.Length() > 0) {
		v8::String::Utf8Value str(isolate, args[0]);
		const char *fn = k8_cstr(str);
		ks = ks_open(fn, write_file);
	} else ks = ks_open(0, false);
	if (ks) {
		ks->enc = args.Length() >= 3? args[2]->Int32Value(isolate->GetCurrentContext()).FromMaybe(0) : 1;
		args.GetReturnValue().Set(v8::External::New(isolate, ks));
	} else {
		isolate->ThrowError("k8_open: failed to open file");
		args.GetReturnValue().SetNull();
	}
}

static void k8_close(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::HandleScope handle_scope(args.GetIsolate());
	if (args.Length() == 0 || !args[0]->IsExternal()) return;
	k8_file_t *ks = (k8_file_t*)args[0].As<v8::External>()->Value();
	if (ks->fp) gzclose(ks->fp);
	if (ks->fpw) fclose(ks->fpw);
	free(ks->buf);
	free(ks->str.s); free(ks->seq.s); free(ks->qual.s); free(ks->name.s); free(ks->comment.s);
	free(ks);
}

static void k8_write(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::HandleScope handle_scope(args.GetIsolate());
	if (args.Length() < 2 || !args[0]->IsExternal()) return;
	k8_file_t *ks = (k8_file_t*)args[0].As<v8::External>()->Value();
	if (ks->fpw == 0) {
		args.GetReturnValue().Set(-1);
		return;
	} else if (args[1]->IsArrayBuffer()) {
		void *data = args[1].As<v8::ArrayBuffer>()->GetBackingStore()->Data();
		int64_t len = args[1].As<v8::ArrayBuffer>()->GetBackingStore()->ByteLength();
		assert(len >= 0 && len < INT32_MAX);
		fwrite(data, 1, len, ks->fpw);
		args.GetReturnValue().Set((int32_t)len);
	} else if (args[1]->IsString()) {
		v8::String::Utf8Value str(args.GetIsolate(), args[1]);
		fwrite(*str, 1, str.length(), ks->fpw);
		args.GetReturnValue().Set((int32_t)str.length());
	}
}

static void k8_getc(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::HandleScope handle_scope(args.GetIsolate());
	if (args.Length() == 0 || !args[0]->IsExternal()) {
		args.GetReturnValue().Set(-1);
	} else {
		k8_file_t *ks = (k8_file_t*)args[0].As<v8::External>()->Value();
		if (ks->fp == 0) {
			args.GetReturnValue().Set(-1);
		} else {
			int32_t c = ks_getc(ks);
			args.GetReturnValue().Set(c);
		}
	}
}

static void k8_ext_delete_cb(void *data, size_t len, void *aux) // do nothing
{
}

static void k8_set_ret(const v8::FunctionCallbackInfo<v8::Value> &args, k8_file_t *ks)
{
	if (ks->enc == 0) {
		args.GetReturnValue().Set(v8::ArrayBuffer::New(args.GetIsolate(), v8::ArrayBuffer::NewBackingStore((uint8_t*)ks->str.s, ks->str.l, k8_ext_delete_cb, 0)));
	} else {
		v8::Local<v8::String> str;
		if (ks->enc == 1) {
			if (!v8::String::NewFromOneByte(args.GetIsolate(), (uint8_t*)ks->str.s, v8::NewStringType::kNormal, ks->str.l).ToLocal(&str))
				args.GetReturnValue().SetNull();
			else args.GetReturnValue().Set(str);
		} else {
			if (!v8::String::NewFromUtf8(args.GetIsolate(), (char*)ks->str.s, v8::NewStringType::kNormal, ks->str.l).ToLocal(&str))
				args.GetReturnValue().SetNull();
			else args.GetReturnValue().Set(str);
		}
	}
}

static void k8_read(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::HandleScope handle_scope(args.GetIsolate());
	if (args.Length() < 2 || !args[0]->IsExternal()) {
		args.GetReturnValue().SetNull();
	} else {
		k8_file_t *ks = (k8_file_t*)args[0].As<v8::External>()->Value();
		if (ks->fp == 0) {
			args.GetReturnValue().SetNull();
		} else {
			int32_t sz = args[1]->Int32Value(args.GetIsolate()->GetCurrentContext()).FromMaybe(0);
			if (sz > ks->str.m) {
				ks->str.m = sz + (sz>>1) + 16;
				ks->str.s = K8_REALLOC(uint8_t, ks->str.s, ks->str.m);
			}
			ks->str.l = ks_read(ks, (uint8_t*)ks->str.s, sz);
			k8_set_ret(args, ks);
		}
	}
}

static void k8_readline(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::HandleScope handle_scope(args.GetIsolate());
	if (args.Length() == 0 || !args[0]->IsExternal()) {
		args.GetReturnValue().SetNull();
	} else {
		k8_file_t *ks = (k8_file_t*)args[0].As<v8::External>()->Value();
		if (ks->fp == 0) {
			args.GetReturnValue().SetNull();
		} else {
			int32_t sep = args.Length() >= 2? args[1]->Int32Value(args.GetIsolate()->GetCurrentContext()).FromMaybe(0) : -1;
			int64_t ret = ks_getuntil2(ks, sep, &ks->str, 0, 0);
			if (ret >= 0) k8_set_ret(args, ks);
			else args.GetReturnValue().SetNull();
		}
	}
}

static void k8_readfastx(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::HandleScope handle_scope(args.GetIsolate());
	if (args.Length() == 0 || !args[0]->IsExternal()) {
		args.GetReturnValue().SetNull();
	} else {
		k8_file_t *ks = (k8_file_t*)args[0].As<v8::External>()->Value();
		if (ks->fp == 0) {
			args.GetReturnValue().SetNull();
		} else {
			v8::Isolate *isolate = args.GetIsolate();
			int64_t ret = ks_readfastx(ks);
			if (ret >= 0) {
				v8::Local<v8::Context> context(isolate->GetCurrentContext());
				v8::Local<v8::Array> a = v8::Array::New(isolate, 4);
				a->Set(context, v8::Number::New(isolate, 0), v8::String::NewFromOneByte(isolate, (uint8_t*)ks->name.s, v8::NewStringType::kNormal, ks->name.l).ToLocalChecked()).FromJust();
				a->Set(context, v8::Number::New(isolate, 1), v8::String::NewFromOneByte(isolate, (uint8_t*)ks->seq.s, v8::NewStringType::kNormal, ks->seq.l).ToLocalChecked()).FromJust();
				a->Set(context, v8::Number::New(isolate, 2), v8::String::NewFromOneByte(isolate, (uint8_t*)ks->qual.s, v8::NewStringType::kNormal, ks->qual.l).ToLocalChecked()).FromJust();
				a->Set(context, v8::Number::New(isolate, 3), v8::String::NewFromOneByte(isolate, (uint8_t*)ks->comment.s, v8::NewStringType::kNormal, ks->comment.l).ToLocalChecked()).FromJust();
				args.GetReturnValue().Set(a);
			} else args.GetReturnValue().SetNull();
		}
	}
}

/*******************
 * The Bytes class *
 *******************/

#define K8_SAVE_PTR(_args, _index, _ptr)  (_args).This()->SetAlignedPointerInInternalField(_index, (void*)(_ptr))
#define K8_LOAD_PTR(_args, _index) (_args).This()->GetAlignedPointerFromInternalField(_index)

typedef struct {
	kstring_t buf;
} k8_bytes_t;

static void k8_bytes_new(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::HandleScope handle_scope(args.GetIsolate());
	k8_bytes_t *a = K8_CALLOC(k8_bytes_t, 1);
	if (args.Length()) {
		a->buf.m = a->buf.l = args[0]->Int32Value(args.GetIsolate()->GetCurrentContext()).FromMaybe(0);
		a->buf.s = K8_CALLOC(uint8_t, a->buf.l);
	}
	K8_SAVE_PTR(args, 0, a);
}

static void k8_bytes_destroy(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::HandleScope handle_scope(args.GetIsolate());
	k8_bytes_t *a = (k8_bytes_t*)K8_LOAD_PTR(args, 0);
	free(a->buf.s); free(a);
	args.GetReturnValue().Set(0);
}

static void k8_bytes_set(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::Isolate *isolate = args.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	if (args.Length() == 0) {
		args.GetReturnValue().Set(-1);
	} else {
		k8_bytes_t *a = (k8_bytes_t*)K8_LOAD_PTR(args, 0);
		int64_t pre = a->buf.l;
		int32_t off = args.Length() >= 2? args[1]->Int32Value(isolate->GetCurrentContext()).FromMaybe(0) : a->buf.l;
		if (args[0]->IsNumber()) {
			K8_GROW0(uint8_t, a->buf.s, off, a->buf.m);
			a->buf.s[off] = (uint8_t)args[0]->Int32Value(isolate->GetCurrentContext()).FromMaybe(0);
			a->buf.l = off + 1;
		} else if (args[0]->IsString()) {
			v8::String::Utf8Value str(isolate, args[0]);
			K8_GROW0(uint8_t, a->buf.s, off + str.length(), a->buf.m);
			memcpy(&a->buf.s[off], *str, str.length());
			a->buf.l = off + str.length();
		}
		args.GetReturnValue().Set((int32_t)(a->buf.l - pre));
	}
}

static void k8_bytes_toString(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::Isolate *isolate = args.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	k8_bytes_t *a = (k8_bytes_t*)K8_LOAD_PTR(args, 0);
	bool utf8 = args.Length() >= 2? args[1]->BooleanValue(isolate) : false;
	v8::Local<v8::String> str;
	if (!utf8) {
		if (!v8::String::NewFromOneByte(args.GetIsolate(), (uint8_t*)a->buf.s, v8::NewStringType::kNormal, a->buf.l).ToLocal(&str))
			args.GetReturnValue().SetNull();
		else args.GetReturnValue().Set(str);
	} else {
		if (!v8::String::NewFromUtf8(args.GetIsolate(), (char*)a->buf.s, v8::NewStringType::kNormal, a->buf.l).ToLocal(&str))
			args.GetReturnValue().SetNull();
		else args.GetReturnValue().Set(str);
	}
}

static void k8_bytes_length_getter(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value> &info)
{
	v8::HandleScope handle_scope(info.GetIsolate());
	k8_bytes_t *a = (k8_bytes_t*)K8_LOAD_PTR(info, 0);
	info.GetReturnValue().Set((int32_t)a->buf.l);
}

static void k8_bytes_length_setter(v8::Local<v8::String> property, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info)
{
	v8::HandleScope handle_scope(info.GetIsolate());
	k8_bytes_t *a = (k8_bytes_t*)K8_LOAD_PTR(info, 0);
	int32_t len = value->Int32Value(info.GetIsolate()->GetCurrentContext()).FromMaybe(0);
	if (len > a->buf.m) K8_GROW0(uint8_t, a->buf.s, len - 1, a->buf.m);
	a->buf.l = len;
}

static void k8_bytes_capacity_getter(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value> &info)
{
	v8::HandleScope handle_scope(info.GetIsolate());
	k8_bytes_t *a = (k8_bytes_t*)K8_LOAD_PTR(info, 0);
	info.GetReturnValue().Set((int32_t)a->buf.m);
}

static void k8_bytes_capacity_setter(v8::Local<v8::String> property, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info)
{
	v8::HandleScope handle_scope(info.GetIsolate());
	k8_bytes_t *a = (k8_bytes_t*)K8_LOAD_PTR(info, 0);
	int32_t len = value->Int32Value(info.GetIsolate()->GetCurrentContext()).FromMaybe(0);
	if (len < a->buf.l) len = a->buf.l;
	a->buf.m = len;
	a->buf.s = K8_REALLOC(uint8_t, a->buf.s, a->buf.m);
}

static void k8_bytes_buffer_getter(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value> &info)
{
	v8::Isolate *isolate = info.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	k8_bytes_t *a = (k8_bytes_t*)K8_LOAD_PTR(info, 0);
	info.GetReturnValue().Set(v8::ArrayBuffer::New(isolate, v8::ArrayBuffer::NewBackingStore((uint8_t*)a->buf.s, a->buf.l, k8_ext_delete_cb, 0)));
}

/******************
 * The File class *
 ******************/

static void k8_file_new(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::Isolate *isolate = args.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	bool write_file = false;
	if (args.Length() >= 2) {
		v8::String::Utf8Value str(isolate, args[0]);
		const char *mode = *str;
		write_file = strchr(mode, 'w')? true : false;
	}
	k8_file_t *ks = 0;
	if (args.Length() > 0) {
		v8::String::Utf8Value str(isolate, args[0]);
		const char *fn = k8_cstr(str);
		ks = ks_open(fn, write_file);
	} else ks = ks_open(0, false);
	if (ks == 0) {
		isolate->ThrowError("k8_open: failed to open file");
		args.GetReturnValue().SetNull();
	} else {
		K8_SAVE_PTR(args, 0, ks);
	}
}

static void k8_file_close(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::HandleScope handle_scope(args.GetIsolate());
	k8_file_t *ks = (k8_file_t*)K8_LOAD_PTR(args, 0);
	if (ks->fp) gzclose(ks->fp);
	if (ks->fpw) fclose(ks->fpw);
	free(ks);
	args.GetReturnValue().Set(0);
}

static void k8_file_readline(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::HandleScope handle_scope(args.GetIsolate());
	k8_file_t *ks = (k8_file_t*)K8_LOAD_PTR(args, 0);
	if (!args.Length() || !args[0]->IsObject()) {
		args.GetReturnValue().SetNull();
		return;
	}
	v8::Handle<v8::Object> b = v8::Handle<v8::Object>::Cast(args[0]); // TODO: check b is a 'Bytes' instance
	k8_bytes_t *a = (k8_bytes_t*)b->GetAlignedPointerFromInternalField(0);
	int32_t dret;
	int64_t ret = ks_getuntil2(ks, -1, &a->buf, &dret, 0);
	if (ret >= 0) args.GetReturnValue().Set(dret);
	else args.GetReturnValue().Set((int32_t)ret);
}

/***********************
 *** Getopt from BSD ***
 ***********************/

// Modified from getopt.c from BSD, 3-clause BSD license. Copyright (c) 1987-2002 The Regents of the University of California.
// We do not use the system getopt() because it may parse "-v" in "k8 prog.js -v".

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
	global->Set(isolate, "k8_version", v8::FunctionTemplate::New(isolate, k8_version));
	global->Set(isolate, "k8_open", v8::FunctionTemplate::New(isolate, k8_open));
	global->Set(isolate, "k8_close", v8::FunctionTemplate::New(isolate, k8_close));
	global->Set(isolate, "k8_write", v8::FunctionTemplate::New(isolate, k8_write));
	global->Set(isolate, "k8_getc", v8::FunctionTemplate::New(isolate, k8_getc));
	global->Set(isolate, "k8_read", v8::FunctionTemplate::New(isolate, k8_read));
	global->Set(isolate, "k8_readline", v8::FunctionTemplate::New(isolate, k8_readline));
	global->Set(isolate, "k8_readfastx", v8::FunctionTemplate::New(isolate, k8_readfastx));
	{ // add the 'Bytes' object
		v8::HandleScope scope(isolate);
		v8::Handle<v8::FunctionTemplate> ft = v8::FunctionTemplate::New(isolate, k8_bytes_new);
		ft->SetClassName(v8::String::NewFromUtf8Literal(isolate, "Bytes"));

		v8::Handle<v8::ObjectTemplate> ot = ft->InstanceTemplate();
		ot->SetInternalFieldCount(1);
		ot->SetAccessor(v8::String::NewFromUtf8Literal(isolate, "length"), k8_bytes_length_getter, k8_bytes_length_setter);
		ot->SetAccessor(v8::String::NewFromUtf8Literal(isolate, "capacity"), k8_bytes_capacity_getter, k8_bytes_capacity_setter);
		ot->SetAccessor(v8::String::NewFromUtf8Literal(isolate, "buffer"), k8_bytes_buffer_getter);

		v8::Handle<v8::ObjectTemplate> pt = ft->PrototypeTemplate();
		pt->Set(isolate, "destroy", v8::FunctionTemplate::New(isolate, k8_bytes_destroy));
		pt->Set(isolate, "set", v8::FunctionTemplate::New(isolate, k8_bytes_set));
		pt->Set(isolate, "toString", v8::FunctionTemplate::New(isolate, k8_bytes_toString));

		global->Set(isolate, "Bytes", ft);
	}
	{ // add the 'File' object
		v8::HandleScope scope(isolate);
		v8::Handle<v8::FunctionTemplate> ft = v8::FunctionTemplate::New(isolate, k8_file_new);
		ft->SetClassName(v8::String::NewFromUtf8Literal(isolate, "File"));

		v8::Handle<v8::ObjectTemplate> ot = ft->InstanceTemplate();
		ot->SetInternalFieldCount(1);

		v8::Handle<v8::ObjectTemplate> pt = ft->PrototypeTemplate();
		pt->Set(isolate, "readline", v8::FunctionTemplate::New(isolate, k8_file_readline));
		pt->Set(isolate, "close", v8::FunctionTemplate::New(isolate, k8_file_close));

		global->Set(isolate, "File", ft);
	}
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
	int ret = 0;
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
