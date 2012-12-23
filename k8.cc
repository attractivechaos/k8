#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include <zlib.h>
#include <v8.h>

#define SAVE_PTR(_args, _index, _ptr)  (_args).This()->SetAlignedPointerInInternalField(_index, (void*)(_ptr))
#define LOAD_PTR(_args, _index, _type) reinterpret_cast<_type>((_args).This()->GetAlignedPointerFromInternalField(_index))
#define SAVE_VALUE(_args, _index, _val) (_args).This()->SetInternalField(_index, _val)
#define LOAD_VALUE(_args, _index) (_args).This()->GetInternalField(_index)

#define JS_STR(...) v8::String::New(__VA_ARGS__)

#define JS_THROW(type, reason) v8::ThrowException(v8::Exception::type(JS_STR(reason)))
#define JS_ERROR(reason) JS_THROW(Error, reason)
#define JS_METHOD(_func, _args) v8::Handle<v8::Value> _func(const v8::Arguments &(_args))

#define ASSERT_CONSTRUCTOR(_args) if (!(_args).IsConstructCall()) { return JS_ERROR("Invalid call format. Please use the 'new' operator."); }

/**********************************************
 *** Generic stream buffer from klib/kseq.h ***
 **********************************************/

#define KS_SEP_SPACE 0 // isspace(): \t, \n, \v, \f, \r
#define KS_SEP_TAB   1 // isspace() && !' '
#define KS_SEP_LINE  2 // line separator: " \n" (Unix) or "\r\n" (Windows)
#define KS_SEP_MAX   2

#define __KS_TYPE(type_t) \
	typedef struct __kstream_t { \
		unsigned char *buf; \
		int begin, end, is_eof; \
		type_t f; \
	} kstream_t;

#define ks_eof(ks) ((ks)->is_eof && (ks)->begin >= (ks)->end)
#define ks_rewind(ks) ((ks)->is_eof = (ks)->begin = (ks)->end = 0)

#define __KS_BASIC(type_t, __bufsize) \
	static inline kstream_t *ks_init(type_t f) { \
		kstream_t *ks = (kstream_t*)calloc(1, sizeof(kstream_t)); \
		ks->f = f; \
		ks->buf = (unsigned char*)malloc(__bufsize); \
		return ks; \
	} \
	static inline void ks_destroy(kstream_t *ks) { if (ks) { free(ks->buf); free(ks); } }

#define __KS_GETC(__read, __bufsize) \
	static inline int ks_getc(kstream_t *ks) { \
		if (ks->is_eof && ks->begin >= ks->end) return -1; \
		if (ks->begin >= ks->end) { \
			ks->begin = 0; ks->end = __read(ks->f, ks->buf, __bufsize); \
			if (ks->end < __bufsize) ks->is_eof = 1; \
			if (ks->end == 0) return -1; \
		} \
		return (int)ks->buf[ks->begin++]; \
	}

#ifndef KSTRING_T
#define KSTRING_T kstring_t
typedef struct __kstring_t {
	size_t l, m;
	char *s;
} kstring_t;
#endif

#ifndef kroundup32
#define kroundup32(x) (--(x), (x)|=(x)>>1, (x)|=(x)>>2, (x)|=(x)>>4, (x)|=(x)>>8, (x)|=(x)>>16, ++(x))
#endif

