/*
 * DumbJS stand alone interpreter
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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__linux__)
#include <malloc.h>
#endif
#include <limits.h>

#include "cutils.h"
#include "quickjs.h"

static int in_argc;
static char **in_argv;

#if defined(WIN32) || defined(_WIN32) || defined __CYGWIN__
  #define PATH_SEPARATOR '\\' 
#else 
  #define PATH_SEPARATOR '/'
#endif
#define MAX_OSPATH 256
#define ISPATHSEP(X) ((X) == '\\' || (X) == '/' || (X) == PATH_SEPARATOR)

const char* get_basename(const char *path, int stripExt)
{
    static char base[MAX_OSPATH] = { 0 };
    int length = (int)strlen(path) - 1;
    // skip trailing slashes
    while (length > 0 && (ISPATHSEP(path[length]))) 
        length--;
    while (length > 0 && !(ISPATHSEP(path[length - 1])))
        length--;
    strncpy(base, &path[length], MAX_OSPATH);
    length = strlen(base) - 1;
    // strip trailing slashes
    while (length > 0 && (ISPATHSEP(base[length])))
        base[length--] = '\0';
    if (stripExt) {
        char *ext = strrchr(base, '.');
        *ext = 0;
    }
    return base;
}

static const char *get_filename_ext(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return "";
    return dot + 1;
}

static JSValue js_print(JSContext *ctx, JSValueConst this_val,
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
    js_print(ctx, JS_NULL, 1, (JSValueConst *)&exception_val);
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

static void js_std_dump_error(JSContext *ctx)
{
    JSValue exception_val;
    
    exception_val = JS_GetException(ctx);
    js_std_dump_error1(ctx, exception_val, TRUE);
    JS_FreeValue(ctx, exception_val);
}

static uint8_t *js_load_file(JSContext *ctx, size_t *pbuf_len, const char *filename)
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

/* load and evaluate a file */
static JSValue js_loadScript(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    uint8_t *buf;
    const char *filename;
    JSValue ret;
    size_t buf_len;
    
    filename = JS_ToCString(ctx, argv[0]);
    if (!filename)
        return JS_EXCEPTION;
    buf = js_load_file(ctx, &buf_len, filename);
    if (!buf) {
        JS_ThrowReferenceError(ctx, "could not load '%s'", filename);
        JS_FreeCString(ctx, filename);
        return JS_EXCEPTION;
    }
    ret = JS_Eval(ctx, (char *)buf, buf_len, filename,
                  JS_EVAL_TYPE_GLOBAL);
    js_free(ctx, buf);
    JS_FreeCString(ctx, filename);
    return ret;
}

/* eval script */
static JSValue js_evalScript(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    const char *script;
    JSValue ret;
    size_t buf_len;
    
    script = JS_ToCStringLen(ctx, &buf_len, argv[0]);
    if (!script)
        return JS_EXCEPTION;
    ret = JS_Eval(ctx, script, buf_len, "<script>", JS_EVAL_TYPE_GLOBAL);
    JS_FreeCString(ctx, script);
    return ret;
}

/* getFile */
static JSValue js_getFile(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    uint8_t *buf;
    const char *filename;
    JSValue ret;
    size_t buf_len;
    
    filename = JS_ToCString(ctx, argv[0]);
    if (!filename)
        return JS_EXCEPTION;
    buf = js_load_file(ctx, &buf_len, filename);
    if (!buf) {
        JS_ThrowReferenceError(ctx, "could not load '%s'", filename);
        JS_FreeCString(ctx, filename);
        return JS_EXCEPTION;
    }
    ret = JS_NewStringLen(ctx, (char*)buf, buf_len); 
    js_free(ctx, buf);
    JS_FreeCString(ctx, filename);
    return ret;
}

static JSValue js_exit(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    int status;
    JS_ToInt32(ctx, &status, argv[0]);
    exit(status);
    return JS_UNDEFINED;
}

static JSValue js_evalScriptContext(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv);

