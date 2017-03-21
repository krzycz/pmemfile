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
#ifndef PMEMFILE_POOL_H
#define PMEMFILE_POOL_H

/*
 * Runtime pool state.
 */

#include "inode.h"
#include "layout.h"
#include "os_thread.h"

struct pmemfile_inode_map;

struct pmemfile_cred {
	uid_t fsuid;
	gid_t fsgid;
	gid_t *groups;
	size_t groupsnum;
	int caps;
};

/* Pool */
struct pmemfilepool {
	PMEMobjpool *pop;
	struct pmemfile_vinode *root;

	struct pmemfile_vinode *cwd;
	os_rwlock_t cwd_rwlock;

	TOID(struct pmemfile_super) super;
	os_rwlock_t rwlock;

	struct pmemfile_inode_map *inode_map;

	struct pmemfile_cred cred;
	os_rwlock_t cred_rwlock;
};

#define PFILE_WANT_READ (1<<0)
#define PFILE_WANT_WRITE (1<<1)
#define PFILE_WANT_EXECUTE (1<<2)
bool can_access(const struct pmemfile_cred *cred,
		struct inode_perms perms,
		int acc);
int get_cred(PMEMfilepool *pfp, struct pmemfile_cred *cred);
void put_cred(struct pmemfile_cred *cred);

bool vinode_can_access(const struct pmemfile_cred *cred,
		struct pmemfile_vinode *vinode, int acc);

bool _vinode_can_access(const struct pmemfile_cred *cred,
		struct pmemfile_vinode *vinode, int acc);

bool gid_in_list(const struct pmemfile_cred *cred, gid_t gid);

#endif
