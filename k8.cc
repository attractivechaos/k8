#define K8_VERSION "0.1.4-r37" // known to work with V8-3.16.1

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <zlib.h>
#include <v8.h>

/************************************
 *** Convenient macros from v8cgi ***
 ************************************/

// A V8 object can have multiple internal fields invisible to JS. The following 4 macros read and write these fields
#define SAVE_PTR(_args, _index, _ptr)  (_args).This()->SetAlignedPointerInInternalField(_index, (void*)(_ptr))
#define LOAD_PTR(_args, _index, _type) reinterpret_cast<_type>((_args).This()->GetAlignedPointerFromInternalField(_index))
#define SAVE_VALUE(_args, _index, _val) (_args).This()->SetInternalField(_index, _val)
#define LOAD_VALUE(_args, _index) (_args).This()->GetInternalField(_index)

#define JS_STR(...) v8::String::New(__VA_ARGS__)

#define JS_THROW(type, reason) v8::ThrowException(v8::Exception::type(JS_STR(reason)))
#define JS_ERROR(reason) JS_THROW(Error, reason)
#define JS_METHOD(_func, _args) v8::Handle<v8::Value> _func(const v8::Arguments &(_args))

#define ASSERT_CONSTRUCTOR(_args) if (!(_args).IsConstructCall()) { return JS_ERROR("Invalid call format. Please use the 'new' operator."); }

#define kroundup32(x) (--(x), (x)|=(x)>>1, (x)|=(x)>>2, (x)|=(x)>>4, (x)|=(x)>>8, (x)|=(x)>>16, ++(x))

/*******************************
 *** Fundamental V8 routines ***
 *******************************/

const char *k8_cstr(const v8::String::AsciiValue &str) // Convert a V8 string to C string
{
	return *str? *str : "<N/A>";
}

static void k8_exception(v8::TryCatch *try_catch) // Exception handling. Adapted from v8/shell.cc
{
	v8::HandleScope handle_scope;
	v8::String::AsciiValue exception(try_catch->Exception());
	const char* exception_string = k8_cstr(exception);
	v8::Handle<v8::Message> message = try_catch->Message();
	if (message.IsEmpty()) {
		// V8 didn't provide any extra information about this error; just print the exception.
		fprintf(stderr, "%s\n", exception_string);
	} else {
		// Print (filename):(line number): (message).
		v8::String::AsciiValue filename(message->GetScriptResourceName());
		const char* filename_string = k8_cstr(filename);
		int linenum = message->GetLineNumber();
		fprintf(stderr, "%s:%i: %s\n", filename_string, linenum, exception_string);
		// Print line of source code.
		v8::String::AsciiValue sourceline(message->GetSourceLine());
		const char *sourceline_string = k8_cstr(sourceline);
		fprintf(stderr, "%s\n", sourceline_string);
		// Print wavy underline (GetUnderline is deprecated).
		int start = message->GetStartColumn();
		for (int i = 0; i < start; i++) fputc(' ', stderr);
		int end = message->GetEndColumn();
		for (int i = start; i < end; i++) fputc('^', stderr);
		fputc('\n', stderr);
		v8::String::AsciiValue stack_trace(try_catch->StackTrace());
		if (stack_trace.length() > 0) { // TODO: is the following redundant?
			const char* stack_trace_string = k8_cstr(stack_trace);
			fputs(stack_trace_string, stderr); fputc('\n', stderr);
		}
	}
}

bool k8_execute(v8::Handle<v8::String> source, v8::Handle<v8::Value> name, bool prt_rst) // Execute JS in a string. Adapted from v8/shell.cc
{
	v8::HandleScope handle_scope;
	v8::TryCatch try_catch;
	v8::Handle<v8::Script> script = v8::Script::Compile(source, name);
	if (script.IsEmpty()) {
		k8_exception(&try_catch);
		return false;
	} else {
		v8::Handle<v8::Value> result = script->Run();
		if (result.IsEmpty()) {
			assert(try_catch.HasCaught());
			k8_exception(&try_catch);
			return false;
		} else {
			assert(!try_catch.HasCaught());
			if (prt_rst && !result->IsUndefined()) {
				v8::String::AsciiValue str(result);
				puts(k8_cstr(str));
			}
			return true;
		}
	}
}