static void js_std_add_helpers(JSContext *ctx, int argc, char **argv)
{
    JSValue global_obj, console, args;
    int i;

    /* XXX: should these global definitions be enumerable? */
    global_obj = JS_GetGlobalObject(ctx);

    console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log",
                      JS_NewCFunction(ctx, js_print, "log", 1));
    JS_SetPropertyStr(ctx, global_obj, "console", console);

    /* same methods as the mozilla JS shell */
    args = JS_NewArray(ctx);
    for(i = 0; i < argc; i++) {
        JS_SetPropertyUint32(ctx, args, i, JS_NewString(ctx, argv[i]));
    }
    JS_SetPropertyStr(ctx, global_obj, "scriptArgs", args);

    JS_SetPropertyStr(ctx, global_obj, "print",
                      JS_NewCFunction(ctx, js_print, "print", 1));
    JS_SetPropertyStr(ctx, global_obj, "__loadScript",
                      JS_NewCFunction(ctx, js_loadScript, "__loadScript", 1));
    JS_SetPropertyStr(ctx, global_obj, "__evalScript",
                      JS_NewCFunction(ctx, js_evalScript, "__evalScript", 1));
    JS_SetPropertyStr(ctx, global_obj, "__evalScriptContext",
                      JS_NewCFunction(ctx, js_evalScriptContext, "__evalScriptContext", 1));
    JS_SetPropertyStr(ctx, global_obj, "__getFile",
                      JS_NewCFunction(ctx, js_getFile, "__getFile", 1));
    JS_SetPropertyStr(ctx, global_obj, "__exit",
                      JS_NewCFunction(ctx, js_exit, "__exit", 1));
    
    JS_FreeValue(ctx, global_obj);
}

/* main loop which calls the user JS callbacks */
void js_std_loop(JSContext *ctx)
{
    JSContext *ctx1;
    int err;

    for(;;) {
        /* execute the pending jobs */
        for(;;) {
            err = JS_ExecutePendingJob(JS_GetRuntime(ctx), &ctx1);
            if (err <= 0) {
                if (err < 0) {
                    js_std_dump_error(ctx1);
                }
                break;
            }
        }
        break;
    }
}

/* evalScriptContext */

static JSValue js_evalScriptContext(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv) 
{
    int i;
    const char *script;
    JSValue ret, new_ret;
    JSContext *new_ctx;
    size_t buf_len;
    
    ret = JS_UNDEFINED;
    new_ctx = JS_NewContext(JS_GetRuntime(ctx));
    if (!new_ctx) {
        return JS_EXCEPTION;
    }
    
    js_std_add_helpers(new_ctx, in_argc, in_argv);
    
    for (i = 0; i < argc; i++) {
        script = JS_ToCStringLen(ctx, &buf_len, argv[i]);
        if (!script) {
            JS_ThrowInternalError(ctx, "could not allocate new context");
            return JS_EXCEPTION;
        }
        new_ret = JS_Eval(new_ctx, script, buf_len, "<script>", JS_EVAL_TYPE_GLOBAL);
        JS_FreeCString(ctx, script);

        if (JS_IsException(new_ret)) {
            JSValue err = JS_GetException(new_ctx);
            const char *stack = JS_ToCString(ctx, err);
            ret = JS_ThrowTypeError(ctx, "%s", stack);
            JS_FreeCString(ctx, stack);
            JS_FreeValue(ctx, err);
            JS_FreeValue(new_ctx, new_ret);
            break;
        }

        JS_FreeValue(new_ctx, new_ret);
    }
    
    JS_FreeContext(new_ctx);

    return ret;
}

// --------------------------------------------------------- //

static int eval_buf(JSContext *ctx, const void *buf, int buf_len,
                    const char *filename, int eval_flags)
{
    JSValue val;
    int ret;

    if ((eval_flags & JS_EVAL_TYPE_MASK) == JS_EVAL_TYPE_MODULE) {
        /* for the modules, we compile then run to be able to set
           import.meta */
        val = JS_Eval(ctx, buf, buf_len, filename,
                      eval_flags | JS_EVAL_FLAG_COMPILE_ONLY);
        if (!JS_IsException(val)) {
            // js_module_set_import_meta(ctx, val, TRUE, TRUE);
            val = JS_EvalFunction(ctx, val);
        }
    } else {
        val = JS_Eval(ctx, buf, buf_len, filename, eval_flags);
    }
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
    int ret, eval_flags;
    size_t buf_len;
    
    buf = js_load_file(ctx, &buf_len, filename);
    if (!buf) {
        perror(filename);
        exit(1);
    }

    eval_flags = JS_EVAL_TYPE_GLOBAL;
    ret = eval_buf(ctx, buf, buf_len, filename, eval_flags);
    js_free(ctx, buf);
    return ret;
}

