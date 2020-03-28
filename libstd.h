#ifndef DUMBTS_LIBC_H
#define DUMBTS_LIBC_H

#include <stdio.h>
#include <stdlib.h>
#include <quickjs.h>

JSValue js_std_throw_error(JSContext *ctx, const char *message, int errcode);
JSValue js_std_sprintf(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_std_printf(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
void js_std_dump_error(JSContext *ctx);
uint8_t *js_std_load_file(JSContext *ctx, size_t *pbuf_len, const char *filename);
JSValue js_std_print(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_std_compile(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_std_readfile(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_std_readfile_raw(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_std_writefile(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_std_exists(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_std_remove(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_std_getcwd(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_std_mkdir(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_std_exit(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_std_getenv(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_std_realpath(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_std_readdir(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_std_stat(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_std_utimes(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_std_eval_bytecode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_std_blob_ptr(JSContext *ctx, void *data, size_t size, int rt_alloc, int no_free);
JSValue js_std_eval_module(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

#endif /* DUMBTS_LIBC_H */