v8::Handle<v8::String> k8_readfile(const char *name) // Read the entire file. Copied from v8/shell.cc
{
	FILE* file = fopen(name, "rb");
	if (file == NULL) return v8::Handle<v8::String>();

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
	v8::Handle<v8::String> result = v8::String::New(chars, size);
	delete[] chars;
	return result;
}

/******************************
 *** New built-in functions ***
 ******************************/

JS_METHOD(k8_print, args) // print(): print to stdout; TAB demilited if multiple arguments are provided
{
	for (int i = 0; i < args.Length(); i++) {
		v8::HandleScope handle_scope;
		if (i) putchar('\t');
		v8::String::AsciiValue str(args[i]);
		fputs(k8_cstr(str), stdout);
	}
	putchar('\n');
	return v8::Undefined();
}

JS_METHOD(k8_exit, args) // exit()
{
	int exit_code = args[0]->Int32Value();
	fflush(stdout); fflush(stderr);
	exit(exit_code);
	return v8::Undefined();
}

JS_METHOD(k8_load, args) // load(): Load and execute a JS file. It also searches ONE path in $K8_LIBRARY_PATH
{
	char buf[1024], *path = getenv("K8_LIBRARY_PATH");
	FILE *fp;
	for (int i = 0; i < args.Length(); i++) {
		v8::HandleScope handle_scope;
		v8::String::Utf8Value file(args[i]);
		buf[0] = 0;
		if ((fp = fopen(*file, "r")) != 0) {
			fclose(fp);
			strcpy(buf, *file);
		} else if (path) { // TODO: to allow multiple paths separated by ":"
			strcpy(buf, path); strcat(buf, "/"); strcat(buf, *file);
			if ((fp = fopen(*file, "r")) == 0) buf[0] = 0;
			else fclose(fp);
		}
		if (buf[0] == 0) return JS_THROW(Error, "[load] fail to locate the file");
		v8::Handle<v8::String> source = k8_readfile(buf);
		if (!k8_execute(source, v8::String::New(*file), false))
			return JS_THROW(Error, "[load] fail to execute the file");
	}
	return v8::Undefined();
}

/********************
 *** Bytes object ***
 ********************/

typedef struct {
	int32_t n, m;
	uint8_t *a;
} kvec8_t;