#define __KS_GETUNTIL(__read, __bufsize) \
	static int ks_getuntil2(kstream_t *ks, int delimiter, kstring_t *str, int *dret, int append) \
	{ \
		if (dret) *dret = 0; \
		str->l = append? str->l : 0; \
		if (ks->begin >= ks->end && ks->is_eof) return -1; \
		for (;;) { \
			int i; \
			if (ks->begin >= ks->end) { \
				if (!ks->is_eof) { \
					ks->begin = 0; \
					ks->end = __read(ks->f, ks->buf, __bufsize); \
					if (ks->end < __bufsize) ks->is_eof = 1; \
					if (ks->end == 0) break; \
				} else break; \
			} \
			if (delimiter == KS_SEP_LINE) { \
				for (i = ks->begin; i < ks->end; ++i) \
					if (ks->buf[i] == '\n') break; \
			} else if (delimiter > KS_SEP_MAX) { \
				for (i = ks->begin; i < ks->end; ++i) \
					if (ks->buf[i] == delimiter) break; \
			} else if (delimiter == KS_SEP_SPACE) { \
				for (i = ks->begin; i < ks->end; ++i) \
					if (isspace(ks->buf[i])) break; \
			} else if (delimiter == KS_SEP_TAB) { \
				for (i = ks->begin; i < ks->end; ++i) \
					if (isspace(ks->buf[i]) && ks->buf[i] != ' ') break; \
			} else i = 0; /* never come to here! */ \
			if (str->m - str->l < (size_t)(i - ks->begin + 1)) { \
				str->m = str->l + (i - ks->begin) + 1; \
				kroundup32(str->m); \
				str->s = (char*)realloc(str->s, str->m); \
			} \
			memcpy(str->s + str->l, ks->buf + ks->begin, i - ks->begin); \
			str->l = str->l + (i - ks->begin); \
			ks->begin = i + 1; \
			if (i < ks->end) { \
				if (dret) *dret = ks->buf[i]; \
				break; \
			} \
		} \
		if (str->s == 0) { \
			str->m = 1; \
			str->s = (char*)calloc(1, 1); \
		} else if (delimiter == KS_SEP_LINE && str->l > 1 && str->s[str->l-1] == '\r') --str->l; \
		str->s[str->l] = '\0';											\
		return str->l; \
	} \
	static inline int ks_getuntil(kstream_t *ks, int delimiter, kstring_t *str, int *dret) \
	{ return ks_getuntil2(ks, delimiter, str, dret, 0); }

#define KSTREAM_INIT(type_t, __read, __bufsize) \
	__KS_TYPE(type_t) \
	__KS_BASIC(type_t, __bufsize) \
	__KS_GETC(__read, __bufsize) \
	__KS_GETUNTIL(__read, __bufsize)

KSTREAM_INIT(gzFile, gzread, 0x10000)

/******************************
 *** New built-in functions ***
 ******************************/

const char *k8_cstr(const v8::String::AsciiValue &str)
{
	return *str? *str : "<N/A>";
}

JS_METHOD(k8f_print, args)
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

JS_METHOD(k8f_exit, args)
{
	int exit_code = args[0]->Int32Value();
	fflush(stdout); fflush(stderr);
	exit(exit_code);
	return v8::Undefined();
}

JS_METHOD(k8f_version, args)
{
	return v8::String::New(v8::V8::GetVersion());
}

/*******************
 *** File object ***
 *******************/

JS_METHOD(k8f_file, args)
{
	v8::HandleScope handle_scope;
	ASSERT_CONSTRUCTOR(args);
	FILE *fpw = 0;
	gzFile fpr = 0;
	if (args.Length()) {
		SAVE_VALUE(args, 0, args[0]);
		v8::String::AsciiValue file(args[0]);
		if (args.Length() >= 2) {
			SAVE_VALUE(args, 1, args[1]);
			v8::String::AsciiValue mode(args[1]);
			const char *cstr = k8_cstr(mode);
			if (strchr(cstr, 'w')) fpw = fopen(k8_cstr(file), cstr);
			else fpr = gzopen(k8_cstr(file), cstr);
		} else {
			SAVE_VALUE(args, 1, JS_STR("r"));
			fpr = gzopen(*file, "r");
		}
		if (fpr == 0 && fpw == 0)
			return JS_THROW(Error, "[File] Fail to open the file");
	} else {
		SAVE_VALUE(args, 0, JS_STR("-"));
		SAVE_VALUE(args, 1, JS_STR("r"));
		fpr = gzdopen(fileno(stdin), "r");
	}
	SAVE_PTR(args, 2, fpr);
	SAVE_PTR(args, 3, fpw);
	return args.This();
}

JS_METHOD(k8f_file_close, args)
{
	gzFile fpr = LOAD_PTR(args, 2, gzFile);
	FILE  *fpw = LOAD_PTR(args, 3, FILE*);
	if (fpr) gzclose(fpr);
	if (fpw) fclose(fpw);
	SAVE_PTR(args, 2, 0);
	SAVE_PTR(args, 3, 0);
	return v8::Undefined();
}

