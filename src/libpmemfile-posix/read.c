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
 * read.c -- pmemfile_*read* implementation
 */

#include <limits.h>

#include "callbacks.h"
#include "data.h"
#include "file.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "pool.h"
#include "utils.h"

/*
 * vinode_read -- reads file
 */
static size_t
vinode_read(PMEMfilepool *pfp, struct pmemfile_vinode *vinode, size_t offset,
		struct pmemfile_block_desc **last_block, char *buf,
		size_t count)
{
	uint64_t size = vinode->inode->size;

	/*
	 * Start reading at offset, stop reading
	 * when end of file is reached, or count bytes were read.
	 * The following two branches compute how many bytes are
	 * going to be read.
	 */
	if (offset >= size)
		return 0; /* EOF already */

	if (size - offset < count)
		count = size - offset;

	struct pmemfile_block_desc *block =
		find_closest_block_with_hint(vinode, offset, *last_block);

	block = iterate_on_file_range(pfp, vinode, block, offset,
			count, buf, read_from_blocks);

	if (block)
		*last_block = block;

	return count;
}

static int
time_cmp(const struct pmemfile_time *t1, const struct pmemfile_time *t2)
{
	if (t1->sec < t2->sec)
		return -1;
	if (t1->sec > t2->sec)
		return 1;
	if (t1->nsec < t2->nsec)
		return -1;
	if (t1->nsec > t2->nsec)
		return 1;
	return 0;
}

static pmemfile_ssize_t
pmemfile_preadv_internal(PMEMfilepool *pfp,
		struct pmemfile_vinode *vinode,
		struct pmemfile_block_desc **last_block,
		uint64_t file_flags,
		size_t offset,
		const pmemfile_iovec_t *iov,
		int iovcnt)
{
	LOG(LDBG, "vinode %p iov %p iovcnt %d", vinode, iov, iovcnt);

	if (!vinode_is_regular_file(vinode)) {
		errno = EINVAL;
		return -1;
	}

	if (!(file_flags & PFILE_READ)) {
		errno = EBADF;
		return -1;
	}

	if (iovcnt == 0)
		return 0;

	if (iov == NULL) {
		errno = EFAULT;
		return -1;
	}

	for (int i = 0; i < iovcnt; ++i) {
		if (iov[i].iov_base == NULL) {
			errno = EFAULT;
			return -1;
		}
	}

	struct pmemfile_inode *inode = vinode->inode;

	pmemfile_ssize_t ret;

	ret = vinode_rdlock_with_block_tree(pfp, vinode);
	if (ret != 0)
		return ret;

	for (int i = 0; i < iovcnt; ++i) {
		size_t len = iov[i].iov_len;
		if ((pmemfile_ssize_t)((size_t)ret + len) < 0)
			len = (size_t)(SSIZE_MAX - ret);
		ASSERT((pmemfile_ssize_t)((size_t)ret + len) >= 0);

		size_t bytes_read = vinode_read(pfp, vinode, offset, last_block,
				iov[i].iov_base, len);

		ret += (pmemfile_ssize_t)bytes_read;
		offset += bytes_read;
		if (bytes_read != len)
			break;
	}

	bool update_atime = !(file_flags & PFILE_NOATIME);
	struct pmemfile_time tm;

	if (update_atime) {
		struct pmemfile_time tm1d;
		if (get_current_time(&tm)) {
			LOG(LINF, "can not get current time");
			update_atime = false;
		} else {
			tm1d.nsec = tm.nsec;
			tm1d.sec = tm.sec - 86400;

			/* relatime */
			update_atime =	time_cmp(&inode->atime, &tm1d) < 0 ||
				time_cmp(&inode->atime, &inode->ctime) < 0 ||
				time_cmp(&inode->atime, &inode->mtime) < 0;
		}
	}

	os_rwlock_unlock(&vinode->rwlock);

	ASSERT_NOT_IN_TX();
	if (update_atime) {
		os_rwlock_wrlock(&vinode->rwlock);

		TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
			TX_SET_DIRECT(inode, atime, tm);
		} TX_ONABORT {
			LOG(LINF, "can not update inode atime");
		} TX_END

		os_rwlock_unlock(&vinode->rwlock);
	}

	return ret;
}

/*
 * pmemfile_read -- reads file
 */
pmemfile_ssize_t
pmemfile_read(PMEMfilepool *pfp, PMEMfile *file, void *buf, size_t count)
{
	pmemfile_iovec_t element = { .iov_base = buf, .iov_len = count };
	return pmemfile_readv(pfp, file, &element, 1);
}

pmemfile_ssize_t
pmemfile_readv(PMEMfilepool *pfp, PMEMfile *file, const pmemfile_iovec_t *iov,
		int iovcnt)
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

	os_mutex_lock(&file->mutex);

	struct pmemfile_block_desc *last_block = file->block_pointer_cache;

	pmemfile_ssize_t ret = pmemfile_preadv_internal(pfp, file->vinode,
			&last_block, file->flags, file->offset, iov, iovcnt);
	if (ret >= 0) {
		file->offset += (size_t)ret;
		file->block_pointer_cache = last_block;
	}

	os_mutex_unlock(&file->mutex);

	return ret;
}

pmemfile_ssize_t
pmemfile_pread(PMEMfilepool *pfp, PMEMfile *file, void *buf, size_t count,
		pmemfile_off_t offset)
{
	pmemfile_iovec_t element = { .iov_base = buf, .iov_len = count };
	return pmemfile_preadv(pfp, file, &element, 1, offset);
}


pmemfile_ssize_t
pmemfile_preadv(PMEMfilepool *pfp, PMEMfile *file, const pmemfile_iovec_t *iov,
		int iovcnt, pmemfile_off_t offset)
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

	if (offset < 0) {
		errno = EINVAL;
		return -1;
	}

	os_mutex_lock(&file->mutex);

	struct pmemfile_block_desc *last_block = file->block_pointer_cache;
	struct pmemfile_vinode *vinode = file->vinode;
	uint64_t flags = file->flags;

	os_mutex_unlock(&file->mutex);

	return pmemfile_preadv_internal(pfp, vinode, &last_block, flags,
			(size_t)offset, iov, iovcnt);
}
