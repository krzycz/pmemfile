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
#ifndef PMEMFILE_LAYOUT_H
#define PMEMFILE_LAYOUT_H

/*
 * On-media structures.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "compiler_utils.h"
#include "libpmemobj.h"

#ifdef __cplusplus
extern "C" {
#endif

POBJ_LAYOUT_BEGIN(pmemfile);
POBJ_LAYOUT_ROOT(pmemfile, struct pmemfile_super);
POBJ_LAYOUT_TOID(pmemfile, struct pmemfile_inode);
POBJ_LAYOUT_TOID(pmemfile, struct pmemfile_dir);
POBJ_LAYOUT_TOID(pmemfile, struct pmemfile_block_array);
POBJ_LAYOUT_TOID(pmemfile, struct pmemfile_block_desc);
POBJ_LAYOUT_TOID(pmemfile, struct pmemfile_inode_array);
POBJ_LAYOUT_TOID(pmemfile, char);
POBJ_LAYOUT_END(pmemfile);

#define METADATA_BLOCK_SIZE 4096

struct pmemfile_block_desc {
	/* block data pointer */
	TOID(char) data;

	/* usable size of the block */
	uint32_t size;

	/* additional information about block */
	uint32_t flags;

	/* offset in file */
	uint64_t offset;

	/* next block, with offset bigger than offset+size */
	TOID(struct pmemfile_block_desc) next;

	/* previous block, with smaller offset */
	TOID(struct pmemfile_block_desc) prev;
};

#define BLOCK_INITIALIZED 1

#define PMEMFILE_BLOCK_ARRAY_VERSION(a) ((uint32_t)0x00414C42 | \
		((uint32_t)(a + '0') << 24))

/* single block array */
struct pmemfile_block_array {
	/* layout version */
	uint32_t version;

	/* padding / unused */
	uint32_t padding1;

	/* next block array */
	TOID(struct pmemfile_block_array) next;

	/* number of entries in "blocks" */
	uint32_t length;

	/* padding / unused */
	uint32_t padding2;

	/* blocks */
	struct pmemfile_block_desc blocks[];
};

#define PMEMFILE_MAX_FILE_NAME 255
/* directory entry */
struct pmemfile_dirent {
	/* inode */
	TOID(struct pmemfile_inode) inode;

	/* name */
	char name[PMEMFILE_MAX_FILE_NAME + 1];
};

#define PMEMFILE_DIR_VERSION(a) ((uint32_t)0x00524944 | \
		((uint32_t)(a + '0') << 24))

/* Directory */
struct pmemfile_dir {
	/* layout version */
	uint32_t version;

	/* number of entries in "dirents" */
	uint32_t num_elements;

	/* next batch of entries */
	TOID(struct pmemfile_dir) next;

	/* directory entries */
	struct pmemfile_dirent dirents[];
};

struct pmemfile_time {
	/* seconds */
	int64_t sec;

	/* nanoseconds */
	int64_t nsec;
};

#define PMEMFILE_INODE_VERSION(a) ((uint32_t)0x00444E49 | \
		((uint32_t)(a + '0') << 24))

#define PMEMFILE_INODE_SIZE METADATA_BLOCK_SIZE
#define PMEMFILE_IN_INODE_STORAGE \
	(sizeof(struct pmemfile_dir) + 2 * sizeof(struct pmemfile_dirent) + 8)

/* Inode */
struct pmemfile_inode {
	/* layout version */
	uint32_t version;

	/* owner */
	uint32_t uid;

	/* group */
	uint32_t gid;

	/*
	 * Number of references from processes that called
	 * pmemfile_pool_suspend.
	 */
	uint32_t suspended_references;

	uint8_t padding1[48];

	/* ---- cacheline boundary ----- */

	/* file flags */
	uint64_t flags[2];

	/* allocated space in file (for regular files) */
	uint64_t allocated_space[2];

	/* size of file */
	uint64_t size[2];

	/* hard link counter */
	uint64_t nlink[2];

	/* ---- cacheline boundary ----- */

	/* time of last access */
	struct pmemfile_time atime[2];

	/* time of last status change */
	struct pmemfile_time ctime[2];

	/* ---- cacheline boundary ----- */

	/* time of last modification */
	struct pmemfile_time mtime[2];

