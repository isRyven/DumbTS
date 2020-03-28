/*
 * QuickJS C library
 * 
 * Copyright (c) 2017-2020 Fabrice Bellard
 * Copyright (c) 2017-2020 Charlie Gordon
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <dirent.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "libstd.h"
#include "deps/dumbjs/cutils.h"

static int64_t msfromtime(const struct timespec *tv)
{
	return (int64_t)tv->tv_sec * 1000 + (tv->tv_nsec / 1000000);
}

static void timefromms(struct timeval *tv, uint64_t v)
{
	tv->tv_sec = v / 1000;
	tv->tv_usec = (v % 1000) * 1000;
}

JSValue js_std_throw_error(JSContext *ctx, const char *message, int errcode) 
{
	JSValue obj = JS_NewError(ctx);
	JS_DefinePropertyValueStr(ctx, obj, "message",
                          JS_NewString(ctx, message),
                          JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
	JS_DefinePropertyValueStr(ctx, obj, "errno",
                          JS_NewInt32(ctx, errno),
                          JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
	return JS_Throw(ctx, obj);
}

typedef enum BlobTypes {
    BLOB_PTR,
    BLOB_FILE
} BlobTypes;

typedef struct Blob {
    void *data;
    BlobTypes type;
    union {
        struct {
            size_t size;
            BOOL rt_alloc;
            BOOL no_free;
        } ptr;
    };
} Blob;

static void blobClassFinalizer(JSRuntime *rt, JSValue val);
static JSClassID blobClassId;
static JSClassDef blobClassDef = {
    "Blob",
    .finalizer = blobClassFinalizer,
};

static void blobClassFinalizer(JSRuntime *rt, JSValue val)
{
    Blob *b = JS_GetOpaque(val, blobClassId);
    if (b) {
        if (b->type == BLOB_PTR && b->data && !b->ptr.no_free) {
            if (b->ptr.rt_alloc) {
                js_free_rt(rt, b->data);
            } else {
                free(b->data);
            }
        }
        js_free_rt(rt, b);
    }
}

JSValue js_std_blob_ptr(JSContext *ctx, void *data, size_t size, int rt_alloc, int no_free) 
{
    Blob *b;
    JSValue obj;

    if (!JS_IsRegisteredClass(JS_GetRuntime(ctx), blobClassId)) {
        JSValue proto;
        JS_NewClassID(&blobClassId);
        JS_NewClass(JS_GetRuntime(ctx), blobClassId, &blobClassDef);
        proto = JS_NewObject(ctx);
        JS_SetClassProto(ctx, blobClassId, proto); // inherits object prototype
    }
    
    obj = JS_NewObjectClass(ctx, blobClassId);
    if (JS_IsException(obj))
        return obj;
    b = js_mallocz(ctx, sizeof(Blob));
    if (!b) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    b->data = data;
    b->ptr.rt_alloc = rt_alloc;
    b->ptr.no_free = no_free;
    b->ptr.size = size;
    JS_SetOpaque(obj, b);
    return obj;
}

JSValue js_std_print(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    int i;
    const char *str;

    for(i = 0; i < argc; i++) {
        if (i != 0)
            putchar(' ');
        str = JS_ToCString(ctx, argv[i]);
        if (!str)
            return JS_EXCEPTION;
        fputs(str, stdout);
        JS_FreeCString(ctx, str);
    }
    putchar('\n');
    return JS_UNDEFINED;
}

static void js_std_dump_error1(JSContext *ctx, JSValueConst exception_val,
                               BOOL is_throw)
{
    JSValue val;
    const char *stack;
    BOOL is_error;
    
    is_error = JS_IsError(ctx, exception_val);
    if (is_throw && !is_error)
        printf("Throw: ");
    js_std_print(ctx, JS_NULL, 1, (JSValueConst *)&exception_val);
    if (is_error) {
        val = JS_GetPropertyStr(ctx, exception_val, "stack");
        if (!JS_IsUndefined(val)) {
            stack = JS_ToCString(ctx, val);
            printf("%s\n", stack);
            JS_FreeCString(ctx, stack);
        }
        JS_FreeValue(ctx, val);
    }
}

void js_std_dump_error(JSContext *ctx)
{
    JSValue exception_val;
    
    exception_val = JS_GetException(ctx);
    js_std_dump_error1(ctx, exception_val, TRUE);
    JS_FreeValue(ctx, exception_val);
}

static void js_std_dbuf_init(JSContext *ctx, DynBuf *s)
{
    dbuf_init2(s, JS_GetRuntime(ctx), (DynBufReallocFunc *)js_realloc_rt);
}

static JSValue js_printf_internal(JSContext *ctx,
                                  int argc, JSValueConst *argv, FILE *fp)
{
    char fmtbuf[32];
    uint8_t cbuf[UTF8_CHAR_LEN_MAX+1];
    JSValue res;
    DynBuf dbuf;
    const char *fmt_str;
    const uint8_t *fmt, *fmt_end;
    const uint8_t *p;
    char *q;
    int i, c, len;
    size_t fmt_len;
    int32_t int32_arg;
    int64_t int64_arg;
    double double_arg;
    const char *string_arg;
    enum { PART_FLAGS, PART_WIDTH, PART_DOT, PART_PREC, PART_MODIFIER } part;
    int modsize;
    /* Use indirect call to dbuf_printf to prevent gcc warning */
    int (*dbuf_printf_fun)(DynBuf *s, const char *fmt, ...) = (void*)dbuf_printf;

    js_std_dbuf_init(ctx, &dbuf);

    if (argc > 0) {
        fmt_str = JS_ToCStringLen(ctx, &fmt_len, argv[0]);
        if (!fmt_str)
            goto fail;

        i = 1;
        fmt = (const uint8_t *)fmt_str;
        fmt_end = fmt + fmt_len;
        while (fmt < fmt_end) {
            for (p = fmt; fmt < fmt_end && *fmt != '%'; fmt++)
                continue;
            dbuf_put(&dbuf, p, fmt - p);
            if (fmt >= fmt_end)
                break;
            q = fmtbuf;
            *q++ = *fmt++;  /* copy '%' */
            part = PART_FLAGS;
            modsize = 0;
            for (;;) {
                if (q >= fmtbuf + sizeof(fmtbuf) - 1)
                    goto invalid;

                c = *fmt++;
                *q++ = c;
                *q = '\0';

                switch (c) {
                case '1': case '2': case '3':
                case '4': case '5': case '6':
                case '7': case '8': case '9':
                    if (part != PART_PREC) {
                        if (part <= PART_WIDTH)
                            part = PART_WIDTH;
                        else 
                            goto invalid;
                    }
                    continue;

                case '0': case '#': case '+': case '-': case ' ': case '\'':
                    if (part > PART_FLAGS)
                        goto invalid;
                    continue;

                case '.':
                    if (part > PART_DOT)
                        goto invalid;
                    part = PART_DOT;
                    continue;

                case '*':
                    if (part < PART_WIDTH)
                        part = PART_DOT;
                    else if (part == PART_DOT)
                        part = PART_MODIFIER;
                    else
                        goto invalid;

                    if (i >= argc)
                        goto missing;

                    if (JS_ToInt32(ctx, &int32_arg, argv[i++]))
                        goto fail;
                    q += snprintf(q, fmtbuf + sizeof(fmtbuf) - q, "%d", int32_arg);
                    continue;

                case 'h':
                    if (modsize != 0 && modsize != -1)
                        goto invalid;
                    modsize--;
                    part = PART_MODIFIER;
                    continue;
                case 'l':
                    q--;
                    if (modsize != 0 && modsize != 1)
                        goto invalid;
                    modsize++;
                    part = PART_MODIFIER;
                    continue;

                case 'c':
                    if (i >= argc)
                        goto missing;
                    if (JS_IsString(argv[i])) {
                        string_arg = JS_ToCString(ctx, argv[i++]);
                        if (!string_arg)
                            goto fail;
                        int32_arg = unicode_from_utf8((uint8_t *)string_arg, UTF8_CHAR_LEN_MAX, &p);
                        JS_FreeCString(ctx, string_arg);
                    } else {
                        if (JS_ToInt32(ctx, &int32_arg, argv[i++]))
                            goto fail;
                    }
                    /* handle utf-8 encoding explicitly */
                    if ((unsigned)int32_arg > 0x10FFFF)
                        int32_arg = 0xFFFD;
                    /* ignore conversion flags, width and precision */
                    len = unicode_to_utf8(cbuf, int32_arg);
                    dbuf_put(&dbuf, cbuf, len);
                    break;

                case 'd':
                case 'i':
                case 'o':
                case 'u':
                case 'x':
                case 'X':
                    if (i >= argc)
                        goto missing;
                    if (modsize > 0) {
                        if (JS_ToInt64(ctx, &int64_arg, argv[i++]))
                            goto fail;
                        q[1] = q[-1];
                        q[-1] = q[0] = 'l';
                        q[2] = '\0';
                        dbuf_printf_fun(&dbuf, fmtbuf, (long long)int64_arg);
                    } else {
                        if (JS_ToInt32(ctx, &int32_arg, argv[i++]))
                            goto fail;
                        dbuf_printf_fun(&dbuf, fmtbuf, int32_arg);
                    }
                    break;

                case 's':
                    if (i >= argc)
                        goto missing;
                    string_arg = JS_ToCString(ctx, argv[i++]);
                    if (!string_arg)
                        goto fail;
                    dbuf_printf_fun(&dbuf, fmtbuf, string_arg);
                    JS_FreeCString(ctx, string_arg);
                    break;

                case 'e':
                case 'f':
                case 'g':
                case 'a':
                case 'E':
                case 'F':
                case 'G':
                case 'A':
                    if (i >= argc)
                        goto missing;
                    if (JS_ToFloat64(ctx, &double_arg, argv[i++]))
                        goto fail;
                    dbuf_printf_fun(&dbuf, fmtbuf, double_arg);
                    break;

                case '%':
                    dbuf_putc(&dbuf, '%');
                    break;

                default:
                    /* XXX: should support an extension mechanism */
                invalid:
                    JS_ThrowTypeError(ctx, "invalid conversion specifier in format string");
                    goto fail;
                missing:
                    JS_ThrowReferenceError(ctx, "missing argument for conversion specifier");
                    goto fail;
                }
                break;
            }
        }
        JS_FreeCString(ctx, fmt_str);
    }
    if (dbuf.error) {
        res = JS_ThrowOutOfMemory(ctx);
    } else {
        if (fp) {
            len = fwrite(dbuf.buf, 1, dbuf.size, fp);
            res = JS_NewInt32(ctx, len);
        } else {
            res = JS_NewStringLen(ctx, (char *)dbuf.buf, dbuf.size);
        }
    }
    dbuf_free(&dbuf);
    return res;

