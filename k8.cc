#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <zlib.h>
#include <sys/stat.h>

#include "v8.h" // This is the header file in "v8/src" NOT in "v8/include"!!!

namespace v8i = v8::internal;

#define SAVE_PTR(index, ptr) args.This()->SetPointerInInternalField(index, (void *)(ptr))
#define LOAD_PTR(index, type) reinterpret_cast<type>(args.This()->GetPointerFromInternalField(index))
#define SAVE_VALUE(index, val) args.This()->SetInternalField(index, val)
#define LOAD_VALUE(index) args.This()->GetInternalField(index)

#define JS_STR(...) v8::String::New(__VA_ARGS__)

#define JS_THROW(type, reason) v8::ThrowException(v8::Exception::type(JS_STR(reason)))
#define JS_ERROR(reason) JS_THROW(Error, reason)
#define JS_METHOD(name) v8::Handle<v8::Value> name(const v8::Arguments& args)

#define ASSERT_CONSTRUCTOR if (!args.IsConstructCall()) { return JS_ERROR("Invalid call format. Please use the 'new' operator."); }

static inline const char *ToCString(const v8::String::Utf8Value& value)
{
	return *value? *value : "<string conversion failed>";
}
// copied from ReadFile()
v8::Handle<v8::String> k8_read_file(const char* fn)
{
	FILE *fp = fopen(fn, "rb");
	if (fp == NULL) return v8::Handle<v8::String>();

	fseek(fp, 0, SEEK_END);
	int size = ftell(fp);
	rewind(fp);

	char* chars = new char[size + 1];
	chars[size] = '\0';
	for (int i = 0; i < size;) {
		int read = fread(&chars[i], 1, size - i, fp);
		i += read;
	}
	fclose(fp);
	v8::Handle<v8::String> result = v8::String::New(chars, size);
	delete[] chars;
	return result;
}
// copied from ReportException() in v8/samples/shell.cc
void k8_report_exception(v8::TryCatch* try_catch)
{
	v8::HandleScope handle_scope;
	v8::String::Utf8Value exception(try_catch->Exception());
	const char* exception_string = ToCString(exception);
	v8::Handle<v8::Message> message = try_catch->Message();
	if (message.IsEmpty()) {
		// V8 didn't provide any extra information about this error; just
		// print the exception.
		fprintf(stderr, "%s\n", exception_string);
	} else {
		// Print (filename):(line number): (message).
		v8::String::Utf8Value filename(message->GetScriptResourceName());
		const char* filename_string = ToCString(filename);
		int linenum = message->GetLineNumber();
		fprintf(stderr, "%s:%i: %s\n", filename_string, linenum, exception_string);
		// Print line of source code.
		v8::String::Utf8Value sourceline(message->GetSourceLine());
		const char* sourceline_string = ToCString(sourceline);
		fprintf(stderr, "%s\n", sourceline_string);
		// Print wavy underline (GetUnderline is deprecated).
		int start = message->GetStartColumn();
		for (int i = 0; i < start; i++)
			fprintf(stderr, " ");
		int end = message->GetEndColumn();
		for (int i = start; i < end; i++)
			fprintf(stderr, "^");
		fprintf(stderr, "\n");
		v8::String::Utf8Value stack_trace(try_catch->StackTrace());
		if (stack_trace.length() > 0) {
			const char* stack_trace_string = ToCString(stack_trace);
			fprintf(stderr, "%s\n", stack_trace_string);
		}
	}
}
// Executes a string within the current v8 context.
bool k8_execute(v8::Handle<v8::String> source, v8::Handle<v8::Value> name)
{
	v8::HandleScope handle_scope;
	v8::TryCatch try_catch;
	v8::Handle<v8::Script> script = v8::Script::Compile(source, name);
	if (script.IsEmpty()) { // compilation error
		k8_report_exception(&try_catch);
		return false;
	} else {
		v8::Handle<v8::Value> result = script->Run();
		if (result.IsEmpty()) { // runtime error
			k8_report_exception(&try_catch);
			return false;
		}
	}
	return true;
}

/********************
 * global functions *
 ********************/

// copied from v8/samples/shell.cc
JS_METHOD(k8_func_load)
{
	char buf[1024], *path = getenv("K8_LIBRARY_PATH");
	struct stat r;
	for (int i = 0; i < args.Length(); i++) {
		v8::HandleScope handle_scope;
		v8::String::Utf8Value file(args[i]);
		buf[0] = 0;
		if (stat(*file, &r) == 0) strcpy(buf, *file);
		else if (path) { // TODO: to allow multiple paths separated by ":"
			strcpy(buf, path); strcat(buf, "/"); strcat(buf, *file);
			if (stat(buf, &r) < 0) buf[0] = 0;
		}
		if (buf[0] == 0) return v8::ThrowException(v8::String::New("[load] fail to locate the file"));
		v8::Handle<v8::String> source = k8_read_file(buf);
		if (!k8_execute(source, v8::String::New(*file)))
			return v8::ThrowException(v8::String::New("[load] fail to execute the file"));
	}
	return v8::Undefined();
}