JS_METHOD(k8f_file_read, args)
{
	v8::HandleScope scope;
	gzFile fp = LOAD_PTR(args, 2, gzFile);
	if (args.Length() == 0) return v8::Null();
	long len, rdlen;
	v8::String::AsciiValue clen(args[0]);
	len = atol(k8_cstr(clen));
	char buf[len];
	rdlen = gzread(fp, buf, len);
	return scope.Close(v8::String::New(buf, rdlen));
}

JS_METHOD(k8f_file_write, args)
{
	v8::HandleScope scope;
	FILE *fp = LOAD_PTR(args, 3, FILE*);
	if (args.Length() == 0) return scope.Close(v8::Integer::New(0));
	v8::String::AsciiValue vbuf(args[0]);
	long len = fwrite(*vbuf, 1, vbuf.length(), fp);
	return scope.Close(v8::Integer::New(len));
}

/*********************
 *** Main function ***
 *********************/

static v8::Persistent<v8::Context> CreateShellContext() // adapted from shell.cc
{
	v8::Handle<v8::ObjectTemplate> global = v8::ObjectTemplate::New();
	global->Set(v8::String::New("print"), v8::FunctionTemplate::New(k8f_print));
	global->Set(v8::String::New("exit"), v8::FunctionTemplate::New(k8f_exit));
	global->Set(v8::String::New("version"), v8::FunctionTemplate::New(k8f_version));
	{
		v8::HandleScope scope;
		v8::Handle<v8::FunctionTemplate> ft = v8::FunctionTemplate::New(k8f_file);
		ft->SetClassName(JS_STR("File"));
		ft->InstanceTemplate()->SetInternalFieldCount(4); // (fn, mode, fpr, fpw)
		v8::Handle<v8::ObjectTemplate> pt = ft->PrototypeTemplate();
		pt->Set("read", v8::FunctionTemplate::New(k8f_file_read));
		pt->Set("write", v8::FunctionTemplate::New(k8f_file_write));
		pt->Set("close", v8::FunctionTemplate::New(k8f_file_close));
		global->Set(v8::String::New("File"), ft);	
	}
	return v8::Context::New(NULL, global);
}

void ReportException(v8::TryCatch *try_catch) // nearly the same as shell.cc
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

static bool ExecuteString(v8::Handle<v8::String> source, v8::Handle<v8::Value> name, bool prt_rst)
{
	v8::HandleScope handle_scope;
	v8::TryCatch try_catch;
	v8::Handle<v8::Script> script = v8::Script::Compile(source, name);
	if (script.IsEmpty()) {
		ReportException(&try_catch);
		return false;
	} else {
		v8::Handle<v8::Value> result = script->Run();
		if (result.IsEmpty()) {
			assert(try_catch.HasCaught());
			ReportException(&try_catch);
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

static v8::Handle<v8::String> ReadFile(const char *name)
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

static int RunMain(int argc, char* argv[])
{
	for (int i = 1; i < argc; i++) {
		const char* str = argv[i];
		if ((strcmp(str, "-e") == 0 || strcmp(str, "-E") == 0) && i + 1 < argc) {
			v8::Handle<v8::String> file_name = v8::String::New("unnamed");
			v8::Handle<v8::String> source = v8::String::New(argv[++i]);
			if (!ExecuteString(source, file_name, (strcmp(str, "-E") == 0))) return 1;
		} else {
			v8::Handle<v8::String> file_name = v8::String::New(str);
			v8::Handle<v8::String> source = ReadFile(str);
			if (source.IsEmpty()) {
				fprintf(stderr, "Error reading '%s'\n", str);
				continue;
			}
			if (!ExecuteString(source, file_name, false)) return 1;
		}
	}
	return 0;
}

int main(int argc, char* argv[])
{
	v8::V8::SetFlagsFromCommandLine(&argc, argv, true);
	int ret;
	if (argc == 1) {
		fprintf(stderr, "Usage: k8 [-e jsCode] [-E jsCode] <src.js>\n");
		return 1;
	}
	{
		v8::HandleScope handle_scope;
		v8::Persistent<v8::Context> context = CreateShellContext();
		if (context.IsEmpty()) {
			fprintf(stderr, "Error creating context\n");
			return 1;
		}
		context->Enter();
		ret = RunMain(argc, argv);
		context->Exit();
		context.Dispose();
	}
	v8::V8::Dispose();
	return ret;
}
