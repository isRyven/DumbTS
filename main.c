#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <quickjs.h>
#include <zrc/zrc.h>

#include "libstd.h"

#define PROG_NAME "dumbts"
#if defined(_WIN32)
#define OS_PLATFORM "win32"
#elif defined(__APPLE__)
#define OS_PLATFORM "darwin"
#else
#define OS_PLATFORM "linux"
#endif

#define MANIFEST_FILE "manifest.json"

zrc_lib_t rc;
zrc_import_lib(assets)

#define geterrstr(n) (n == 0 ? "" : strerror(n))

static char* file_extract(zrc_lib_t *rc, const char *path, unsigned int *size) 
{
	return zrc_read_file(rc, path, size);
}

#define file_extract_or_die(rc, path, out, outsize) \
if (!(out = file_extract(rc, path, outsize))) { \
	fprintf(stderr, "cannot extract file '%s': %s", path, geterrstr(errno)); \
	status = 1; \
	goto done; \
}

typedef enum {
	SCRIPT_INVALID,
	SCRIPT_REGULAR,
	SCRIPT_BINARY
} script_type_t;

int extract_script(zrc_lib_t *rc, const char *filename, char **output, unsigned int *output_size) 
{
	char path[256], *ext;
	// try loading jsbin, if js script is not found
	if (!(*output = file_extract(rc, filename, output_size))) {
		sprintf(path, "%s", filename);
		ext = strrchr(path, '.');
		if (!ext) {
			return SCRIPT_INVALID;
		}
		*ext = 0;
		strcat(path, ".jsbin");
		if ((*output = file_extract(rc, path, output_size))) {
			return SCRIPT_BINARY;
		}
	}
	if (!output)
		return SCRIPT_INVALID;
	return SCRIPT_REGULAR;
}

static int eval_buf(JSContext *ctx, const void *buf, int buf_len, const char *filename)
{
    JSValue val;
    int ret;

    val = JS_Eval(ctx, buf, buf_len, filename, JS_EVAL_TYPE_GLOBAL);

    if (JS_IsException(val)) {
        js_std_dump_error(ctx);
        ret = -1;
    } else {
        ret = 0;
    }
    JS_FreeValue(ctx, val);
    return ret;
}

static int eval_file(JSContext *ctx, const char *filename)
{
    uint8_t *buf;
    int ret;
    size_t buf_len;
    
    buf = js_std_load_file(ctx, &buf_len, filename);
    if (!buf) {
        perror(filename);
        return -1;
    }
    ret = eval_buf(ctx, buf, buf_len, filename);
    js_free(ctx, buf);
    return ret;
}

static int print_host_version(JSContext *ctx, JSValue manifest) 
{
	JSValue prop;
	const char *ver;
	if (!JS_IsObject(manifest))
		return 1;
	prop = JS_GetPropertyStr(ctx, manifest, "version");
	ver = JS_ToCString(ctx, prop);
	printf("v%s\n", ver);
	JS_FreeValue(ctx, prop);
	JS_FreeCString(ctx, ver);
	return 0;
}

static int print_host_about(JSContext *ctx, JSValue manifest) 
{
	const char *ver, *name, *desc;
	JSValue prop;
	if (!JS_IsObject(manifest))
		return 1;
	
	prop = JS_GetPropertyStr(ctx, manifest, "version");
	ver = JS_ToCString(ctx, prop);
	JS_FreeValue(ctx, prop);
	
	prop = JS_GetPropertyStr(ctx, manifest, "name");
	name = JS_ToCString(ctx, prop);
	JS_FreeValue(ctx, prop);

	prop = JS_GetPropertyStr(ctx, manifest, "description");
	desc = JS_ToCString(ctx, prop);
	JS_FreeValue(ctx, prop);

	printf("%s v%s\n", name, ver);
	printf("%s\n", desc);

	JS_FreeCString(ctx, ver);
	JS_FreeCString(ctx, name);
	JS_FreeCString(ctx, desc);

	return 0;
}

/* read internal file  */
JSValue js_std_readfile_internal(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    char *buf;
    const char *filename;
    JSValue ret;
    unsigned int buf_len;
    
    filename = JS_ToCString(ctx, argv[0]);
    if (!filename)
        return JS_EXCEPTION;

    buf = file_extract(&rc, filename, &buf_len);
    if (!buf) {
    	JS_FreeCString(ctx, filename);
        return js_std_throw_error(ctx, geterrstr(errno), errno);
    }
    ret = JS_NewStringLen(ctx, buf, buf_len);
    free(buf);
    JS_FreeCString(ctx, filename);
    return ret;
}