JS_METHOD(k8_bytes, args)
{
	v8::HandleScope scope;
	ASSERT_CONSTRUCTOR(args);
	kvec8_t *a;
	a = (kvec8_t*)calloc(1, sizeof(kvec8_t));
	if (args.Length()) {
		a->m = a->n = args[0]->Int32Value();
		a->a = (uint8_t*)calloc(a->n, 1);
	}
	SAVE_PTR(args, 0, a);
	args.This()->SetIndexedPropertiesToExternalArrayData(a->a, v8::kExternalUnsignedByteArray, a->n);
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

JS_METHOD(k8_bytes_size, args)
{
	v8::HandleScope scope;
	kvec8_t *a = LOAD_PTR(args, 0, kvec8_t*);
	if (args.Length()) {
		int32_t n_old = a->n;
		a->n = args[0]->Int32Value();
		if (a->n > a->m) kv_recapacity(a, a->n);
		if (n_old != a->n) args.This()->SetIndexedPropertiesToExternalArrayData(a->a, v8::kExternalUnsignedByteArray, a->n);
	}
	return scope.Close(v8::Integer::New(a->n));
}

JS_METHOD(k8_bytes_capacity, args)
{
	v8::HandleScope scope;
	kvec8_t *a = LOAD_PTR(args, 0, kvec8_t*);
	if (args.Length()) {
		int32_t n_old = a->n;
		kv_recapacity(a, args[0]->Int32Value());
		if (a->n != n_old) args.This()->SetIndexedPropertiesToExternalArrayData(a->a, v8::kExternalUnsignedByteArray, a->n);
	}
	return scope.Close(v8::Integer::New(a->m));
}

JS_METHOD(k8_bytes_destroy, args)
{
	v8::HandleScope scope;
	args.This()->SetIndexedPropertiesToExternalArrayData(0, v8::kExternalUnsignedByteArray, 0);
	kvec8_t *a = LOAD_PTR(args, 0, kvec8_t*);
	free(a->a);
	v8::V8::AdjustAmountOfExternalAllocatedMemory(-a->m);
	free(a);
	SAVE_PTR(args, 0, 0);
	return v8::Undefined();
}

JS_METHOD(k8_bytes_set, args)
{
#define _extend_vec_(_l_) do { \
		if (pos + (int32_t)(_l_) >= a->n) { \
			kv_recapacity(a, pos + (_l_)); \
			a->n = pos + (_l_); \
			args.This()->SetIndexedPropertiesToExternalArrayData(a->a, v8::kExternalUnsignedByteArray, a->n); \
		} \
		cnt = (_l_); \
	} while (0)

	v8::HandleScope scope;
	kvec8_t *a = LOAD_PTR(args, 0, kvec8_t*);
	if (args.Length() == 0) return v8::Undefined();
	int cnt = 0;
	int32_t pos = args.Length() >= 2? args[1]->Int32Value() : a->n;
	if (args[0]->IsUint32()) {
		_extend_vec_(1);
		a->a[pos] = args[0]->Int32Value();
	} else if (args[0]->IsString()) {
		v8::String::AsciiValue str(args[0]);
		const char *cstr = *str;
		_extend_vec_(str.length());
		for (int i = 0; i < str.length(); ++i) a->a[i+pos] = uint8_t(cstr[i]);
	} else if (args[0]->IsArray()) {
		v8::Handle<v8::Array> array = v8::Handle<v8::Array>::Cast(args[0]);
		_extend_vec_(array->Length());
		for (unsigned i = 0; i < array->Length(); ++i) {
			v8::Handle<v8::Value> tmp = array->Get(v8::Integer::New(i));
			a->a[i+pos] = tmp->IsUint32()? tmp->Int32Value() : 0;
		}
	} else if (args[0]->IsObject()) {
		v8::Handle<v8::Object> b = v8::Handle<v8::Object>::Cast(args[0]); // TODO: check b is a 'Bytes' instance
		kvec8_t *a2 = reinterpret_cast<kvec8_t*>(b->GetAlignedPointerFromInternalField(0));
		_extend_vec_(a2->n);
		for (int32_t i = 0; i < a2->n; ++i) a->a[i+pos] = a2->a[i];
	}
	return scope.Close(v8::Integer::New(cnt));
}

