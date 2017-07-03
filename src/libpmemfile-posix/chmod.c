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
 * chmod.c -- pmemfile_*chmod* implementation
 */

#include "callbacks.h"
#include "creds.h"
#include "dir.h"
#include "file.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "pool.h"
#include "utils.h"

/*
 * vinode_chmod
 *
 * Can't be called in a transaction.
 */
static int
vinode_chmod(PMEMfilepool *pfp, struct pmemfile_vinode *vinode,
		pmemfile_mode_t mode)
{
	struct pmemfile_inode *inode = vinode->inode;
	int error = 0;

	ASSERT_NOT_IN_TX();

	struct pmemfile_cred cred;
	if (cred_acquire(pfp, &cred))
		return errno;

	os_rwlock_wrlock(&vinode->rwlock);

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		if (vinode->inode->uid != cred.fsuid &&
				!(cred.caps & (1 << PMEMFILE_CAP_FOWNER)))
			pmemfile_tx_abort(EPERM);

		struct pmemfile_time tm;
		tx_get_current_time(&tm);

		TX_ADD_DIRECT(&inode->ctime);
		inode->ctime = tm;

		TX_ADD_DIRECT(&inode->flags);
		inode->flags = (inode->flags & ~(uint64_t)PMEMFILE_ALLPERMS)
				| mode;

		if (vinode->inode->gid != cred.fsgid &&
				!gid_in_list(&cred, vinode->inode->gid) &&
				!(cred.caps & (1 << PMEMFILE_CAP_FSETID)))
			inode->flags &= ~(uint64_t)PMEMFILE_S_ISGID;

	} TX_ONABORT {
		error = errno;
	} TX_END

	os_rwlock_unlock(&vinode->rwlock);

	cred_release(&cred);

	return error;
}

static int
_pmemfile_fchmodat(PMEMfilepool *pfp, struct pmemfile_vinode *dir,
		const char *path, pmemfile_mode_t mode, int flags)
{
	mode &= PMEMFILE_ALLPERMS;

	if (flags & PMEMFILE_AT_SYMLINK_NOFOLLOW) {
		errno = ENOTSUP;
		return -1;
	}

	if (flags & ~(PMEMFILE_AT_SYMLINK_NOFOLLOW)) {
		errno = EINVAL;
		return -1;
	}

	LOG(LDBG, "path %s", path);

	struct pmemfile_cred cred;
	if (cred_acquire(pfp, &cred))
		return -1;

	int error = 0;
	struct pmemfile_path_info info;
	struct pmemfile_vinode *vinode = resolve_pathat_full(pfp, &cred, dir,
			path, &info, 0, RESOLVE_LAST_SYMLINK);

	if (info.error) {
		error = info.error;
		goto end;
	}

	error = vinode_chmod(pfp, vinode, mode);

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
pmemfile_fchmodat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		pmemfile_mode_t mode, int flags)
{
	struct pmemfile_vinode *at;
	bool at_unref;

	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!pathname) {
		errno = ENOENT;
		return -1;
	}

	if (pathname[0] != '/' && !dir) {
		LOG(LUSR, "NULL dir");
		errno = EFAULT;
		return -1;
	}

	at = pool_get_dir_for_path(pfp, dir, pathname, &at_unref);

	int ret = _pmemfile_fchmodat(pfp, at, pathname, mode, flags);

	if (at_unref)
		vinode_cleanup(pfp, at, ret != 0);

	return ret;
}

int
pmemfile_chmod(PMEMfilepool *pfp, const char *path, pmemfile_mode_t mode)
{
	return pmemfile_fchmodat(pfp, PMEMFILE_AT_CWD, path, mode, 0);
}

int
pmemfile_fchmod(PMEMfilepool *pfp, PMEMfile *file, pmemfile_mode_t mode)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (!file) {
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
	}

	if (file->flags & PFILE_PATH) {
		errno = EBADF;
		return -1;
	}

	int ret = vinode_chmod(pfp, file->vinode, mode);

	if (ret) {
		errno = ret;
		return -1;
	}

	return 0;
}