/* read internal file  */
JSValue js_std_readfile_internal_raw(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    char *buf;
    const char *filename;
    JSValue ret;
    unsigned int buf_len;
    
    filename = JS_ToCString(ctx, argv[0]);
    if (!filename)
        return JS_EXCEPTION;

    buf = file_extract(&rc, filename, &buf_len);
    if (!buf) {
    	JS_FreeCString(ctx, filename);
        return js_std_throw_error(ctx, geterrstr(errno), errno);
    }
    JS_FreeCString(ctx, filename);
    return js_std_blob_ptr(ctx, buf, buf_len, 0, 0);
}

/* check if internal file exists */
JSValue js_std_exists_internal(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    const char *filename;;
    
    filename = JS_ToCString(ctx, argv[0]);
    if (!filename)
        return JS_EXCEPTION;

    if (zrc_file_exists(&rc, filename)) {
    	JS_FreeCString(ctx, filename);
		return JS_TRUE;
	}

    JS_FreeCString(ctx, filename);
    return JS_FALSE;
}

static void add_helpers(JSContext *ctx, int argc, char **argv)
{
    JSValue global_obj, console, args;
    int i;

    global_obj = JS_GetGlobalObject(ctx);

    args = JS_NewArray(ctx);
    for(i = 0; i < argc; i++) {
        JS_SetPropertyUint32(ctx, args, i, JS_NewString(ctx, argv[i]));
    }

    JS_SetPropertyStr(ctx, global_obj, "__scriptArgs", args);
    JS_SetPropertyStr(ctx, global_obj, "__print", JS_NewCFunction(ctx, js_std_print, "__print", 1));
    JS_SetPropertyStr(ctx, global_obj, "__printf", JS_NewCFunction(ctx, js_std_printf, "__printf", 1));
    JS_SetPropertyStr(ctx, global_obj, "__sprintf", JS_NewCFunction(ctx, js_std_sprintf, "__sprintf", 1));
    JS_SetPropertyStr(ctx, global_obj, "__compile", JS_NewCFunction(ctx, js_std_compile, "__compile", 2));
    JS_SetPropertyStr(ctx, global_obj, "__readfile", JS_NewCFunction(ctx, js_std_readfile, "__readfile", 1));
    JS_SetPropertyStr(ctx, global_obj, "__readfile_raw", JS_NewCFunction(ctx, js_std_readfile_raw, "__readfile_raw", 1));
    JS_SetPropertyStr(ctx, global_obj, "__writefile", JS_NewCFunction(ctx, js_std_writefile, "__writefile", 2));
    JS_SetPropertyStr(ctx, global_obj, "__readfile_internal", JS_NewCFunction(ctx, js_std_readfile_internal, "__readfile_internal", 1));
    JS_SetPropertyStr(ctx, global_obj, "__readfile_internal_raw", JS_NewCFunction(ctx, js_std_readfile_internal_raw, "__readfile_internal_raw", 1));
    JS_SetPropertyStr(ctx, global_obj, "__exists_internal", JS_NewCFunction(ctx, js_std_exists_internal, "__exists_internal", 1));
    JS_SetPropertyStr(ctx, global_obj, "__exists", JS_NewCFunction(ctx, js_std_exists, "__exists", 2));
    JS_SetPropertyStr(ctx, global_obj, "__exit", JS_NewCFunction(ctx, js_std_exit, "__exit", 1));
    JS_SetPropertyStr(ctx, global_obj, "__getenv", JS_NewCFunction(ctx, js_std_getenv, "__getenv", 1));
    JS_SetPropertyStr(ctx, global_obj, "__getcwd", JS_NewCFunction(ctx, js_std_getcwd, "__getcwd", 0));
    JS_SetPropertyStr(ctx, global_obj, "__realpath", JS_NewCFunction(ctx, js_std_realpath, "__realpath", 1));
    JS_SetPropertyStr(ctx, global_obj, "__mkdir", JS_NewCFunction(ctx, js_std_mkdir, "__mkdir", 2));
    JS_SetPropertyStr(ctx, global_obj, "__remove", JS_NewCFunction(ctx, js_std_remove, "__remove", 1));
    JS_SetPropertyStr(ctx, global_obj, "__readdir", JS_NewCFunction(ctx, js_std_readdir, "__readdir", 1));
    JS_SetPropertyStr(ctx, global_obj, "__stat", JS_NewCFunction(ctx, js_std_stat, "__stat", 1));
    JS_SetPropertyStr(ctx, global_obj, "__utimes", JS_NewCFunction(ctx, js_std_utimes, "__utimes", 3));
    JS_SetPropertyStr(ctx, global_obj, "__eval_bytecode", JS_NewCFunction(ctx, js_std_eval_bytecode, "__eval_bytecode", 2));
    JS_SetPropertyStr(ctx, global_obj, "__eval_module", JS_NewCFunction(ctx, js_std_eval_module, "__eval_module", 2));
    JS_SetPropertyStr(ctx, global_obj, "__platform", JS_NewString(ctx, OS_PLATFORM));
    JS_SetPropertyStr(ctx, global_obj, "globalThis", JS_DupValue(ctx, global_obj));
    
    JS_FreeValue(ctx, global_obj);
}

