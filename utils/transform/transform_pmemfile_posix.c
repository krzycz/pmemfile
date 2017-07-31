/*
 * Copyright 2017, Intel Corporation
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
 * transform_pmemfile_posix.c - transforms the function declarations in
 * pmemfile_posix.h to definitions of wrapper functions.
 */

#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "function_decl_finder.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/*
 * print_type_and_name -- prints "type name" or "type *name"
 */
static void
print_type_and_name(const char *type, const char *name)
{
	if (type[strlen(type) - 1] == '*')
		printf("%s%s", type, name);
	else
		printf("%s %s", type, name);
}

/*
 * print_prototype -- prints the function prototype.
 * The return type is the same as the original function's return type.
 * The name gets a prefix attached to it.
 * The argument list is the same as the orig_prefix function's argument
 * list, except for making sure all arguments have names (see fix_args).
 *
 * Example output:
 * +------------------------------------------+
 * | static inline int                        |
 * | wrapper_pmemfile_link(PMEMfilepool *pfp, |
 * |            const char *oldpath,          |
 * |            const char *newpath)          |
 * +------------------------------------------+
 */
static void
print_prototype(const struct func_desc *desc)
{
	printf("static inline %s\n", desc->return_type.name);
	printf("wrapper_%s(", desc->name);

	if (desc->arg_count == 0)
		printf("void");

	for (int i = 0; i < desc->arg_count; ++i) {
		if (i > 0)
			printf(",\n\t\t");
		print_type_and_name(desc->args[i].type.name,
					desc->args[i].name);
	}
	puts(")");
}

/*
 * print_forward_call -- prints a call to the original function.
 *
 *
 * Example output:
 * +-------------------------+
 * | pmemfile_link(pfp,      |
 * |            oldpath,     |
 * |            newpath);    |
 * +-------------------------+
 */
static void
print_forward_call(const struct func_desc *desc)
{
	printf("%s(", desc->name);
	for (int i = 0; i < desc->arg_count; ++i) {
		if (i > 0)
			printf(",\n\t\t");
		printf("%s", desc->args[i].name);
	}
	puts(");");
}

static bool
is_printable_cstr_type(const char *type_name)
{
	static const char *const accepted_types[] = {
		"const char *"
	};

	for (size_t i = 0; i < ARRAY_SIZE(accepted_types); ++i)
		if (strcmp(type_name, accepted_types[i]) == 0)
			return true;

	return false;
}

static bool
is_printable_cstr_name(const char *name)
{
	static const char *const accepted_names[] = {
		"path",
		"pathname",
		"oldpath",
		"newpath",
		"old_path",
		"new_path"
	};

	for (size_t i = 0; i < ARRAY_SIZE(accepted_names); ++i)
		if (strcmp(name, accepted_names[i]) == 0)
			return true;

	return false;
}

static bool
is_arg_printable_cstr(const char *type_name, const char *name)
{
	return is_printable_cstr_type(type_name) &&
		is_printable_cstr_name(name);
}

