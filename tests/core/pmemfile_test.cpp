/*
 * Copyright 2016-2017, Intel Corporation
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
 * pmemfile_test.cpp -- unit test utility functions for pmemfile
 */

#include <stdarg.h>
#include <errno.h>
#include <string.h>

#include "gtest/gtest.h"
#include "libpmemfile-core.h"
#include "pmemfile_test.hpp"

std::string global_path;

bool
test_pmemfile_stats_match(PMEMfilepool *pfp, unsigned inodes, unsigned dirs,
		unsigned block_arrays, unsigned inode_arrays, unsigned blocks)
{
	struct pmemfile_stats stats;
	pmemfile_stats(pfp, &stats);

	EXPECT_EQ(stats.inodes, inodes);
	EXPECT_EQ(stats.dirs, dirs);
	EXPECT_EQ(stats.block_arrays, block_arrays);
	EXPECT_EQ(stats.inode_arrays, inode_arrays);
	EXPECT_EQ(stats.blocks, blocks);

	return stats.inodes == inodes &&
			stats.dirs == dirs &&
			stats.block_arrays == block_arrays &&
			stats.inode_arrays == inode_arrays &&
			stats.blocks == blocks;
}

bool
test_pmemfile_create(PMEMfilepool *pfp, const char *path, int flags,
		mode_t mode)
{
	PMEMfile *file = pmemfile_open(pfp, path, flags | PMEMFILE_O_CREAT,
			mode);
	EXPECT_NE(file, nullptr) << strerror(errno);
	if (!file)
		return false;
	pmemfile_close(pfp, file);
	return true;
}

ssize_t
test_pmemfile_file_size(PMEMfilepool *pfp, PMEMfile *file)
{
	struct stat buf;
	int ret = pmemfile_fstat(pfp, file, &buf);
	EXPECT_EQ(ret, 0) << strerror(errno);
	if (ret != 0)
		return -1;
	return buf.st_size;
}

ssize_t
test_pmemfile_path_size(PMEMfilepool *pfp, const char *path)
{
	struct stat buf;
	int ret = pmemfile_stat(pfp, path, &buf);
	EXPECT_EQ(ret, 0) << strerror(errno);
	if (ret != 0)
		return -1;
	return buf.st_size;
}

#define VAL_EXPECT_EQ(v1, v2) do { \
	tmp = (v1) != (v2); \
	if (tmp) \
		ADD_FAILURE() << (v1) << " != " << (v2); \
	anyerr |= tmp; \
} while (0)

#define MODE_EXPECT(f, v, exp) do { \
	tmp = f(v) != (exp); \
	if (tmp) \
		ADD_FAILURE() << #f << " " << (v); \
	anyerr |= tmp; \
} while (0)

#define STR_EXPECT_EQ(v1, v2) do { \
	tmp = strcmp(v1, v2) != 0; \
	if (tmp) \
		ADD_FAILURE() << (v1) << " != " << (v2); \
	anyerr |= tmp; \
} while (0)

std::map<std::string, file_attrs>
test_list_files(PMEMfilepool *pfp, PMEMfile *dir, const char *dirp,
		unsigned length)
{
	struct stat statbuf;
	char symlinkbuf[PMEMFILE_PATH_MAX];
	std::map<std::string, file_attrs> retmap;
	bool err = false;
	bool tmp;

	for (unsigned i = 0; i < length; ) {
		i += 8;
		i += 8;

		unsigned short int reclen = *(unsigned short *)&dirp[i];
		i += 2;

		char type = *(char *)&dirp[i];
		i += 1;

		int ret = pmemfile_fstatat(pfp, dir, dirp + i, &statbuf,
				PMEMFILE_AT_SYMLINK_NOFOLLOW);
		if (ret) {
			err = true;
			break;
		}

		bool anyerr = false;
		symlinkbuf[0] = 0;
		if (type == PMEMFILE_DT_REG) {
			MODE_EXPECT(PMEMFILE_S_ISREG, statbuf.st_mode, 1);
		} else if (type == PMEMFILE_DT_DIR) {
			MODE_EXPECT(PMEMFILE_S_ISDIR, statbuf.st_mode, 1);
		} else if (type == PMEMFILE_DT_LNK) {
			MODE_EXPECT(PMEMFILE_S_ISLNK, statbuf.st_mode, 1);

			ssize_t ret = pmemfile_readlinkat(pfp, dir, dirp + i,
					symlinkbuf, PMEMFILE_PATH_MAX);
			tmp = ret <= 0 || ret >= PMEMFILE_PATH_MAX;
			if (tmp)
				ADD_FAILURE() << ret;
			anyerr |= tmp;
			if (anyerr)
				break;
			symlinkbuf[ret] = 0;
		} else {
			ADD_FAILURE() << "unknown type " << type;
			anyerr = true;
		}

		if (!anyerr) {
			file_attrs attr(statbuf, symlinkbuf);
			retmap.insert(std::pair<std::string, file_attrs>(
					std::string(dirp + i), attr));
		}

		err |= anyerr;

		i += reclen;
		i -= 8 + 8 + 2 + 1;
	}

	if (err)
		return std::map<std::string, file_attrs>();

	return retmap;
}