int main(int argc, char **argv)
{
    JSRuntime *rt;
    JSContext *ctx;
    JSValue obj;
    JSValue val;
    int i, res, status = 0;
    int run_external_script = 0;
    char *manifest_src = NULL, *bootstrap_src;
    unsigned int manifest_size, bootstrap_size;
    int (*host_action)(JSContext *ctx, JSValue manifest);
    const char *bootstrap_file;

    if (zrc_open_lib(&rc, zrc_get_lib(assets), NULL, NULL)) {
		fprintf(stderr, "cannot allocate memory or read resources.\n");
		return 1;
	}
    
    if (!(rt = JS_NewRuntime())) {
        fprintf(stderr, PROG_NAME ": cannot allocate JS runtime\n");
        exit(2);
    }

    if (!(ctx = JS_NewContext(rt))) {
        fprintf(stderr, PROG_NAME ": cannot allocate JS context\n");
        exit(2);
    }

    file_extract_or_die(&rc, MANIFEST_FILE, manifest_src, &manifest_size);
    manifest_src = realloc(manifest_src, manifest_size + 1);
    manifest_src[manifest_size] = 0;

    obj = JS_ParseJSON(ctx, manifest_src, manifest_size, "manifest.json");
	if (JS_IsException(obj)) {
		JSValue err = JS_GetException(ctx);
        const char *err_str = JS_ToCString(ctx, err);
        fprintf(stderr, "%s\n", err_str);
        JS_FreeValue(ctx, err);
        JS_FreeCString(ctx, err_str);
        JS_ResetUncatchableError(ctx);
        goto done;
	}

	for (i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--host-version")) {
			host_action = print_host_version;
		} else if (!strcmp(argv[i], "--host-about")) {
			host_action = print_host_about;
		} else if (!strcmp(argv[i], "--host-script")) {
			run_external_script = 1;
			JS_FreeValue(ctx, obj);
			break;
		} else {
			continue;
		}
		status = host_action(ctx, obj);
	    JS_FreeValue(ctx, obj);
		goto done;
	}

  	add_helpers(ctx, argc, argv);

    if (run_external_script) {
    	if (argc < 2) {
	    	goto done;
    	}
		obj = JS_GetGlobalObject(ctx);
		JS_SetPropertyStr(ctx, obj, "__filename", JS_NewString(ctx, argv[argc - 1]));
		JS_FreeValue(ctx, obj);
		eval_file(ctx, argv[argc - 1]);
    	goto done;
    }

	val = JS_GetPropertyStr(ctx, obj, "bootstrap");
	if (JS_IsUndefined(val)) {
		status = 1;
		fprintf(stderr, "cannot find bootstrap script.\n");
		JS_FreeValue(ctx, obj);
		goto done;
	}

	bootstrap_file = JS_ToCString(ctx, val);

    JS_FreeValue(ctx, val);
    JS_FreeValue(ctx, obj);

    if (!bootstrap_file) {
		status = 1;
		fprintf(stderr, "cannot find bootstrap script.\n");
		JS_FreeCString(ctx, bootstrap_file);
    	goto done;
    }

    /* add bootstrap reference */
    obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, obj, "__filename", JS_NewString(ctx, bootstrap_file));
    JS_FreeValue(ctx, obj);

    res = extract_script(&rc, bootstrap_file, &bootstrap_src, &bootstrap_size);
    if (res == SCRIPT_INVALID) {
    	status = 1;
    	fprintf(stderr, "cannot extract file '%s': %s", bootstrap_file, geterrstr(errno));
    	JS_FreeCString(ctx, bootstrap_file);
    	goto done;
    }

    if (res == SCRIPT_BINARY) {
    	obj = JS_ReadObject(ctx, (uint8_t*)bootstrap_src, bootstrap_size, JS_READ_OBJ_BYTECODE);
        if (JS_IsException(obj)) {
            js_std_dump_error(ctx);
        } else {
	        val = JS_EvalFunction(ctx, obj);
	        if (JS_IsException(val))
	            js_std_dump_error(ctx);
	        else
		        JS_FreeValue(ctx, val);
        }
    } else {
	    bootstrap_src = realloc(bootstrap_src, bootstrap_size + 1);
	    bootstrap_src[bootstrap_size] = 0;
	    eval_buf(ctx, bootstrap_src, bootstrap_size, bootstrap_file);
    }

    free(bootstrap_src);
    JS_FreeCString(ctx, bootstrap_file);

done:
	free(manifest_src);
	zrc_close_lib(&rc); 
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);

    return status;	
}