static void
print_format(const struct type_desc *type, const char *name)
{
	if (is_arg_printable_cstr(type->name, name))
		fputs("\\\"%s\\\"", stdout);
	else if (strcmp(type->name, "size_t") == 0)
		fputs("%zu", stdout);
	else if (strcmp(type->name, "ptrdiff_t") == 0)
		fputs("%td", stdout);
	else if (strcmp(type->name, "pmemfile_ssize_t") == 0)
		fputs("%zd", stdout); /* assuming it is the same as ssize_t */
	else if (strcmp(type->name, "pmemfile_mode_t") == 0)
		fputs("%3jo", stdout);
	else if (type->is_pointer)
		fputs("%p", stdout);
	else if (strcmp(type->name, "char") == 0)
		fputs((CHAR_MIN == 0) ? "%hhu" : "%hhd", stdout);
	else if (strcmp(type->name, "signed char") == 0)
		fputs("%hhd", stdout);
	else if (strcmp(type->name, "unsigned char") == 0)
		fputs("%hhu", stdout);
	else if (strcmp(type->name, "int") == 0)
		fputs("%d", stdout);
	else if (strcmp(type->name, "unsigned") == 0)
		fputs("%u", stdout);
	else if (strcmp(type->name, "short") == 0)
		fputs("%hd", stdout);
	else if (strcmp(type->name, "unsigned short") == 0)
		fputs("%hu", stdout);
	else if (strcmp(type->name, "long") == 0)
		fputs("%ld", stdout);
	else if (strcmp(type->name, "unsigned long") == 0)
		fputs("%lu", stdout);
	else if (strcmp(type->name, "long long") == 0)
		fputs("%lld", stdout);
	else if (strcmp(type->name, "unsigned long long") == 0)
		fputs("%llu", stdout);
	else if (strcmp(type->name, "intmax_t") == 0)
		fputs("%jd", stdout);
	else if (strcmp(type->name, "uintmax_t") == 0)
		fputs("%ju", stdout);
	else if (strcmp(type->name, "int8_t") == 0)
		fputs("%\" PRIi8 \"", stdout);
	else if (strcmp(type->name, "uint8_t") == 0)
		fputs("%\" PRIu8 \"", stdout);
	else if (strcmp(type->name, "int16_t") == 0)
		fputs("%\" PRIi16 \"", stdout);
	else if (strcmp(type->name, "uint16_t") == 0)
		fputs("%\" PRIu16 \"", stdout);
	else if (strcmp(type->name, "int32_t") == 0)
		fputs("%\" PRIi32 \"", stdout);
	else if (strcmp(type->name, "uint32_t") == 0)
		fputs("%\" PRIu32 \"", stdout);
	else if (strcmp(type->name, "int64_t") == 0)
		fputs("%\" PRIi64 \"", stdout);
	else if (strcmp(type->name, "uint64_t") == 0)
		fputs("%\" PRIu64 \"", stdout);
	else if (strcmp(type->name, "intptr_t") == 0)
		fputs("%\" PRIiptr \"", stdout);
	else if (strcmp(type->name, "uintptr_t") == 0)
		fputs("%\" PRIuptr \"", stdout);
	else if (strcmp(type->name, "int_least8_t") == 0)
		fputs("%\" PRIiLEAST8 \"", stdout);
	else if (strcmp(type->name, "uint_least8_t") == 0)
		fputs("%\" PRIuLEAST8 \"", stdout);
	else if (strcmp(type->name, "int_least16_t") == 0)
		fputs("%\" PRIiLEAST16 \"", stdout);
	else if (strcmp(type->name, "uint_least16_t") == 0)
		fputs("%\" PRIuLEAST16 \"", stdout);
	else if (strcmp(type->name, "int_least32_t") == 0)
		fputs("%\" PRIiLEAST32 \"", stdout);
	else if (strcmp(type->name, "uint_least32_t") == 0)
		fputs("%\" PRIuLEAST32 \"", stdout);
	else if (strcmp(type->name, "int_least64_t") == 0)
		fputs("%\" PRIiLEAST64 \"", stdout);
	else if (strcmp(type->name, "uint_least64_t") == 0)
		fputs("%\" PRIuLEAST64 \"", stdout);
	else if (strcmp(type->name, "int_fast8_t") == 0)
		fputs("%\" PRIiFAST8 \"", stdout);
	else if (strcmp(type->name, "uint_fast8_t") == 0)
		fputs("%\" PRIuFAST8 \"", stdout);
	else if (strcmp(type->name, "int_fast16_t") == 0)
		fputs("%\" PRIiFAST16 \"", stdout);
	else if (strcmp(type->name, "uint_fast16_t") == 0)
		fputs("%\" PRIuFAST16 \"", stdout);
	else if (strcmp(type->name, "int_fast32_t") == 0)
		fputs("%\" PRIiFAST32 \"", stdout);
	else if (strcmp(type->name, "uint_fast32_t") == 0)
		fputs("%\" PRIuFAST32 \"", stdout);
	else if (strcmp(type->name, "int_fast64_t") == 0)
		fputs("%\" PRIiFAST64 \"", stdout);
	else if (strcmp(type->name, "uint_fast64_t") == 0)
		fputs("%\" PRIuFAST64 \"", stdout);
	else if (type->is_signed_integral)
		fputs("%jd", stdout);
	else /* treating it as an unsigned integral type */
		fputs("%jx", stdout);
}