fail:
    dbuf_free(&dbuf);
    return JS_EXCEPTION;
}


JSValue js_std_sprintf(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv)
{
    return js_printf_internal(ctx, argc, argv, NULL);
}

JSValue js_std_printf(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    return js_printf_internal(ctx, argc, argv, stdout);
}

uint8_t *js_std_load_file(JSContext *ctx, size_t *pbuf_len, const char *filename)
{
    FILE *f;
    uint8_t *buf;
    size_t buf_len;
    long lret;
    
    f = fopen(filename, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) < 0)
        goto fail;
    lret = ftell(f);
    if (lret < 0)
        goto fail;
    /* XXX: on Linux, ftell() return LONG_MAX for directories */
    if (lret == LONG_MAX) {
        errno = EISDIR;
        goto fail;
    }
    buf_len = lret;
    if (fseek(f, 0, SEEK_SET) < 0)
        goto fail;
    if (ctx)
        buf = js_malloc(ctx, buf_len + 1);
    else
        buf = malloc(buf_len + 1);
    if (!buf)
        goto fail;
    if (fread(buf, 1, buf_len, f) != buf_len) {
        errno = EIO;
        if (ctx)
            js_free(ctx, buf);
        else
            free(buf);
    fail:
        fclose(f);
        return NULL;
    }
    buf[buf_len] = '\0';
    fclose(f);
    *pbuf_len = buf_len;
    return buf;
}

