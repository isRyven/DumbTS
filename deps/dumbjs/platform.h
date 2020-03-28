#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#define force_inline inline __attribute__((always_inline))
#define no_inline __attribute__((noinline))
#define __maybe_unused __attribute__((unused))
#define __packed( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#define __printf_format(format_param, dots_param) __attribute__((format(printf, format_param, dots_param)))
#define __exception __attribute__((warn_unused_result))

#include <sys/time.h>
#include <time.h>

#elif defined(_MSC_VER)
#define likely(x)     (x)
#define unlikely(x)   (x)
#define force_inline  __forceinline
#define no_inline __declspec(noinline)
#define __maybe_unused
#define __packed( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop))
#define __printf_format(format_param, dots_param)
#define __exception 

#include <sys/timeb.h>
#include <sys/types.h>
#include <winsock2.h>

    int gettimeofday(struct timeval* t, void* timezone);
#define __need_clock_t
#include <time.h>

    /* Structure describing CPU time used by a process and its children.  */
    struct tms
    {
        clock_t tms_utime;          /* User CPU time.  */
        clock_t tms_stime;          /* System CPU time.  */

        clock_t tms_cutime;         /* User CPU time of dead children.  */
        clock_t tms_cstime;         /* System CPU time of dead children.  */
    };

    /* Store the CPU time used by this process and all its
       dead children (and their dead children) in BUFFER.
       Return the elapsed real time, or (clock_t) -1 for errors.
       All times are in CLK_TCKths of a second.  */
    clock_t times(struct tms* __buffer);

    typedef long long suseconds_t;

    static inline int gettimeofday(struct timeval* t, void* timezone)
    {
        struct _timeb timebuffer;
        _ftime(&timebuffer);
        t->tv_sec = timebuffer.time;
        t->tv_usec = 1000 * timebuffer.millitm;
        return 0;
    }

    static inline clock_t times(struct tms* __buffer) {

        __buffer->tms_utime = clock();
        __buffer->tms_stime = 0;
        __buffer->tms_cstime = 0;
        __buffer->tms_cutime = 0;
        return __buffer->tms_utime;
    }

#include <BaseTsd.h>
    typedef SSIZE_T ssize_t;

#include <intrin.h>

    static inline int __builtin_ctz(uint32_t x) {
        unsigned long ret;
        _BitScanForward(&ret, x);
        return (int)ret;
    }

    static inline int __builtin_ctzll(unsigned long long x) {
        unsigned long ret;
        _BitScanForward64(&ret, x);
        return (int)ret;
    }

    static inline int __builtin_ctzl(unsigned long x) {
        return sizeof(x) == 8 ? __builtin_ctzll(x) : __builtin_ctz((uint32_t)x);
    }

    static inline int __builtin_clz(uint32_t x) {
        //unsigned long ret;
        //_BitScanReverse(&ret, x);
        //return (int)(31 ^ ret);
        return (int)__lzcnt(x);
    }

    static inline int __builtin_clzll(unsigned long long x) {
        //unsigned long ret;
        //_BitScanReverse64(&ret, x);
        //return (int)(63 ^ ret);
        return (int)__lzcnt64(x);
    }

    static inline int __builtin_clzl(unsigned long x) {
        return sizeof(x) == 8 ? __builtin_clzll(x) : __builtin_clz((uint32_t)x);
    }

#endif

#ifdef __cplusplus
} /* extern "C" { */
#endif

#endif /* PLATFORM_H */