static void
print_format_argument(const struct type_desc *type, const char *name)
{
	if (is_arg_printable_cstr(type->name, name) ||
		type->is_pointer ||
		(strcmp(type->name, "void *") == 0) ||
		(strcmp(type->name, "const void *") == 0) ||
		(strcmp(type->name, "size_t") == 0) ||
		(strcmp(type->name, "ptrdiff_t") == 0) ||
		(strcmp(type->name, "pmemfile_ssize_t") == 0) ||
		(strcmp(type->name, "char") == 0) ||
		(strcmp(type->name, "signed char") == 0) ||
		(strcmp(type->name, "unsigned char") == 0) ||
		(strcmp(type->name, "int") == 0) ||
		(strcmp(type->name, "unsigned") == 0) ||
		(strcmp(type->name, "short") == 0) ||
		(strcmp(type->name, "unsigned short") == 0) ||
		(strcmp(type->name, "long") == 0) ||
		(strcmp(type->name, "unsigned long") == 0) ||
		(strcmp(type->name, "long long") == 0) ||
		(strcmp(type->name, "unsigned long long") == 0) ||
		(strcmp(type->name, "int8_t") == 0) ||
		(strcmp(type->name, "uint8_t") == 0) ||
		(strcmp(type->name, "int16_t") == 0) ||
		(strcmp(type->name, "uint16_t") == 0) ||
		(strcmp(type->name, "int32_t") == 0) ||
		(strcmp(type->name, "uint32_t") == 0) ||
		(strcmp(type->name, "int64_t") == 0) ||
		(strcmp(type->name, "uint64_t") == 0) ||
		(strcmp(type->name, "int_least8_t") == 0) ||
		(strcmp(type->name, "uint_least8_t") == 0) ||
		(strcmp(type->name, "int_least16_t") == 0) ||
		(strcmp(type->name, "uint_least16_t") == 0) ||
		(strcmp(type->name, "int_least32_t") == 0) ||
		(strcmp(type->name, "uint_least32_t") == 0) ||
		(strcmp(type->name, "int_least64_t") == 0) ||
		(strcmp(type->name, "uint_least64_t") == 0) ||
		(strcmp(type->name, "int_fast8_t") == 0) ||
		(strcmp(type->name, "uint_fast8_t") == 0) ||
		(strcmp(type->name, "int_fast16_t") == 0) ||
		(strcmp(type->name, "uint_fast16_t") == 0) ||
		(strcmp(type->name, "int_fast32_t") == 0) ||
		(strcmp(type->name, "uint_fast32_t") == 0) ||
		(strcmp(type->name, "int_fast64_t") == 0) ||
		(strcmp(type->name, "uint_fast64_t") == 0) ||
		(strcmp(type->name, "intptr_t") == 0) ||
		(strcmp(type->name, "uintptr_t") == 0) ||
		(strcmp(type->name, "intmax_t") == 0) ||
		(strcmp(type->name, "uintmax_t") == 0))
		fputs(name, stdout);
	else if (type->is_signed_integral)
		printf("(intmax_t)%s", name);
	else
		printf("(uintmax_t)%s", name);
}

/*
 * print_log_write -- prints a call to log_write.
 *
 * Example output:
 * +----------------------------------------------------+
 * | log_write(                                         |
 * |        "pmemfile_link(%p, \"%s\", \"%s\") = %jd",  |
 * |            (void *)pfp,                            |
 * |            oldpath,                                |
 * |            newpath, (intmax_t)ret);                |
 * +----------------------------------------------------+
 */
static void
print_log_write(const struct func_desc *desc)
{

	printf("\tlog_write(\n\t    ");

	/* printing the format string e.g.: "%p, \"%s\", \"%s\") = %jd" */
	printf("\"%s(", desc->name);
	for (int i = 0; i < desc->arg_count; ++i) {
		if (i > 0)
			printf(", ");

		print_format(&desc->args[i].type, desc->args[i].name);
	}
	printf(")");

	if (!desc->return_type.is_void) {
		printf(" = ");
		print_format(&desc->return_type, "");
	}
	printf("\"");

	/* printing format string arguments, with appropriate casts */
	for (int i = 0; i < desc->arg_count; ++i) {
		printf(",\n\t\t");
		print_format_argument(&desc->args[i].type, desc->args[i].name);
	}

	if (!desc->return_type.is_void) {
		printf(",\n\t\t");
		print_format_argument(&desc->return_type, "");
		printf("ret");
	}

	puts(");");
}

/*
 * handle_errno
 *
 * Some functions return signed integers, where -1 means an error
 * has happened. In such cases, pmemfile-posix stores an error code
 * in libc provided errno, while a syscall would return that error
 * in the return value. This routine prints the code to perform
 * this translation when needed.
 *
 * prints (optionally):
 * +-------------------+
 * | if (ret < 0)      |
 * |     ret = -errno; |
 * +-------------------+
 */