JS_METHOD(k8_func_exit)
{
	int exit_code = args[0]->Int32Value();
	exit(exit_code);
	return v8::Undefined(); // never come to here
}

JS_METHOD(k8_func_version)
{
	return v8::String::New(v8::V8::GetVersion());
}

JS_METHOD(k8_func_print)
{
	for (int i = 0; i < args.Length(); i++) {
		v8::HandleScope handel_scope;
		if (i > 0) putchar(' ');
		v8::String::Utf8Value str(args[i]);
		const char *cstr = ToCString(str);
		fwrite(cstr, 1, str.length(), stdout);
	}
	putchar('\n');
	fflush(stdout);
	return v8::Undefined();
}

/*******************
 * k8 input stream *
 *******************/

#define KS_BUFSIZE 4096
#ifndef kroundup32
#define kroundup32(x) (--(x), (x)|=(x)>>1, (x)|=(x)>>2, (x)|=(x)>>4, (x)|=(x)>>8, (x)|=(x)>>16, ++(x))
#endif

typedef struct {
	int l, m;
	char *s;
} kstring_t;

typedef struct {
	gzFile f;
	int begin, end, is_eof;
	unsigned char *buf;
	kstring_t s;
} kstream_t;

kstream_t *ks_init(gzFile f)
{
	kstream_t *ks = (kstream_t*)calloc(1, sizeof(kstream_t));
	ks->f = f;
	ks->buf = (unsigned char*)malloc(KS_BUFSIZE);
	return ks;
}
void ks_destroy(kstream_t *ks)
{
	if (!ks) return;
	free(ks->buf); free(ks->s.s); free(ks);
}
int ks_getuntil(kstream_t *ks, int delimiter, int *dret)
{
	kstring_t *str = &ks->s;
	if (dret) *dret = 0;
	str->l = 0;
	if (ks->begin >= ks->end && ks->is_eof) return -1;
	for (;;) {
		int i;
		if (ks->begin >= ks->end) {
			if (!ks->is_eof) {
				ks->begin = 0;
				ks->end = gzread(ks->f, ks->buf, KS_BUFSIZE);
				if (ks->end < KS_BUFSIZE) ks->is_eof = 1;
				if (ks->end == 0) break;
			} else break;
		}
		if (delimiter != 0) {
			for (i = ks->begin; i < ks->end; ++i)
				if (ks->buf[i] == delimiter) break;
		} else {
			for (i = ks->begin; i < ks->end; ++i)
				if (isspace(ks->buf[i])) break;
		}
		if (str->m - str->l < i - ks->begin + 1) {
			str->m = str->l + (i - ks->begin) + 1;
			kroundup32(str->m);
			str->s = (char*)realloc(str->s, str->m);
		}
		memcpy(str->s + str->l, ks->buf + ks->begin, i - ks->begin);
		str->l = str->l + (i - ks->begin);
		ks->begin = i + 1;
		if (i < ks->end) {
			if (dret) *dret = ks->buf[i];
			break;
		}
	}
	if (str->l == 0) {
		str->m = 1;
		str->s = (char*)calloc(1, 1);
	}
	str->s[str->l] = '\0';
	return str->l;
}

/*******************
 * The File object *
 *******************/

JS_METHOD(k8_File)
{
	v8::HandleScope handle_scope;
	ASSERT_CONSTRUCTOR;
	gzFile fp;
	if (args.Length()) {
		SAVE_VALUE(0, args[0]);
		v8::String::Utf8Value file(args[0]);
		if (args.Length() >= 2) {
			SAVE_VALUE(1, args[1]);
			v8::String::AsciiValue mode(args[1]);
			fp = gzopen(*file, *mode);
		} else {
			SAVE_VALUE(1, JS_STR("r"));
			fp = gzopen(*file, "r");
		}
		if (fp == 0) return v8::ThrowException(v8::String::New("[File] Fail to open the file"));
	} else {
		SAVE_VALUE(0, JS_STR("-"));
		SAVE_VALUE(1, JS_STR("r"));
		fp = gzdopen(fileno(stdin), "r");
	}
	kstream_t *ks = ks_init(fp);
	SAVE_PTR(2, ks);
	return args.This();
}

JS_METHOD(k8_File_close)
{
	kstream_t *ks = LOAD_PTR(2, kstream_t*);
	gzclose(ks->f);
	ks_destroy(ks);
	return v8::Undefined();
}