std::map<std::string, file_attrs>
test_list_files(PMEMfilepool *pfp, const char *path)
{
	PMEMfile *f = pmemfile_open(pfp, path, PMEMFILE_O_DIRECTORY |
			PMEMFILE_O_RDONLY);
	if (!f) {
		ADD_FAILURE() << "open " << path;
		return std::map<std::string, file_attrs>();
	}

	std::map<std::string, file_attrs> ret;

	char dir_buf[32758];
	while (1) {
		int r = pmemfile_getdents64(pfp, f,
		    (struct linux_dirent64 *)dir_buf, sizeof(dir_buf));
		if (r < 0) {
			ADD_FAILURE() << "getdents " << path << " " <<
					strerror(errno);
			return std::map<std::string, file_attrs>();
		}
		if (r == 0)
			break;

		std::map<std::string, file_attrs> tmp =
				test_list_files(pfp, f, dir_buf, (unsigned)r);
		if (tmp.empty()) {
			ADD_FAILURE() << "test_list_files " << path << " " <<
					strerror(errno);
			return tmp;
		}

		ret.insert(tmp.begin(), tmp.end());
	}

	pmemfile_close(pfp, f);

	return ret;
}

bool
test_compare_dirs(const std::map<std::string, file_attrs> &files,
		const struct pmemfile_ls expected[], bool check_attrs)
{
	bool anyerr = false;
	bool tmp;

	while (expected[0].name) {
		auto attrs_iter = files.find(expected[0].name);
		if (attrs_iter == files.end()) {
			ADD_FAILURE() << expected[0].name << " not found";
			return false;
		}
		const file_attrs &attrs = (*attrs_iter).second;

		VAL_EXPECT_EQ(expected->mode, attrs.stat.st_mode);
		VAL_EXPECT_EQ(expected->nlink, attrs.stat.st_nlink);
		VAL_EXPECT_EQ(expected->size, attrs.stat.st_size);

		if (expected->link == NULL) {
			MODE_EXPECT(PMEMFILE_S_ISLNK, attrs.stat.st_mode, 0);
		} else {
			MODE_EXPECT(PMEMFILE_S_ISLNK, attrs.stat.st_mode, 1);

			STR_EXPECT_EQ(expected->link, attrs.link.c_str());
		}

		if (check_attrs) {
			VAL_EXPECT_EQ(expected->uid, attrs.stat.st_uid);
			VAL_EXPECT_EQ(expected->gid, attrs.stat.st_gid);
		}

		expected++;
	}

	if (anyerr)
		ADD_FAILURE() << files;

	return !anyerr;
}

bool
test_compare_dirs(PMEMfilepool *pfp, const char *path,
		const struct pmemfile_ls expected[], bool check_attrs)
{
	std::map<std::string, file_attrs> files = test_list_files(pfp, path);
	if (files.empty())
		return false;
	return test_compare_dirs(files, expected, check_attrs);
}

bool
test_empty_dir(PMEMfilepool *pfp, const char *path)
{
	std::map<std::string, file_attrs> files = test_list_files(pfp, path);

	return test_compare_dirs(files, (const struct pmemfile_ls[]) {
	    {040777, 2, 4008, "."},
	    {040777, 2, 4008, ".."},
	    {}});
}