JSValue js_std_compile(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
	const char *buf, *filename = NULL;
	size_t buf_len;
  	JSValue res;
	DynBuf dbuf;

  	buf = JS_ToCStringLen(ctx, &buf_len, argv[0]);

  	if (!buf)
        return JS_EXCEPTION;

  	js_std_dbuf_init(ctx, &dbuf);

  	dbuf_putstr(&dbuf, "(function () {\n");
  	dbuf_put(&dbuf, (const uint8_t*)buf, buf_len);
  	dbuf_putstr(&dbuf, "})");
  	dbuf_putc(&dbuf, 0);

    JS_FreeCString(ctx, buf);

    if (JS_IsString(argv[1])) {
    	if (!(filename = JS_ToCString(ctx, argv[1]))) {
    		dbuf_free(&dbuf);
	        return JS_EXCEPTION;
    	}
    }

	if (dbuf.error) {
        res = JS_ThrowOutOfMemory(ctx);
    } else {    	
	  	res = JS_Eval(ctx, (char*)dbuf.buf, dbuf.size, filename ? filename : "<script>", JS_EVAL_TYPE_GLOBAL);
    }

    JS_FreeCString(ctx, filename);
    dbuf_free(&dbuf);

    return res;
}

/* read file */
JSValue js_std_readfile(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    uint8_t *buf;
    const char *filename;
    JSValue ret;
    size_t buf_len;
	int err;
    
    if (!JS_IsString(argv[0])) {
    	return js_std_throw_error(ctx, "file path must be string", 0);	
    }
    if (!(filename = JS_ToCString(ctx, argv[0])))
        return JS_EXCEPTION;
    buf = js_std_load_file(ctx, &buf_len, filename);
    if (!buf) {
    	ret = js_std_throw_error(ctx, strerror(errno), errno);
    } else {
	    ret = JS_NewStringLen(ctx, (char*)buf, buf_len); 
    }
    js_free(ctx, buf);
    JS_FreeCString(ctx, filename);
    return ret;
}

