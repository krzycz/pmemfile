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
 * symlink.c -- pmemfile_symlink* implementation
 */

#include "blocks.h"
#include "callbacks.h"
#include "dir.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "pool.h"
#include "utils.h"

const char *
get_symlink(PMEMfilepool *pfp, struct pmemfile_vinode *vinode)
{
	const char *symlink_target;
	struct pmemfile_inode *inode = vinode->inode;

	if (inode_is_longsymlink(inode))
		symlink_target = PF_RO(pfp, inode->file_data.long_symlink);
	else
		symlink_target = inode->file_data.short_symlink;

	return symlink_target;
}

static int
_pmemfile_symlinkat(PMEMfilepool *pfp, const char *target,
		struct pmemfile_vinode *dir, const char *linkpath)
{
	LOG(LDBG, "target %s linkpath %s", target, linkpath);

	struct pmemfile_cred cred;
	if (cred_acquire(pfp, &cred))
		return -1;

	int error = 0;

	struct pmemfile_path_info info;
	resolve_pathat(pfp, &cred, dir, linkpath, &info, 0);
	struct pmemfile_vinode *vinode = NULL;

	struct pmemfile_vinode *vparent = info.parent;

	if (info.error) {
		error = info.error;
		goto end;
	}

	size_t namelen = component_length(info.remaining);

	vinode = vinode_lookup_dirent(pfp, info.parent, info.remaining,
			namelen, 0);
	if (vinode) {
		error = EEXIST;
		goto end;
	}

	size_t len = strlen(target);

	const struct pmem_block_info *block_info =
			data_block_info(len + 1, MAX_BLOCK_SIZE);

	if (len + 1 > block_info->size) {
		error = ENAMETOOLONG;
		goto end;
	}

	os_rwlock_wrlock(&vparent->rwlock);

	ASSERT_NOT_IN_TX();

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		if (!_vinode_can_access(&cred, vparent, PFILE_WANT_WRITE))
			pmemfile_tx_abort(EACCES);

		TOID(struct pmemfile_inode) tinode = inode_alloc(pfp, &cred,
				PMEMFILE_S_IFLNK | PMEMFILE_ACCESSPERMS);
		struct pmemfile_inode *inode = PF_RW(pfp, tinode);
		char *buf;

		if (len + 1 <= sizeof(inode->file_data.short_symlink)) {
			buf = inode->file_data.short_symlink;
		} else {
			inode->file_data.long_symlink =
				TX_XALLOC(char, block_info->size,
					POBJ_XALLOC_NO_FLUSH |
					block_info->class_id);

			inode->flags |= PMEMFILE_S_LONGSYMLINK;

			buf = PF_RW(pfp, inode->file_data.long_symlink);
		}

		pmemobj_memcpy_persist(pfp->pop, buf, target, len + 1);

		*inode_get_size_ptr(inode) = len;

		inode_add_dirent(pfp, vparent->tinode, info.remaining, namelen,
				tinode, inode_get_ctime(inode));
	} TX_ONABORT {
		if (errno == ENOMEM)
			errno = ENOSPC;
		error = errno;
	} TX_END

	os_rwlock_unlock(&vparent->rwlock);

end:
	path_info_cleanup(pfp, &info);
	cred_release(&cred);

	ASSERT_NOT_IN_TX();
	if (vinode)
		vinode_unref(pfp, vinode);

	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

int
pmemfile_symlinkat(PMEMfilepool *pfp, const char *target, PMEMfile *newdir,
		const char *linkpath)
{
	struct pmemfile_vinode *at;
	bool at_unref;

	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!target || !linkpath) {
		errno = EFAULT;
		return -1;
	}

	if (linkpath[0] != '/' && !newdir) {
		LOG(LUSR, "NULL dir");
		errno = EFAULT;
		return -1;
	}

	at = pool_get_dir_for_path(pfp, newdir, linkpath, &at_unref);

	int ret = _pmemfile_symlinkat(pfp, target, at, linkpath);

	if (at_unref)
		vinode_cleanup(pfp, at, ret != 0);

	return ret;
}

int
pmemfile_symlink(PMEMfilepool *pfp, const char *target, const char *linkpath)
{
	return pmemfile_symlinkat(pfp, target, PMEMFILE_AT_CWD, linkpath);
}
