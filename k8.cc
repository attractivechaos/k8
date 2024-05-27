/* The MIT License

   Copyright (c) 2018- Dana-Farber Cancer Institute
                 2011-2018 Broad Institute, Inc

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
#define K8_VERSION "1.1-r135-dirty"

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

/**************************
 *** Global definitions ***
 **************************/

#define K8_FILE_MAGIC  (0x46696c)
#define K8_BYTES_MAGIC (0x427974)

#define K8_MALLOC(type, cnt) ((type*)malloc((cnt) * sizeof(type)))
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

#ifdef PATH_MAX
#define K8_PATH_MAX PATH_MAX
#else
#define K8_PATH_MAX 4095
#endif

static char *k8_src_path = 0;

typedef struct {
	int64_t l, m;
	uint8_t *s;
} kstring_t;

typedef struct {
	uint64_t magic;
	kstring_t buf;
} k8_bytes_t;

/****************
 *** File I/O ***
 ****************/

#define KS_SEP_SPACE 0
#define KS_SEP_TAB   1
#define KS_SEP_LINE  2

typedef struct {
	uint64_t magic;
	gzFile fp;
	FILE *fpw;
	int32_t st, en, buf_size, enc, last_char;
	int32_t is_eof:16, is_fastq:16;
	uint8_t *buf;
} k8_file_t;

#define ks_err(ks) ((ks)->en < 0)
#define ks_eof(ks) ((ks)->is_eof && (ks)->st >= (ks)->en)

static k8_file_t *ks_open(int fd, const char *fn, const char *mode)
{
	gzFile fp = 0;
	FILE *fpw = 0;
	int32_t write_file = (mode && (strchr(mode, 'w') || strchr(mode, 'a')) && strchr(mode, 'r') == 0);
	if (fd >= 0) {
		if (write_file) fpw = fdopen(fd, mode);
		else fp = gzdopen(fd, "r");
	} else if (fn) {
		if (write_file) fpw = strcmp(fn, "-")? fopen(fn, mode) : stdout;
		else fp = strcmp(fn, "-")? gzopen(fn, "r") : gzdopen(0, "r");
	} else {
		if (write_file) fpw = stdout;
		else fp = gzdopen(0, "r");
	}
	if (fp == 0 && fpw == 0) return 0;
	k8_file_t *ks = K8_CALLOC(k8_file_t, 1);
	ks->magic = K8_FILE_MAGIC;
	ks->fp = fp, ks->fpw = fpw;
	if (fp) {
		ks->buf_size = 0x40000;
		ks->buf = K8_CALLOC(uint8_t, ks->buf_size);
	}
	return ks;
}

static void ks_close(k8_file_t *ks)
{
	if (ks == 0) return;
	if (ks->fp) gzclose(ks->fp);
	if (ks->fpw) fclose(ks->fpw);
	free(ks->buf);
	memset(ks, 0, sizeof(*ks));
	free(ks);
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
	if (ks->is_eof && ks->st >= ks->en) return 0;
	while (len > ks->en - ks->st) {
		int64_t l = ks->en - ks->st;
		if (l > 0) {
			memcpy(buf + off, ks->buf + ks->st, l);
			len -= l; off += l;
		}
		ks->st = 0;
		ks->en = gzread(ks->fp, ks->buf, ks->buf_size);
		if (ks->en < ks->buf_size) ks->is_eof = 1;
		if (ks->en == 0) return off;
		else if (ks->en < 0) return -3; // gzread error
	}
	memcpy(buf + off, ks->buf + ks->st, len);
	ks->st += len;
	return off + len;
}

static int64_t ks_read_all(k8_file_t *ks, kstring_t *str)
{
	str->l = 0;
	while (!ks_eof(ks)) {
		int64_t l = ks->en - ks->st;
		if (l > 0) {
			K8_GROW(uint8_t, str->s, str->l + l, str->m);
			memcpy(&str->s[str->l], &ks->buf[ks->st], l);
			str->l += l;
		}
		ks->st = 0;
		ks->en = gzread(ks->fp, ks->buf, ks->buf_size);
		if (ks->en < ks->buf_size) ks->is_eof = 1;
		if (ks->en <= 0) break;
	}
	str->s[str->l] = 0; // always enough room due to K8_GROW() is requesting on extra byte
	return str->l;
}