/* read file */
JSValue js_std_readfile_raw(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    uint8_t *buf;
    const char *filename;
    JSValue ret;
    size_t buf_len;
	int err;
    
    if (!JS_IsString(argv[0])) {
    	return js_std_throw_error(ctx, "file path must be string", 0);	
    }
    if (!(filename = JS_ToCString(ctx, argv[0])))
        return JS_EXCEPTION;
    buf = js_std_load_file(NULL, &buf_len, filename);
    JS_FreeCString(ctx, filename);
    if (!buf) {
    	return js_std_throw_error(ctx, strerror(errno), errno);
    }

	return js_std_blob_ptr(ctx, buf, buf_len, 0, 0);
}

JSValue js_std_writefile(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
	FILE *f;
	const char *filename, *data;
	size_t data_length;
	JSValue ret;
	size_t total_written, written;

	if (!JS_IsString(argv[0])) {
    	return js_std_throw_error(ctx, "file path must be string", 0);	
    }
    if (!JS_IsString(argv[1])) {
    	return js_std_throw_error(ctx, "file data must be string", 0);	
    }

	if (!(filename = JS_ToCString(ctx, argv[0]))) {
		return JS_EXCEPTION;
	}

	f = fopen(filename, "wb+");
	if (!f) {
		JS_FreeCString(ctx, filename);
		return js_std_throw_error(ctx, strerror(errno), errno);
	}

	data = JS_ToCStringLen(ctx, &data_length, argv[1]);
	if (!data) {
		JS_FreeCString(ctx, filename);
		return JS_EXCEPTION;
	}

	total_written = 0;
	while (total_written != data_length) {
		written = fwrite(data + total_written, 1, data_length - total_written, f);
		if (written == 0 && ferror(f)) {
			ret = js_std_throw_error(ctx, strerror(errno), errno);
			goto done;
		}
		total_written += written;
	}

	ret = JS_NewInt32(ctx, total_written);
done:
	fclose(f);
	JS_FreeCString(ctx, filename);
	JS_FreeCString(ctx, data);
	return ret;
}