static int compile_file(JSContext *ctx, const char *filename, uint8_t **output, size_t *output_size, int as_module)
{
    uint8_t *buf;
    int ret, eval_flags;
    size_t buf_len;
    JSValue obj;
    
    buf = js_load_file(ctx, &buf_len, filename);
    if (!buf) {
        perror(filename);
        exit(1);
    }
    if (as_module) {
        eval_flags = JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY;
    } else {
        eval_flags = JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY;
    }
    obj = JS_Eval(ctx, (const char*)buf, buf_len, filename, eval_flags);
    if (JS_IsException(obj)) {
        js_std_dump_error(ctx);
        ret = -1;
    } else {
        ret = 0;
    }
    *output = JS_WriteObject(ctx, output_size, obj, JS_WRITE_OBJ_BYTECODE);
    if (!*output) {
        js_std_dump_error(ctx);
        ret = 0;
    }
    JS_FreeValue(ctx, obj);
    js_free(ctx, buf);
    return ret;
}

#if defined(__APPLE__)
#define MALLOC_OVERHEAD  0
#else
#define MALLOC_OVERHEAD  8
#endif

struct trace_malloc_data {
    uint8_t *base;
};

static inline unsigned long long js_trace_malloc_ptr_offset(uint8_t *ptr,
                                                struct trace_malloc_data *dp)
{
    return ptr - dp->base;
}

/* default memory allocation functions with memory limitation */
static inline size_t js_trace_malloc_usable_size(void *ptr)
{
#if defined(__APPLE__)
    return malloc_size(ptr);
#elif defined(_WIN32)
    return _msize(ptr);
#elif defined(EMSCRIPTEN)
    return 0;
#elif defined(__linux__)
    return malloc_usable_size(ptr);
#else
    /* change this to `return 0;` if compilation fails */
    return malloc_usable_size(ptr);
#endif
}

static void __printf_format(2, 3)
    js_trace_malloc_printf(JSMallocState *s, const char *fmt, ...)
{
    va_list ap;
    int c;

    va_start(ap, fmt);
    while ((c = *fmt++) != '\0') {
        if (c == '%') {
            /* only handle %p and %zd */
            if (*fmt == 'p') {
                uint8_t *ptr = va_arg(ap, void *);
                if (ptr == NULL) {
                    printf("NULL");
                } else {
                    printf("H%+06lld.%zd",
                           js_trace_malloc_ptr_offset(ptr, s->opaque),
                           js_trace_malloc_usable_size(ptr));
                }
                fmt++;
                continue;
            }
            if (fmt[0] == 'z' && fmt[1] == 'd') {
                size_t sz = va_arg(ap, size_t);
                printf("%zd", sz);
                fmt += 2;
                continue;
            }
        }
        putc(c, stdout);
    }
    va_end(ap);
}

static void js_trace_malloc_init(struct trace_malloc_data *s)
{
    free(s->base = malloc(8));
}

static void *js_trace_malloc(JSMallocState *s, size_t size)
{
    void *ptr;

    /* Do not allocate zero bytes: behavior is platform dependent */
    assert(size != 0);

    if (unlikely(s->malloc_size + size > s->malloc_limit))
        return NULL;
    ptr = malloc(size);
    js_trace_malloc_printf(s, "A %zd -> %p\n", size, ptr);
    if (ptr) {
        s->malloc_count++;
        s->malloc_size += js_trace_malloc_usable_size(ptr) + MALLOC_OVERHEAD;
    }
    return ptr;
}

static void js_trace_free(JSMallocState *s, void *ptr)
{
    if (!ptr)
        return;

    js_trace_malloc_printf(s, "F %p\n", ptr);
    s->malloc_count--;
    s->malloc_size -= js_trace_malloc_usable_size(ptr) + MALLOC_OVERHEAD;
    free(ptr);
}

static void *js_trace_realloc(JSMallocState *s, void *ptr, size_t size)
{
    size_t old_size;

    if (!ptr) {
        if (size == 0)
            return NULL;
        return js_trace_malloc(s, size);
    }
    old_size = js_trace_malloc_usable_size(ptr);
    if (size == 0) {
        js_trace_malloc_printf(s, "R %zd %p\n", size, ptr);
        s->malloc_count--;
        s->malloc_size -= old_size + MALLOC_OVERHEAD;
        free(ptr);
        return NULL;
    }
    if (s->malloc_size + size - old_size > s->malloc_limit)
        return NULL;

    js_trace_malloc_printf(s, "R %zd %p", size, ptr);

    ptr = realloc(ptr, size);
    js_trace_malloc_printf(s, " -> %p\n", ptr);
    if (ptr) {
        s->malloc_size += js_trace_malloc_usable_size(ptr) - old_size;
    }
    return ptr;
}