JS_METHOD(k8_File_next)
{
	kstream_t *ks = LOAD_PTR(2, kstream_t*);
	int sep = '\n';
	if (args.Length()) {
		v8::HandleScope scope;
		v8::String::AsciiValue sep_str(args[0]);
		sep = int((*sep_str)[0]);
	}
	int ret, dret;
	ret = ks_getuntil(ks, sep, &dret);
	if (ret < 0) return v8::Null();
	return v8::String::New(ks->s.s, ks->s.l);
}

/*********************
 * the main function *
 *********************/

int main(int argc, char *argv[])
{
	int new_argc = 0;
	char **new_argv = 0, dashdash[] = "--";
	{ // insert "--" 
		int i, j, k;
		for (i = 1; i < argc; ++i)
			if (argv[i][0] != '-') break;
		if (i < argc) {
			new_argv = (char**)calloc(argc+1, sizeof(char*));
			for (j = k = 0; j <= i; ++j)
				new_argv[k++] = argv[j];
			new_argv[k++] = dashdash;
			for (; j < argc; ++j) new_argv[k++] = argv[j];
			new_argc = k;
		}
	}
	v8i::FlagList::SetFlagsFromCommandLine(&new_argc, new_argv, true);
	if (v8i::FLAG_help) return 1;
	if (new_argc == 1) {
		fprintf(stderr, "Usage: k8 [options] <source.js> [arguments]\n");
		return 1;
	}

	v8::HandleScope handle_scope;
	v8::Handle<v8::ObjectTemplate> global = v8::ObjectTemplate::New();

	// add global functions
	global->Set(v8::String::New("print"), v8::FunctionTemplate::New(k8_func_print));
	global->Set(v8::String::New("load"), v8::FunctionTemplate::New(k8_func_load));
	global->Set(v8::String::New("exit"), v8::FunctionTemplate::New(k8_func_exit));
	global->Set(v8::String::New("version"), v8::FunctionTemplate::New(k8_func_version));
	{ // add the File object
		v8::HandleScope handle;
		v8::Handle<v8::FunctionTemplate> ft = v8::FunctionTemplate::New(k8_File);
		ft->SetClassName(JS_STR("File"));
		ft->InstanceTemplate()->SetInternalFieldCount(3); // (fn, mode, fp)
		v8::Handle<v8::ObjectTemplate> pt = ft->PrototypeTemplate();
		pt->Set("next", v8::FunctionTemplate::New(k8_File_next));
		pt->Set("close", v8::FunctionTemplate::New(k8_File_close));
		global->Set(v8::String::New("File"), ft);			
	}

	// set arguments
	v8::Persistent<v8::Context> utility_context = v8::Context::New(NULL, global);
	v8::Context::Scope utility_scope(utility_context);
	v8i::JSArguments js_args = v8i::FLAG_js_arguments;
	v8i::Handle<v8i::FixedArray> arguments_array = v8i::Factory::NewFixedArray(js_args.argc());
	for (int j = 0; j < js_args.argc(); j++) {
		v8i::Handle<v8i::String> arg = v8i::Factory::NewStringFromUtf8(v8i::CStrVector(js_args[j]));
		arguments_array->set(j, *arg);
	}
	v8i::Handle<v8i::JSArray> arguments_jsarray = v8i::Factory::NewJSArrayWithElements(arguments_array);
	global->Set(v8::String::New("arguments"), v8::Utils::ToLocal(arguments_jsarray));

	v8::Persistent<v8::Context> context = v8::Context::New(NULL, global);
	for (int i = 1; i < new_argc; i++) {
		// Enter the execution environment before evaluating any code.
		v8::Context::Scope context_scope(context);
		const char* str = new_argv[i];
		if (strcmp(str, "-e") == 0 && i + 1 < new_argc) {
			// Execute argument given to -e option directly
			v8::HandleScope handle_scope;
			v8::Handle<v8::String> file_name = v8::String::New("cmd");
			v8::Handle<v8::String> source = v8::String::New(argv[i + 1]);
			if (!k8_execute(source, file_name))
				return 1;
			i++;
		} else {
			// Use all other arguments as names of files to load and run.
			v8::HandleScope handle_scope;
			v8::Handle<v8::String> file_name = v8::String::New(str);
			v8::Handle<v8::String> source = k8_read_file(str);
			if (source.IsEmpty()) {
				fprintf(stderr, "Error reading '%s'\n", str);
				return 1;
			}
			if (!k8_execute(source, file_name))
				return 1;
		}
	}
	context.Dispose();
	utility_context.Dispose(); // FIXME: is this correct???
	if (new_argv) free(new_argv);
	return 0;
}