JSValue js_std_exists(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
	int checkdir, status;
	struct stat st;
	const char *path;
	if (!(path = JS_ToCString(ctx, argv[0]))) {
		return JS_EXCEPTION;
	}
	checkdir = JS_IsUndefined(argv[1]) ? 0 : JS_ToBool(ctx, argv[1]);
	if (checkdir == -1) {
		return JS_EXCEPTION;
	}
	status = stat(path, &st);
	if (status) {
		return JS_FALSE;
	} else if (checkdir) {
		return JS_MKVAL(JS_TAG_BOOL, (st.st_mode & S_IFMT) == S_IFDIR);
	} else {
		return JS_TRUE;
	}
}

JSValue js_std_exit(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
	int status;
    if (JS_ToInt32(ctx, &status, argv[0]))
        status = -1;
    exit(status);
    return JS_UNDEFINED;
}

JSValue js_std_getenv(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
	const char *value, *name;
	if (!JS_IsString(argv[0])) {
    	return JS_UNDEFINED;	
    }
    if (!(name = JS_ToCString(ctx, argv[0]))) {
    	return JS_EXCEPTION;
    }
    value = getenv(name);
    JS_FreeCString(ctx, name);
    return value ? JS_NewString(ctx, value) : JS_UNDEFINED;
}

JSValue js_std_getcwd(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
	static char cwd[PATH_MAX];
	if (getcwd(cwd, sizeof(cwd)) == NULL) {
		return js_std_throw_error(ctx, strerror(errno), errno);
	}
	return JS_NewString(ctx, cwd);
}

JSValue js_std_realpath(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
	static char buf[PATH_MAX];
	const char *path;
	JSValue ret;

	if (!JS_IsString(argv[0])) {
    	return js_std_throw_error(ctx, "file path must be string", 0);
    }
    if (!(path = JS_ToCString(ctx, argv[0]))) {
    	return JS_EXCEPTION;
    }
	if (!realpath(path, buf)) {
		ret = js_std_throw_error(ctx, strerror(errno), errno);
	} else {
		ret = JS_NewString(ctx, buf);
	}
	return ret;
}

JSValue js_std_mkdir(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
	int32_t mode;
	const char *path;
	JSValue ret;

	if (!JS_IsString(argv[0])) {
    	return js_std_throw_error(ctx, "file path must be string", 0);
    }
    if (!(path = JS_ToCString(ctx, argv[0]))) {
    	return JS_EXCEPTION;
    }
    mode = 0777;
    if (!JS_IsUndefined(argv[1]) && JS_ToInt32(ctx, &mode, argv[1])) {
    	JS_FreeCString(ctx, path);
    	return JS_EXCEPTION;
    }
	ret = JS_UNDEFINED;
    if (mkdir(path, mode)) {
    	ret = js_std_throw_error(ctx, strerror(errno), errno);
    }
    JS_FreeCString(ctx, path);
    return ret;
}

JSValue js_std_remove(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
	const char *path;
	JSValue ret;

	if (!JS_IsString(argv[0])) {
    	return js_std_throw_error(ctx, "file path must be string", 0);
    }
    if (!(path = JS_ToCString(ctx, argv[0]))) {
    	return JS_EXCEPTION;
    }
    ret = JS_UNDEFINED;
    if (remove(path)) {
    	ret = js_std_throw_error(ctx, strerror(errno), errno);
    }
    JS_FreeCString(ctx, path);
    return ret;
}

JSValue js_std_readdir(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
	int len = 0;
	struct dirent *dent;
	DIR *dd;
	const char *path;
	JSValue ret;

	if (!JS_IsString(argv[0])) {
    	return js_std_throw_error(ctx, "file path must be string", 0);
    }
    if (!(path = JS_ToCString(ctx, argv[0]))) {
    	return JS_EXCEPTION;
    }

    if (!(dd = opendir(path))) {
    	JS_FreeCString(ctx, path);
    	return js_std_throw_error(ctx, strerror(errno), errno);
    }
	JS_FreeCString(ctx, path);

    ret = JS_NewArray(ctx);
    if (JS_IsException(ret)) {
        return JS_EXCEPTION;
    }

    while (1) {
    	errno = 0;
    	if (!(dent = readdir(dd))) {
    		if (errno != 0) {
				JS_FreeValue(ctx, ret); // release array
    			ret = js_std_throw_error(ctx, strerror(errno), errno);
    		}
			break;
    	}
    	JS_DefinePropertyValueUint32(ctx, ret, len++, JS_NewString(ctx, dent->d_name), JS_PROP_C_W_E);
    }
    closedir(dd);
    return ret;
}