	uint8_t padding2[32];

	/* ---- cacheline boundary ----- */

	union pmemfile_inode_slots {
		struct {
			unsigned atime : 1;
			unsigned ctime : 1;
			unsigned mtime : 1;
			unsigned nlink : 1;
			unsigned size : 1;
			unsigned allocated_space : 1;
			unsigned flags : 1;
			unsigned bit_padding : 25;
			char byte_padding[4];
		} bits;
		uint64_t value;
	} slots;

	char byte_padding[56];

	/* ---- cacheline boundary ---- */

	uint8_t padding3[3200];

	/* ---- cacheline boundary ---- */

	/* data! */
	union {
		/* file specific data */
		struct pmemfile_block_array blocks;

		/* directory specific data */
		struct pmemfile_dir dir;

		TOID(char) long_symlink;

		char short_symlink[PMEMFILE_IN_INODE_STORAGE];
	} file_data;
};

/*
 * Sizeof(slots) must be equal to the size for which architecture guarantees
 * store atomicity.
 */
COMPILE_ERROR_ON(sizeof(union pmemfile_inode_slots) != 8);

COMPILE_ERROR_ON(sizeof(struct pmemfile_inode) != PMEMFILE_INODE_SIZE);

#define PMEMFILE_INODE_ARRAY_VERSION(a) ((uint32_t)0x00414E49 | \
		((uint32_t)(a + '0') << 24))
#define PMEMFILE_INODE_ARRAY_SIZE METADATA_BLOCK_SIZE
/* number of inodes for pmemfile_inode_array to fit in 4kB */
#define NUMINODES_PER_ENTRY 249

COMPILE_ERROR_ON(4 /* version */
		+ 4  /* used */ \
		+ 8 /* padding */\
		+ 16 /* prev */ \
		+ 16 /* next */ \
		+ sizeof(PMEMmutex) \
		+ NUMINODES_PER_ENTRY * sizeof(TOID(struct pmemfile_inode)) \
		!= PMEMFILE_INODE_ARRAY_SIZE);

struct pmemfile_inode_array {
	/* layout version */
	uint32_t version;

	/* number of used entries, <0, NUMINODES_PER_ENTRY> */
	uint32_t used;

	/* padding / unused */
	uint64_t padding;

	TOID(struct pmemfile_inode_array) prev;
	TOID(struct pmemfile_inode_array) next;
	PMEMmutex mtx;

	TOID(struct pmemfile_inode) inodes[NUMINODES_PER_ENTRY];
};

COMPILE_ERROR_ON(sizeof(struct pmemfile_inode_array) !=
		PMEMFILE_INODE_ARRAY_SIZE);

#define PMEMFILE_SUPER_VERSION(a, b) ((uint64_t)0x000056454C494650 | \
		((uint64_t)(a + '0') << 48) | ((uint64_t)(b + '0') << 56))
#define PMEMFILE_SUPER_SIZE METADATA_BLOCK_SIZE

/*
 * Number of distinct directory trees. At the moment, a static compile time
 * constant. But the client is required to get this by calling
 * pmemfile_root_count(), therefore it can be a dynamic value in future
 * implementations.
 */
#define PMEMFILE_ROOT_COUNT 4

/* superblock */
struct pmemfile_super {
	/* superblock version */
	uint64_t version;

	/* list of arrays of inodes that were deleted, but are still opened */
	TOID(struct pmemfile_inode_array) orphaned_inodes;

	/* list of arrays of inodes that are suspended */
	TOID(struct pmemfile_inode_array) suspended_inodes;

	/*
	 * The array of root directories. Each one of them is a root of a
	 * separate directory tree. The path "/" resolves to root #0, all other
	 * roots are only accessible via special values passed to pmemfile_at*
	 * funcions.
	 */
	TOID(struct pmemfile_inode) root_inode[PMEMFILE_ROOT_COUNT];

	char padding[PMEMFILE_SUPER_SIZE
			- 8  /* version */
			- 16 * (PMEMFILE_ROOT_COUNT) /* toid */
			- 16 /* toid */
			- 16 /* toid */];
};

COMPILE_ERROR_ON(sizeof(struct pmemfile_super) != PMEMFILE_SUPER_SIZE);

#ifdef __cplusplus
}
#endif
#endif