JS_METHOD(k8_bytes_toString, args)
{
	v8::HandleScope scope;
	kvec8_t *a = LOAD_PTR(args, 0, kvec8_t*);
	return scope.Close(v8::String::New((char*)a->a, a->n));
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
static int ks_getuntil(file_t &fp, kstream_t *ks, kvec8_t *kv, int delimiter, int *dret, bool append, reader_t reader)
{
	if (dret) *dret = 0;
	kv->n = append? kv->n : 0;
	if (ks->begin >= ks->end && ks->is_eof) return -1;
	for (;;) {
		int i;
		if (ks->begin >= ks->end) {
			if (!ks->is_eof) {
				ks->begin = 0;
				ks->end = reader(fp, ks->buf, ks->buf_size);
				if (ks->end < ks->buf_size) ks->is_eof = 1;
				if (ks->end == 0) break;
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
		memcpy(kv->a + kv->n, ks->buf + ks->begin, i - ks->begin);
		kv->n = kv->n + (i - ks->begin);
		ks->begin = i + 1;
		if (i < ks->end) {
			if (dret) *dret = ks->buf[i];
			break;
		}
	}
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
		if (len + off > kv->n) kv_recapacity(kv, len + off);
		len = ks_read(fp, ks, kv->a + off, len, gzread);
		if (len + off > kv->n) {
			kv->n = len + off;
			b->SetIndexedPropertiesToExternalArrayData(kv->a, v8::kExternalUnsignedByteArray, kv->n);
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
	bool append = (args.Length() > 2 && args[2]->IsBoolean())? args[2]->BooleanValue() : false;
	ret = ks_getuntil(fpr, ks, kv, sep, &dret, append, gzread);
	b->SetIndexedPropertiesToExternalArrayData(kv->a, v8::kExternalUnsignedByteArray, kv->n);
	return ret >= 0? scope.Close(v8::Integer::New(dret)) : scope.Close(v8::Integer::New(ret));
}

/*********************
 *** Main function ***
 *********************/

static v8::Persistent<v8::Context> CreateShellContext() // adapted from shell.cc
{
	v8::Handle<v8::ObjectTemplate> global = v8::ObjectTemplate::New();
	global->Set(JS_STR("print"), v8::FunctionTemplate::New(k8_print));
	global->Set(JS_STR("exit"), v8::FunctionTemplate::New(k8_exit));
	global->Set(JS_STR("load"), v8::FunctionTemplate::New(k8_load));
	{ // add the 'Bytes' object
		v8::HandleScope scope;
		v8::Handle<v8::FunctionTemplate> ft = v8::FunctionTemplate::New(k8_bytes);
		ft->SetClassName(JS_STR("Bytes"));
		ft->InstanceTemplate()->SetInternalFieldCount(1);
		v8::Handle<v8::ObjectTemplate> pt = ft->PrototypeTemplate();
		pt->Set("size", v8::FunctionTemplate::New(k8_bytes_size));
		pt->Set("capacity", v8::FunctionTemplate::New(k8_bytes_capacity));
		pt->Set("set", v8::FunctionTemplate::New(k8_bytes_set));
		pt->Set("toString", v8::FunctionTemplate::New(k8_bytes_toString));
		pt->Set("destroy", v8::FunctionTemplate::New(k8_bytes_destroy));
		global->Set(JS_STR("Bytes"), ft);	
	}
	{ // add the 'File' object
		v8::HandleScope scope;
		v8::Handle<v8::FunctionTemplate> ft = v8::FunctionTemplate::New(k8_file);
		ft->SetClassName(JS_STR("File"));
		ft->InstanceTemplate()->SetInternalFieldCount(5); // (fn, mode, fpr, fpw)
		v8::Handle<v8::ObjectTemplate> pt = ft->PrototypeTemplate();
		pt->Set("read", v8::FunctionTemplate::New(k8_file_read));
		pt->Set("readline", v8::FunctionTemplate::New(k8_file_readline));
		pt->Set("write", v8::FunctionTemplate::New(k8_file_write));
		pt->Set("close", v8::FunctionTemplate::New(k8_file_close));
		pt->Set("destroy", v8::FunctionTemplate::New(k8_file_close));
		global->Set(v8::String::New("File"), ft);	
	}
	return v8::Context::New(NULL, global);
}

int main(int argc, char* argv[])
{
	v8::V8::SetFlagsFromCommandLine(&argc, argv, true);
	int ret = 0;
	if (argc == 1) {
		fprintf(stderr, "Usage: k8 [-v] [-e jsSrc] [-E jsSrc] <src.js> [arguments]\n");
		return 1;
	}
	{
		v8::HandleScope scope;
		v8::Persistent<v8::Context> context = CreateShellContext();
		if (context.IsEmpty()) {
			fprintf(stderr, "Error creating context\n");
			return 1;
		}
		context->Enter();
		int i, c;
		while ((c = getopt(argc, argv, "ve:E:")) >= 0) // parse k8 related command line options
			if (c == 'e' || c == 'E') {
				if (!k8_execute(JS_STR(optarg), JS_STR("CLI"), (c == 'E'))) { // note the difference between 'e' and 'E'
					ret = 1;
					break;
				}
			} else if (c == 'v') printf("V8: %s\nK8: %s\n", v8::V8::GetVersion(), K8_VERSION);
		if (!ret && optind != argc) {
			v8::HandleScope scope2;
			v8::Local<v8::Array> args = v8::Array::New(argc - optind - 1);
			for (i = optind + 1; i < argc; ++i)
				args->Set(v8::Integer::New(i - optind - 1), JS_STR(argv[i]));
			context->Global()->Set(JS_STR("arguments"), args);
			if (!k8_execute(k8_readfile(argv[optind]), JS_STR(argv[optind]), false)) ret = 1;
		}
		context->Exit();
		context.Dispose();
	}
	v8::V8::Dispose();
	return ret;
}