JSValue js_std_stat(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
	struct stat st;
	const char *path;
	JSValue ret;

	if (!JS_IsString(argv[0])) {
    	return js_std_throw_error(ctx, "file path must be string", 0);
    }
    if (!(path = JS_ToCString(ctx, argv[0]))) {
    	return JS_EXCEPTION;
    }

    if (stat(path, &st)) {
    	JS_FreeCString(ctx, path);
    	return js_std_throw_error(ctx, strerror(errno), errno);
    }

	JS_FreeCString(ctx, path);

    ret = JS_NewObject(ctx);
    if (JS_IsException(ret)) {
        return JS_EXCEPTION;	
    }
    JS_DefinePropertyValueStr(ctx, ret, "isFile", JS_NewBool(ctx, (st.st_mode & S_IFMT) == S_IFREG), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, ret, "isDirectory", JS_NewBool(ctx, (st.st_mode & S_IFMT) == S_IFDIR), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, ret, "size", JS_NewInt64(ctx, st.st_size), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, ret, "atime", JS_NewInt64(ctx, msfromtime(&st.st_atim)), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, ret, "mtime", JS_NewInt64(ctx, msfromtime(&st.st_mtim)), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, ret, "ctime", JS_NewInt64(ctx, msfromtime(&st.st_ctim)), JS_PROP_C_W_E);

    return ret;
}

/* update file times */
JSValue js_std_utimes(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
	int64_t atime;
	int64_t mtime;
	const char *path;
	struct timeval timevals[2];
	JSValue ret;

	if (!JS_IsString(argv[0])) {
    	return js_std_throw_error(ctx, "file path must be string", 0);
    }
    if (!(path = JS_ToCString(ctx, argv[0]))) {
    	return JS_EXCEPTION;
    }
    if (JS_ToInt64(ctx, &atime, argv[1]) || JS_ToInt64(ctx, &atime, argv[1])) {
    	JS_FreeCString(ctx, path);
    	return JS_EXCEPTION;
    }

	timefromms(timevals, atime);
	timefromms(timevals + 1, mtime);

	ret = JS_UNDEFINED;
	if (utimes(path, timevals)) {
		ret = js_std_throw_error(ctx, strerror(errno), errno);
	}
	JS_FreeCString(ctx, path);
	return ret;
}

JSValue js_std_eval_bytecode(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
	Blob *b;
	if (!JS_IsObject(argv[0])) {
		return js_std_throw_error(ctx, "expected object", 0);
	}
	if (!JS_IsUndefined(argv[1]) && !JS_IsObject(argv[1])) {
		return JS_ThrowTypeError(ctx, "expected local variables to be an object");
	}
	b = JS_GetOpaque2(ctx, argv[0], blobClassId);
	if (!b) {
		return JS_EXCEPTION;
	}
	return JS_EvalFunction2(ctx, JS_ReadObject(ctx, b->data, b->ptr.size, JS_READ_OBJ_BYTECODE), argv[1]);
}

JSValue js_std_eval_module(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
	JSValue res;
	size_t buf_len;
	const char *buf;
	if (!JS_IsUndefined(argv[1]) && !JS_IsObject(argv[1])) {
		return JS_ThrowTypeError(ctx, "expected local variables to be an object");
	}
	buf = JS_ToCStringLen(ctx, &buf_len, argv[0]);
	if (!buf) {
		return JS_EXCEPTION;
	}
	res = JS_Eval(ctx, buf, buf_len, "<module>", JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
	JS_FreeCString(ctx, buf);
	if (JS_IsException(res)) {
		return JS_EXCEPTION;
	}
	return JS_EvalFunction2(ctx, res, argv[1]);
}