static int64_t ks_getuntil2(k8_file_t *ks, int delimiter, kstring_t *str, int *dret, int append)
{
	int gotany = 0;
	if (dret) *dret = 0;
	str->l = append? str->l : 0;
	for (;;) {
		int i = 0;
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
		} else if (delimiter == KS_SEP_TAB) {
			for (i = ks->st; i < ks->en; ++i)
				if (ks->buf[i] == '\t' || ks->buf[i] == '\n') break;
		} else if (delimiter > 0) {
			for (i = ks->st; i < ks->en; ++i)
				if (ks->buf[i] == delimiter) break;
		} else abort();
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

/*******************************
 *** Fundamental v8 routines ***
 *******************************/

#define K8_SAVE_PTR(_args, _index, _ptr)  (_args).This()->SetAlignedPointerInInternalField(_index, (void*)(_ptr))
#define K8_LOAD_PTR(_args, _index, _type) ((_type*)(_args).This()->GetAlignedPointerFromInternalField(_index))

static inline const char *k8_cstr(const v8::String::Utf8Value &str) // Convert a v8 string to C string
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
		// v8 didn't provide any extra information about this error; just print the exception.
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
	kstring_t str = {0,0,0};
	k8_file_t *ks = ks_open(-1, name, 0);
	if (ks == 0) {
		fprintf(stderr, "ERROR: fail to open file '%s'.\n", name);
		return v8::Handle<v8::String>();
	}
	ks_read_all(ks, &str);
	ks_close(ks);

	if (str.l > 2 && strncmp((char*)str.s, "#!", 2) == 0) { // then skip the "#!" line
		int64_t i;
		for (i = 0; i < str.l; ++i)
			if (str.s[i] == '\n') break;
		str.l -= i + 1;
		memmove(str.s, &str.s[i+1], str.l);
	}
	v8::MaybeLocal<v8::String> result = v8::String::NewFromUtf8(isolate, (char*)str.s, v8::NewStringType::kNormal, str.l);
	free(str.s);
	return result;
}

/******************************
 *** New built-in functions ***
 ******************************/

static void k8_write_string(FILE *fp, const v8::FunctionCallbackInfo<v8::Value> &args, int32_t i, kstring_t *buf)
{
	v8::HandleScope handle_scope(args.GetIsolate());
	if (args[i]->IsString()) {
		int64_t len = args[i].As<v8::String>()->Length();
		if (len > 0) {
			K8_GROW(uint8_t, buf->s, len, buf->m);
			args[i].As<v8::String>()->WriteOneByte(args.GetIsolate(), buf->s);
			fwrite(buf->s, 1, len, fp);
		}
		return;
	} else if (args[i]->IsArrayBuffer()) {
		void *data = args[i].As<v8::ArrayBuffer>()->GetBackingStore()->Data();
		int64_t len = args[i].As<v8::ArrayBuffer>()->GetBackingStore()->ByteLength();
		if (len > 0) fwrite(data, 1, len, fp);
		return;
	} else if (args[i]->IsObject()) {
		if (args[i].As<v8::Object>()->InternalFieldCount() > 0) {
			k8_bytes_t *a = (k8_bytes_t*)args[i].As<v8::Object>()->GetAlignedPointerFromInternalField(0);
			if (a && a->magic == K8_BYTES_MAGIC) {
				if (a->buf.l > 0) fwrite(a->buf.s, 1, a->buf.l, fp);
				return;
			}
		}
	}
	v8::String::Utf8Value str(args.GetIsolate(), args[i]);
	fputs(k8_cstr(str), fp);
}

static void k8_print(const v8::FunctionCallbackInfo<v8::Value> &args) // print(): print to stdout; TAB demilited if multiple arguments are provided
{
	kstring_t buf = {0,0,0};
	for (int32_t i = 0; i < args.Length(); i++) {
		if (i) fputc('\t', stdout);
		k8_write_string(stdout, args, i, &buf);
	}
	fputc('\n', stdout);
	if (buf.s) free(buf.s);
}