static const JSMallocFunctions trace_mf = {
    js_trace_malloc,
    js_trace_free,
    js_trace_realloc,
#if defined(__APPLE__)
    malloc_size,
#elif defined(_WIN32)
    (size_t (*)(const void *))_msize,
#elif defined(EMSCRIPTEN)
    NULL,
#elif defined(__linux__)
    (size_t (*)(const void *))malloc_usable_size,
#else
    /* change this to `NULL,` if compilation fails */
    malloc_usable_size,
#endif
};

#define PROG_NAME "djs"

void help(void)
{
    printf("DumbJS version " CONFIG_VERSION "\n"
           "usage: " PROG_NAME " [options] [file [args]]\n"
           "-h  --help         list options\n"
           "-c  --compile      precompile script\n"
           "-cm --compile-mod  precompile script as module scoped\n"
           "-b  --binary       load precompiled script\n" 
           "-e  --eval EXPR    evaluate EXPR\n"
           "-I  --include file include an additional file\n"
           "-T  --trace        trace memory allocation\n"
           "-d  --dump         dump the memory usage stats\n"
           "    --memory-limit n       limit the memory usage to 'n' bytes\n"
           "-q  --quit         just instantiate the interpreter and quit\n");
    exit(1);
}

int main(int argc, char **argv)
{
    JSRuntime *rt;
    JSContext *ctx;
    struct trace_malloc_data trace_data = { NULL };
    int optind;
    char *expr = NULL;
    int dump_memory = 0;
    int trace_memory = 0;
    int empty_run = 0;
    size_t memory_limit = 0;
    char *include_list[32];
    int i, include_count = 0;
    int precomile = 0;
    int as_mod = 0;
    int force_loadbin = 0;
    uint8_t *output;
    size_t output_size;
    const char *filename;
    uint8_t *buf;
    size_t buf_len;
    JSValue obj, val;

    in_argc = 0;
    in_argv = NULL;
    
    /* cannot use getopt because we want to pass the command line to
       the script */
    optind = 1;
    while (optind < argc && *argv[optind] == '-') {
        char *arg = argv[optind] + 1;
        const char *longopt = "";
        /* a single - is not an option, it also stops argument scanning */
        if (!*arg)
            break;
        optind++;
        if (*arg == '-') {
            longopt = arg + 1;
            arg += strlen(arg);
            /* -- stops argument scanning */
            if (!*longopt)
                break;
        }
        for (; *arg || *longopt; longopt = "") {
            char opt = *arg;
            if (opt)
                arg++;
            if (opt == 'h' || opt == '?' || !strcmp(longopt, "help")) {
                help();
                continue;
            }
            if (opt == 'e' || !strcmp(longopt, "eval")) {
                if (*arg) {
                    expr = arg;
                    break;
                }
                if (optind < argc) {
                    expr = argv[optind++];
                    break;
                }
                fprintf(stderr, PROG_NAME ": missing expression for -e\n");
                exit(2);
            }
            if (opt == 'I' || !strcmp(longopt, "include")) {
                if (optind >= argc) {
                    fprintf(stderr, "expecting filename");
                    exit(1);
                }
                if (include_count >= countof(include_list)) {
                    fprintf(stderr, "too many included files");
                    exit(1);
                }
                include_list[include_count++] = argv[optind++];
                continue;
            }
            if (opt == 'c' || !strncmp(longopt, "compile", 7)) {
                precomile++;
                if ((*arg == 'm' && arg++) || !strcmp(longopt, "compile-mod")) {
                    as_mod++;
                }
                continue;
            }
            if (opt == 'b') {
                force_loadbin++;
                continue;
            }
            if (opt == 'd' || !strcmp(longopt, "dump")) {
                dump_memory++;
                continue;
            }
            if (opt == 'T' || !strcmp(longopt, "trace")) {
                trace_memory++;
                continue;
            }
            if (opt == 'q' || !strcmp(longopt, "quit")) {
                empty_run++;
                continue;
            }
            if (!strcmp(longopt, "memory-limit")) {
                if (optind >= argc) {
                    fprintf(stderr, "expecting memory limit");
                    exit(1);
                }
                memory_limit = (size_t)strtod(argv[optind++], NULL);
                continue;
            }
            if (opt) {
                fprintf(stderr, PROG_NAME ": unknown option '-%c'\n", opt);
            } else {
                fprintf(stderr, PROG_NAME ": unknown option '--%s'\n", longopt);
            }
            help();
        }
    }

    if (trace_memory) {
        js_trace_malloc_init(&trace_data);
        rt = JS_NewRuntime2(&trace_mf, &trace_data);
    } else {
        rt = JS_NewRuntime();
    }
    if (!rt) {
        fprintf(stderr, PROG_NAME ": cannot allocate JS runtime\n");
        exit(2);
    }
    if (memory_limit != 0)
        JS_SetMemoryLimit(rt, memory_limit);
    ctx = JS_NewContext(rt);
    if (!ctx) {
        fprintf(stderr, PROG_NAME ": cannot allocate JS context\n");
        exit(2);
    }
    
    if (!empty_run) {
        js_std_add_helpers(ctx, argc - optind, argv + optind);
        in_argc = argc - optind;
        in_argv = argv + optind;

        for(i = 0; i < include_count; i++) {
            if (eval_file(ctx, include_list[i]))
                goto fail;
        }

        if (expr) {
            if (eval_buf(ctx, expr, strlen(expr), "<cmdline>", 0))
                goto fail;
        } else
        if (optind >= argc) {
            help();
            exit(0);
        } else if (precomile) {
            for (i = optind; i < argc; i++) {
                filename = argv[i];
                if (!compile_file(ctx, filename, &output, &output_size, as_mod)) {
                    int writen;
                    char outpath[MAX_OSPATH] = { 0 };
                    sprintf(outpath, "./%s.jsbin", get_basename(filename, 1));
                    FILE *f = fopen(outpath, "wb");
                    if (!f) {
                        perror(filename);
                        js_free(ctx, output);
                        goto fail;
                    }
                    writen = fwrite(output, 1, output_size, f);
                    if (writen != output_size) {
                        perror(filename);
                        js_free(ctx, output);
                        goto fail;
                    }
                    fclose(f);
                    js_free(ctx, output);
                } else {
                    printf("could not precompile script\n");
                    goto fail;
                }
            }
        } else if (force_loadbin) {
loadbin:
            filename = argv[optind];
            buf = js_load_file(ctx, &buf_len, filename);
            if (!buf) {
                perror(filename);
                goto fail;
            }
            obj = JS_ReadObject(ctx, buf, buf_len, JS_READ_OBJ_BYTECODE);
            if (JS_IsException(obj))
                goto exception;
            val = JS_EvalFunction(ctx, obj);
            if (JS_IsException(val)) {
            exception:
                js_std_dump_error(ctx);
                js_free(ctx, buf);
                goto fail;
            }
            JS_FreeValue(ctx, val);
            js_free(ctx, buf);
        } else {
            filename = argv[optind];
            if (strcmp(get_filename_ext(filename), "jsbin") == 0) {
                goto loadbin;
            } 
            if (eval_file(ctx, filename))
                goto fail;
        }
        js_std_loop(ctx);
    }
    
    if (dump_memory) {
        JSMemoryUsage stats;
        JS_ComputeMemoryUsage(rt, &stats);
        JS_DumpMemoryUsage(stdout, &stats, rt);
    }
    
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);

    if (empty_run && dump_memory) {
        clock_t t[5];
        double best[5];
        int i, j;
        for (i = 0; i < 100; i++) {
            t[0] = clock();
            rt = JS_NewRuntime();
            t[1] = clock();
            ctx = JS_NewContext(rt);
            t[2] = clock();
            JS_FreeContext(ctx);
            t[3] = clock();
            JS_FreeRuntime(rt);
            t[4] = clock();
            for (j = 4; j > 0; j--) {
                double ms = 1000.0 * (t[j] - t[j - 1]) / CLOCKS_PER_SEC;
                if (i == 0 || best[j] > ms)
                    best[j] = ms;
            }
        }
        printf("\nInstantiation times (ms): %.3f = %.3f+%.3f+%.3f+%.3f\n",
               best[1] + best[2] + best[3] + best[4],
               best[1], best[2], best[3], best[4]);
    }
    return 0;

 fail:
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return 1;
}