static void
handle_errno(const struct func_desc *desc)
{
	if (strcmp(desc->return_type.name, "int") == 0 ||
	    strcmp(desc->return_type.name, "pmemfile_ssize_t") == 0) {
		puts("\tif (ret < 0)");
		puts("\t\tret = -errno;");
	}
}

/*
 * print_return_value_assignment -- assigns the return value
 * from the original function to a local variable (that is if
 * the original function returns any value).
 */
static void
print_return_value_assignment(const struct func_desc *desc)
{
	if (desc->return_type.is_void) {
		printf("\t");
	} else {
		/*
		 * prints:
		 *
		 * +---------------+
		 * | type ret;     |
		 * | ret =         |
		 * +---------------+
		 *
		 * This is expected to be followed by print_forward_call.
		 */
		putchar('\t');
		print_type_and_name(desc->return_type.name, "ret");
		printf(";\n\n\tret = ");
	}
}

static void
print_function_epilogue(const struct func_desc *desc)
{
	if (!desc->return_type.is_void) {
		puts("");
		puts("\treturn ret;");
	}
}

/*
 * print_wrapper -- prints a wrapper function.
 */
static void
print_wrapper(struct func_desc *desc)
{
	print_prototype(desc);
	puts("{");
	print_return_value_assignment(desc);
	print_forward_call(desc);
	handle_errno(desc);
	puts("");
	print_log_write(desc);
	print_function_epilogue(desc);
	puts("}");
	puts("");
}

/*
 * has_arg_name -- is the given string used as a name of any of
 * the function arguments?
 */
static bool
has_arg_name(const struct func_desc *desc, const char *name)
{
	for (int i = 0; i < desc->arg_count; ++i) {
		if (strcmp(desc->args[i].name, name) == 0)
			return true;
	}

	return false;
}

/*
 * fill_arg_name -- assign names to some function arguments, which
 * don't have a name in the orignal header file.
 *
 * E.g treat the following declaration:
 * int pmemfile_fchmod(PMEMfilepool *, PMEMfile *, pmemfile_mode_t mode);
 *
 * as:
 * int pmemfile_fchmod(PMEMfilepool *pfp, PMEMfile *file, pmemfile_mode_t mode);
 */
static int
fill_arg_name(struct func_desc *desc, struct arg_desc *arg)
{
	if (strcmp(arg->type.name, "PMEMfilepool *") == 0) {
		if (has_arg_name(desc, "pfp"))
			return -1;

		arg->name = "pfp";
		return 0;
	}

	if (strcmp(arg->type.name, "PMEMfile *") == 0) {
		if (has_arg_name(desc, "file")) {
			if (has_arg_name(desc, "file2"))
				return -1;

			arg->name = "file2";
		} else {
			arg->name = "file";
		}

		return 0;
	}

	return -1;
}

static int
fix_args(struct func_desc *desc)
{
	for (int i = 0; i < desc->arg_count; ++i) {
		struct arg_desc *arg = desc->args + i;

		if (arg->name == NULL || arg->name[0] == '\0') {
			if (fill_arg_name(desc, arg) != 0)
				return -1;
		}
	}

	return 0;
}

static int
process_function(struct func_desc *desc)
{
	static const char orig_prefix[] = "pmemfile_";

	if (desc->is_variadic)
		return 0;

	if (strncmp(desc->name, orig_prefix, strlen(orig_prefix)) != 0)
		return 0;

	if (fix_args(desc) != 0)
		return -1;

	print_wrapper(desc);

	return 0;
}

static void
write_prologue(void)
{
	puts("/* Generated source file, do not edit manually! */");
	puts("");
	puts("#ifndef LIBPMEMFILE_POSIX_WRAPPERS_H");
	puts("#define LIBPMEMFILE_POSIX_WRAPPERS_H");
	puts("");
	puts("#include \"libpmemfile-posix.h\"");
	puts("#include \"preload.h\"");
	puts("#include <inttypes.h>");
	puts("");
}

static void
write_epilogue(void)
{
	puts("");
	puts("#endif");
}

int
main(int argc, char **argv)
{
	if (argc < 3)
		return 1;

	char *input = argv[1];
	char *output = argv[2];

	if (freopen(output, "w", stdout) == NULL)
		return 1;

	write_prologue();

	argc -= 3;
	argv += 3;
	if (visit_function_decls(input, process_function, argc, argv) != 0)
		return 1;

	write_epilogue();

	return 0;
}
