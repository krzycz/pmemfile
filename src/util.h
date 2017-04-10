/*
 * Copyright 2014-2017, Intel Corporation
 * Copyright (c) 2016, Microsoft Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * util.h -- internal definitions for util module
 */

#ifndef PMEMFILE_UTIL_H
#define PMEMFILE_UTIL_H 1

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Macro calculates number of elements in given table
 */
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x)	(sizeof(x) / sizeof((x)[0]))
#endif

#if !defined(likely)
#if defined(__GNUC__)
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x) (!!(x))
#define unlikely(x) (!!(x))
#endif
#endif

#ifndef _WIN32
#define DIR_SEPARATOR '/'
#else
#define DIR_SEPARATOR '\\'
#endif

#define COMPILE_ERROR_ON(cond) ((void)sizeof(char[(cond) ? -1 : 1]))

#define pf_always_inline __attribute__((always_inline)) inline
#define pf_printf_like(fmt_arg_num, arg_num) \
	__attribute__((format(printf, fmt_arg_num, arg_num)))
#define pf_noreturn __attribute__((noreturn))
#define pf_constructor static __attribute__((constructor))
#define pf_destructor static __attribute__((destructor))
#define pf_used_var __attribute__((used))

#ifdef __cplusplus
}
#endif

#endif /* util.h */
