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
#ifndef PMEMFILE_OFSSET_MAPPING_H
#define PMEMFILE_OFSSET_MAPPING_H

#include <stdbool.h>
#include <stdint.h>

#include "libpmemfile-posix.h"

#ifdef __cplusplus
extern "C" {
#endif

/* branching factor is 2^N_CHILDREN_POW */
#define N_CHILDREN_POW 4

#define N_CHILDREN (1 << N_CHILDREN_POW)

struct pmemfile_block_desc;

struct offset_map_entry {

	/*
	 * data holds pointer to pmemfile_block_desc when internal == false
	 * or to offset_map_entry array otherwise
	 */

	union {
		struct pmemfile_block_desc *block;

		struct offset_map_entry *children;
	} data;

	bool internal;
};

struct offset_map {

	struct offset_map_entry entry;

	PMEMfilepool *pfp;

	/*
	 * specifies range covered by offset_map:
	 * range starts at 0 and has length of range_length
	 */
	uint64_t range_length;
};

struct offset_map *offset_map_new(PMEMfilepool *pfp);

void offset_map_delete(struct offset_map *m);

struct pmemfile_block_desc *block_find_closest(struct offset_map *map,
						uint64_t offset);

int insert_block(struct offset_map *map, struct pmemfile_block_desc *block);

int remove_block(struct offset_map *map, struct pmemfile_block_desc *block);

#ifdef __cplusplus
}
#endif
#endif