static void k8_warn(const v8::FunctionCallbackInfo<v8::Value> &args) // warn(): similar print() but print to stderr
{
	kstring_t buf = {0,0,0};
	for (int32_t i = 0; i < args.Length(); i++) {
		if (i) fputc('\t', stderr);
		k8_write_string(stderr, args, i, &buf);
	}
	fputc('\n', stderr);
	if (buf.s) free(buf.s);
}

static void k8_exit(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::HandleScope handle_scope(args.GetIsolate());
	int exit_code = args[0]->Int32Value(args.GetIsolate()->GetCurrentContext()).FromMaybe(0);
	fflush(stdout); fflush(stderr);
	exit(exit_code);
}

static void k8_load(const v8::FunctionCallbackInfo<v8::Value> &args) // load(): Load and execute a JS file. It also searches ONE path in $K8_PATH
{
	char *env_path = getenv("K8_PATH");
	char abspath[K8_PATH_MAX+1], buf[K8_PATH_MAX+1];
	FILE *fp;
	int32_t k;
	realpath(k8_src_path, abspath);
	for (k = strlen(abspath) - 1; k >= 0; --k)
		if (abspath[k] == '/') break;
	assert(k >= 0);
	abspath[++k] = 0;
	for (int i = 0; i < args.Length(); i++) {
		v8::HandleScope handle_scope(args.GetIsolate());
		v8::String::Utf8Value file(args.GetIsolate(), args[i]);
		int32_t l_fn = strlen(*file);
		buf[0] = 0;
		if (buf[0] == 0 && l_fn < K8_PATH_MAX) { // 1) the current directory first
			if ((fp = fopen(*file, "r")) != 0) {
				strcpy(buf, *file);
				fclose(fp);
			}
		}
		if (buf[0] == 0 && k + l_fn < K8_PATH_MAX) { // 2) the script directory
			strcpy(&abspath[k], *file);
			if ((fp = fopen(abspath, "r")) != 0) {
				strcpy(buf, abspath);
				fclose(fp);
			}
		}
		if (buf[0] == 0 && env_path && strlen(env_path) + l_fn < K8_PATH_MAX) { // 3) a directory on K8_PATH. TODO: to allow multiple paths separated by ":"
			strcpy(buf, env_path); strcat(buf, "/"); strcat(buf, *file);
			if ((fp = fopen(buf, "r")) != 0) fclose(fp);
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

static void k8_read_file(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	if (args.Length() != 1) return;
	v8::HandleScope handle_scope(args.GetIsolate());
	v8::String::Utf8Value fn(args.GetIsolate(), args[0]);
	k8_file_t *fp = ks_open(-1, *fn, "r");
	if (fp == 0) return;
	kstring_t buf = {0,0,0};
	if (ks_read_all(fp, &buf) < 0) return;
	v8::Handle<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(args.GetIsolate(), buf.l);
	memcpy(ab->GetBackingStore()->Data(), buf.s, buf.l);
	args.GetReturnValue().Set(ab);
}

static void k8_encode(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	if (args.Length() == 0 || !args[0]->IsString()) return;
	v8::Isolate *isolate = args.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	int32_t type = 0; // Latin-1
	if (args.Length() >= 2) {
		v8::String::Utf8Value e(isolate, args[1]);
		if (strcmp(*e, "utf8") == 0 || strcmp(*e, "utf-8") == 0 || strcmp(*e, "UTF8") == 0 || strcmp(*e, "UTF-8") == 0)
			type = 1; // UTF-8
	}
	if (type == 0) {
		v8::Handle<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(isolate, args[0].As<v8::String>()->Length());
		args[0].As<v8::String>()->WriteOneByte(isolate, (uint8_t*)ab->GetBackingStore()->Data());
		args.GetReturnValue().Set(ab);
	} else if (type == 1) {
		v8::Handle<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(isolate, args[0].As<v8::String>()->Utf8Length(isolate));
		args[0].As<v8::String>()->WriteUtf8(isolate, (char*)ab->GetBackingStore()->Data());
		args.GetReturnValue().Set(ab);
	}
}

static void k8_decode(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	if (args.Length() == 0) return;
	v8::HandleScope handle_scope(args.GetIsolate());
	void *data = 0;
	int64_t len = 0;
	if (args[0]->IsArrayBuffer())  {
		data = args[0].As<v8::ArrayBuffer>()->GetBackingStore()->Data();
		len = args[0].As<v8::ArrayBuffer>()->GetBackingStore()->ByteLength();
	} else if (args[0]->IsObject()) {
		if (args[0].As<v8::Object>()->InternalFieldCount() > 0) {
			k8_bytes_t *a = (k8_bytes_t*)args[0].As<v8::Object>()->GetAlignedPointerFromInternalField(0);
			if (a && a->magic == K8_BYTES_MAGIC)
				data = a->buf.s, len = a->buf.l;
		}
	}
	if (data == 0) return;
	int32_t type = 0; // Latin-1
	if (args.Length() >= 2) {
		v8::String::Utf8Value e(args.GetIsolate(), args[1]);
		if (strcmp(*e, "utf8") == 0 || strcmp(*e, "utf-8") == 0 || strcmp(*e, "UTF8") == 0 || strcmp(*e, "UTF-8") == 0)
			type = 1; // UTF-8
	}
	v8::Local<v8::String> str;
	if (type == 0) {
		if (v8::String::NewFromOneByte(args.GetIsolate(), (uint8_t*)data, v8::NewStringType::kNormal, len).ToLocal(&str))
			args.GetReturnValue().Set(str);
	} else if (type == 1) {
		if (v8::String::NewFromUtf8(args.GetIsolate(), (char*)data, v8::NewStringType::kNormal, len).ToLocal(&str))
			args.GetReturnValue().Set(str);
	}
}

static void k8_version(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	args.GetReturnValue().Set(v8::String::NewFromUtf8Literal(args.GetIsolate(), K8_VERSION));
}

const char k8_comp_tab[] = {
	  0,   1,	2,	 3,	  4,   5,	6,	 7,	  8,   9,  10,	11,	 12,  13,  14,	15,
	 16,  17,  18,	19,	 20,  21,  22,	23,	 24,  25,  26,	27,	 28,  29,  30,	31,
	 32,  33,  34,	35,	 36,  37,  38,	39,	 40,  41,  42,	43,	 44,  45,  46,	47,
	 48,  49,  50,	51,	 52,  53,  54,	55,	 56,  57,  58,	59,	 60,  61,  62,	63,
	 64, 'T', 'V', 'G', 'H', 'E', 'F', 'C', 'D', 'I', 'J', 'M', 'L', 'K', 'N', 'O',
	'P', 'Q', 'Y', 'S', 'A', 'A', 'B', 'W', 'X', 'R', 'Z',	91,	 92,  93,  94,	95,
	 64, 't', 'v', 'g', 'h', 'e', 'f', 'c', 'd', 'i', 'j', 'm', 'l', 'k', 'n', 'o',
	'p', 'q', 'y', 's', 'a', 'a', 'b', 'w', 'x', 'r', 'z', 123, 124, 125, 126, 127
};

static void k8_revcomp(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	if (args.Length() == 0) return;
	v8::HandleScope handle_scope(args.GetIsolate());
	uint8_t *seq = 0;
	int64_t len = 0;
	if (args[0]->IsArrayBuffer())  {
		seq = (uint8_t*)args[0].As<v8::ArrayBuffer>()->GetBackingStore()->Data();
		len = args[0].As<v8::ArrayBuffer>()->GetBackingStore()->ByteLength();
	} else if (args[0]->IsObject()) {
		if (args[0].As<v8::Object>()->InternalFieldCount() > 0) {
			k8_bytes_t *a = (k8_bytes_t*)args[0].As<v8::Object>()->GetAlignedPointerFromInternalField(0);
			if (a && a->magic == K8_BYTES_MAGIC)
				seq = a->buf.s, len = a->buf.l;
		}
	}
	if (seq == 0) return;
	for (int64_t i = 0; i < len>>1; ++i) {
		uint8_t tmp = seq[len - 1 - i];
		seq[len - 1 - i] = seq[i] < 128? k8_comp_tab[seq[i]] : seq[i];
		seq[i] = tmp < 128? k8_comp_tab[tmp] : tmp;
	}
	if (len & 1) seq[len>>1] = seq[len>>1] < 128? k8_comp_tab[seq[len>>1]] : seq[len>>1];
}

/***********************
 *** The Bytes class ***
 ***********************/

static void k8_bytes_new(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::HandleScope handle_scope(args.GetIsolate());
	k8_bytes_t *a = K8_CALLOC(k8_bytes_t, 1);
	a->magic = K8_BYTES_MAGIC;
	if (args.Length()) {
		a->buf.m = a->buf.l = args[0]->IntegerValue(args.GetIsolate()->GetCurrentContext()).FromMaybe(0);
		a->buf.s = K8_CALLOC(uint8_t, a->buf.l);
	}
	K8_SAVE_PTR(args, 0, a);
}

static void k8_bytes_destroy(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::HandleScope handle_scope(args.GetIsolate());
	k8_bytes_t *a = K8_LOAD_PTR(args, 0, k8_bytes_t);
	if (a == 0) return;
	free(a->buf.s); free(a);
	K8_SAVE_PTR(args, 0, 0);
	args.GetReturnValue().Set(0);
}

static void k8_bytes_set(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::Isolate *isolate = args.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	k8_bytes_t *a = K8_LOAD_PTR(args, 0, k8_bytes_t);
	if (a == 0 || args.Length() == 0) return;
	int64_t pre = a->buf.l;
	int64_t off = args.Length() >= 2? args[1]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(a->buf.l) : a->buf.l;
	if (args[0]->IsNumber()) {
		K8_GROW0(uint8_t, a->buf.s, off, a->buf.m);
		a->buf.s[off] = (uint8_t)args[0]->Uint32Value(isolate->GetCurrentContext()).FromMaybe(0);
		a->buf.l = off + 1;
	} else if (args[0]->IsString()) {
		int32_t len = args[0].As<v8::String>()->Length();
		K8_GROW0(uint8_t, a->buf.s, off + len, a->buf.m);
		args[0].As<v8::String>()->WriteOneByte(isolate, &a->buf.s[off]);
		a->buf.l = off + len;
	} else if (args[0]->IsArray()) {
		v8::Handle<v8::Array> array = v8::Handle<v8::Array>::Cast(args[0]);
		K8_GROW0(uint8_t, a->buf.s, off + array->Length(), a->buf.m);
		for (size_t i = 0; i < array->Length(); ++i) {
			v8::Local<v8::Value> x;
			if (array->Get(isolate->GetCurrentContext(), i).ToLocal(&x))
				a->buf.s[off++] = (uint8_t)x->Uint32Value(isolate->GetCurrentContext()).FromMaybe(0);
		}
		a->buf.l = off;
	} else if (args[0]->IsArrayBuffer()) {
		void *data = args[0].As<v8::ArrayBuffer>()->GetBackingStore()->Data();
		int64_t len = args[0].As<v8::ArrayBuffer>()->GetBackingStore()->ByteLength();
		K8_GROW0(uint8_t, a->buf.s, off + len, a->buf.m);
		memcpy(&a->buf.s[off], data, len);
		a->buf.l = off + len;
	} else {
		isolate->ThrowError("[k8_bytes_set] unsupported type");
	}
	args.GetReturnValue().Set((int32_t)(a->buf.l - pre));
}

static void k8_bytes_toString(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::Isolate *isolate = args.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	k8_bytes_t *a = K8_LOAD_PTR(args, 0, k8_bytes_t);
	if (a == 0) return;
	v8::Local<v8::String> str;
	if (v8::String::NewFromOneByte(args.GetIsolate(), (uint8_t*)a->buf.s, v8::NewStringType::kNormal, a->buf.l).ToLocal(&str))
		args.GetReturnValue().Set(str);
}

static void k8_bytes_length_getter(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value> &info)
{
	v8::HandleScope handle_scope(info.GetIsolate());
	k8_bytes_t *a = K8_LOAD_PTR(info, 0, k8_bytes_t);
	if (a == 0) return;
	info.GetReturnValue().Set((int32_t)a->buf.l);
}

static void k8_bytes_length_setter(v8::Local<v8::String> property, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info)
{
	v8::HandleScope handle_scope(info.GetIsolate());
	k8_bytes_t *a = K8_LOAD_PTR(info, 0, k8_bytes_t);
	if (a == 0) return;
	int64_t len = value->IntegerValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(a->buf.l);
	if (len > a->buf.m) K8_GROW0(uint8_t, a->buf.s, len - 1, a->buf.m);
	a->buf.l = len;
}

static void k8_bytes_capacity_getter(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value> &info)
{
	v8::HandleScope handle_scope(info.GetIsolate());
	k8_bytes_t *a = K8_LOAD_PTR(info, 0, k8_bytes_t);
	if (a == 0) return;
	info.GetReturnValue().Set((int32_t)a->buf.m);
}

static void k8_bytes_capacity_setter(v8::Local<v8::String> property, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info)
{
	v8::HandleScope handle_scope(info.GetIsolate());
	k8_bytes_t *a = K8_LOAD_PTR(info, 0, k8_bytes_t);
	if (a == 0) return;
	int64_t len = value->IntegerValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(a->buf.m);
	if (len < a->buf.l) len = a->buf.l;
	a->buf.m = len;
	a->buf.s = K8_REALLOC(uint8_t, a->buf.s, a->buf.m);
}

static void k8_ext_delete_cb(void *data, size_t len, void *aux) {} // do nothing

static void k8_bytes_buffer_getter(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value> &info)
{
	v8::HandleScope handle_scope(info.GetIsolate());
	k8_bytes_t *a = K8_LOAD_PTR(info, 0, k8_bytes_t);
	if (a == 0) return;
	info.GetReturnValue().Set(v8::ArrayBuffer::New(info.GetIsolate(), v8::ArrayBuffer::NewBackingStore((uint8_t*)a->buf.s, a->buf.l, k8_ext_delete_cb, 0)));
}

/**********************
 *** The File class ***
 **********************/

static void k8_file_open(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::Isolate *isolate = args.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	k8_file_t *ks = 0;
	int fd = args.Length() >= 1 && args[0]->IsUint32()? args[0]->Int32Value(isolate->GetCurrentContext()).FromMaybe(-1) : -1;
	if (args.Length() >= 2) { // File(fn, mode) or File(fd, mode)
		v8::String::Utf8Value mode(isolate, args[1]);
		if (fd >= 0) { // File(fd, mode)
			ks = ks_open(fd, 0, *mode);
		} else { // File(fn, mode)
			v8::String::Utf8Value fn(isolate, args[0]);
			ks = ks_open(-1, *fn, *mode);
		}
	} else if (args.Length() == 1) { // File(fn) or File(fd)
		if (fd >= 0) {
			ks = ks_open(fd, 0, 0);
		} else {
			v8::String::Utf8Value fn(isolate, args[0]);
			ks = ks_open(-1, *fn, 0);
		}
	} else { // File()
		ks = ks_open(0, 0, 0); // open stdin for reading
	}
	if (ks) {
		K8_SAVE_PTR(args, 0, ks);
	} else {
		isolate->ThrowError("k8_open: failed to open file");
		args.GetReturnValue().SetNull();
	}
}

static void k8_file_close(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::HandleScope handle_scope(args.GetIsolate());
	k8_file_t *ks = K8_LOAD_PTR(args, 0, k8_file_t);
	if (ks == 0) return;
	ks_close(ks);
	K8_SAVE_PTR(args, 0, 0);
	args.GetReturnValue().Set(0);
}

static int32_t k8_get_sep_off(const v8::FunctionCallbackInfo<v8::Value> &args, int32_t *sep)
{
	v8::Isolate *isolate = args.GetIsolate();
	*sep = KS_SEP_LINE;
	if (args.Length() >= 2) {
		if (args[1]->IsString()) {
			v8::String::Utf8Value str(isolate, args[0]);
			*sep = (*str)[0];
		} else if (args[1]->IsInt32()) {
			*sep = args[1]->Int32Value(isolate->GetCurrentContext()).FromMaybe(KS_SEP_LINE);
		}
	}
	return args.Length() >= 3? args[2]->Int32Value(isolate->GetCurrentContext()).FromMaybe(0) : 0;
}

static void k8_file_readline(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::Isolate *isolate = args.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	k8_file_t *ks = K8_LOAD_PTR(args, 0, k8_file_t);
	if (ks == 0) return;
	if (!args.Length() || !args[0]->IsObject()) {
		args.GetReturnValue().Set(-2);
		return;
	}
	v8::Handle<v8::Object> b = v8::Handle<v8::Object>::Cast(args[0]);
	k8_bytes_t *a = (k8_bytes_t*)b->GetAlignedPointerFromInternalField(0);
	if (a->magic != K8_BYTES_MAGIC) {
		args.GetReturnValue().Set(-2);
	} else {
		int32_t dret, sep;
		a->buf.l = k8_get_sep_off(args, &sep);
		int64_t ret = ks_getuntil2(ks, sep, &a->buf, &dret, 1);
		if (ret >= 0) args.GetReturnValue().Set(dret);
		else args.GetReturnValue().Set((int32_t)ret);
	}
}

static void k8_file_read(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::Isolate *isolate = args.GetIsolate();
	v8::HandleScope handle_scope(isolate);
	k8_file_t *ks = K8_LOAD_PTR(args, 0, k8_file_t);
	if (ks == 0) return;
	if (args.Length() == 0) { // prototype.read()
		int32_t c = ks_getc(ks);
		args.GetReturnValue().Set(c);
	} else if (args.Length() >= 1 && args[0]->IsObject()) {
		v8::Handle<v8::Object> b = v8::Handle<v8::Object>::Cast(args[0]);
		k8_bytes_t *a = (k8_bytes_t*)b->GetAlignedPointerFromInternalField(0);
		if (a->magic != K8_BYTES_MAGIC) { // TODO: well, there got to be a better way
			args.GetReturnValue().Set(-2);
			return;
		}
		uint32_t off = a->buf.l;
		if (args.Length() >= 2 && args[1]->IsUint32())
			off = args[1]->Uint32Value(isolate->GetCurrentContext()).FromMaybe(0);
		if (args.Length() == 3 && args[1]->IsUint32() && args[2]->IsUint32()) { // prototype.read(bytes, off, len)
			uint32_t len = args[2]->Uint32Value(isolate->GetCurrentContext()).FromMaybe(0);
			K8_GROW(uint8_t, a->buf.s, off + len - 1, a->buf.m);
			int64_t ret = ks_read(ks, &a->buf.s[off], len);
			if (ret > 0 && a->buf.l < off + ret) a->buf.l = off + ret;
			args.GetReturnValue().Set((int32_t)ret);
		} else if (args.Length() == 1 || (args.Length() == 2 && args[1]->IsUint32())) { // prototype.read(bytes) or prototype.read(bytes, off)
			kstring_t tmp = {0,0,0};
			ks_read_all(ks, &tmp);
			K8_GROW(uint8_t, a->buf.s, off + tmp.l, a->buf.m);
			memcpy(&a->buf.s[off], tmp.s, tmp.l);
			a->buf.l = off + tmp.l;
			free(tmp.s);
			args.GetReturnValue().Set((int32_t)tmp.l);
		}
	}
}

static void k8_file_write(const v8::FunctionCallbackInfo<v8::Value> &args)
{
	v8::HandleScope handle_scope(args.GetIsolate());
	k8_file_t *ks = K8_LOAD_PTR(args, 0, k8_file_t);
	if (ks == 0) return;
	if (ks->magic != K8_FILE_MAGIC || ks->fpw == 0) {
		args.GetReturnValue().Set(-1);
		return;
	} else if (args[0]->IsArrayBuffer()) {
		void *data = args[0].As<v8::ArrayBuffer>()->GetBackingStore()->Data();
		int64_t len = args[0].As<v8::ArrayBuffer>()->GetBackingStore()->ByteLength();
		assert(len >= 0 && len < INT32_MAX);
		if (len > 0) fwrite(data, 1, len, ks->fpw);
		args.GetReturnValue().Set((int32_t)len);
	} else if (args[0]->IsString()) {
		int32_t len = args[0].As<v8::String>()->Length();
		uint8_t *buf;
		buf = K8_MALLOC(uint8_t, len);
		args[0].As<v8::String>()->WriteOneByte(args.GetIsolate(), buf);
		if (len > 0) fwrite(buf, 1, len, ks->fpw);
		free(buf);
		args.GetReturnValue().Set(len);
	}
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
	global->Set(isolate, "read_file", v8::FunctionTemplate::New(isolate, k8_read_file));
	global->Set(isolate, "encode", v8::FunctionTemplate::New(isolate, k8_encode));
	global->Set(isolate, "decode", v8::FunctionTemplate::New(isolate, k8_decode));
	global->Set(isolate, "revcomp", v8::FunctionTemplate::New(isolate, k8_revcomp));
	global->Set(isolate, "k8_version", v8::FunctionTemplate::New(isolate, k8_version));
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
		v8::Handle<v8::FunctionTemplate> ft = v8::FunctionTemplate::New(isolate, k8_file_open);
		ft->SetClassName(v8::String::NewFromUtf8Literal(isolate, "File"));

		v8::Handle<v8::ObjectTemplate> ot = ft->InstanceTemplate();
		ot->SetInternalFieldCount(1);

		v8::Handle<v8::ObjectTemplate> pt = ft->PrototypeTemplate();
		pt->Set(isolate, "read", v8::FunctionTemplate::New(isolate, k8_file_read));
		pt->Set(isolate, "readline", v8::FunctionTemplate::New(isolate, k8_file_readline));
		pt->Set(isolate, "write", v8::FunctionTemplate::New(isolate, k8_file_write));
		pt->Set(isolate, "close", v8::FunctionTemplate::New(isolate, k8_file_close));
		pt->Set(isolate, "destroy", v8::FunctionTemplate::New(isolate, k8_file_close));
		global->Set(isolate, "File", ft);
	}
	return v8::Context::New(isolate, NULL, global);
}

static int k8_main(v8::Isolate *isolate, v8::Platform *platform, v8::Local<v8::Context> &context, int argc, char* argv[])
{
	// parse command-line options
	int c;
	while ((c = getopt(argc, argv, "e:E:vM:m:")) >= 0) {
		if (c == 'e' || c == 'E') { // execute a string
			v8::Local<v8::String> file_name = v8::String::NewFromUtf8Literal(isolate, "unnamed");
			v8::Local<v8::String> source;
			if (!v8::String::NewFromUtf8(isolate, optarg).ToLocal(&source))
				return 1;
			bool success = k8_execute(isolate, source, file_name, (c == 'E'));
			while (v8::platform::PumpMessageLoop(platform, isolate)) continue;
			return success? 0 : 1;
		} else if (c == 'v') {
			printf("v8: %s\nk8: %s\n", v8::V8::GetVersion(), K8_VERSION);
			return 0;
		} else if (c == 'm' || c == 'M') { // do nothing as this has been parsed in k8_set_mem
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
		fprintf(stderr, "  -m INT      v8 max size of the old space (in Mbytes) [16384]\n");
		fprintf(stderr, "  -v          print version number\n");
		fprintf(stderr, "  --help      show v8 command-line options\n");
		return 0;
	}

	// pass command-line arguments though the "arguments" array
	v8::Local<v8::Array> args = v8::Array::New(isolate, argc - optind - 1);
	for (int i = optind + 1; i < argc; ++i)
		args->Set(context, i - optind - 1, v8::String::NewFromUtf8(isolate, argv[i]).ToLocalChecked()).FromJust();
    v8::Local<v8::String> name = v8::String::NewFromUtf8Literal(isolate, "arguments", v8::NewStringType::kInternalized);
	context->Global()->Set(context, name, args).FromJust();

	// load and evaluate the source file
	k8_src_path = argv[optind];
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

void k8_set_mem(int argc, char *argv[])
{
	int c;
	char buf[64], *ptr_size = 0;
	while ((c = getopt(argc, argv, "ve:E:M:m:")) >= 0)
		if (c == 'M' || c == 'm') ptr_size = optarg;
	optreset = optind = opterr = 1;
	if (ptr_size && strlen(ptr_size) > 40) {
		fprintf(stderr, "ERROR: failed to set max_old_space_size\n");
		return;
	}
	strcat(strcpy(buf, "--max_old_space_size="), ptr_size? ptr_size : "16384");
	v8::V8::SetFlagsFromString(buf, strlen(buf));
}

int main(int argc, char *argv[])
{
	int ret = 0;
	k8_set_mem(argc, argv);
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
			fprintf(stderr, "ERROR: failed to create context\n");
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
