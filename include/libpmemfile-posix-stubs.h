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
 * libpmemfile-posix-stubs.h -- definitions of not yet implemented
 * libpmemfile-posix entry points. Do not use these. All the routines
 * just set errno to ENOTSUP.
 * This is header file, and the symbols exported are used while designing
 * the interface of the library.
 * Everything here is subject to change at any time.
 *
 * If/when some pmemfile functionality is implemented, the corresponding
 * header declarations should be moved to the libpmemfile-posix.h header file.
 *
 * This file is expected to be removed eventually.
 */
#ifndef LIBPMEMFILE_POSIX_H
#error Never include this header file directly
#endif

#ifndef LIBPMEMFILE_POSIX_STUBS_H
#define LIBPMEMFILE_POSIX_STUBS_H

int pmemfile_access(PMEMfilepool *, const char *path, mode_t mode);
int pmemfile_euidaccess(PMEMfilepool *, const char *pathname, int mode);
int pmemfile_faccessat(PMEMfilepool *, PMEMfile *dir, const char *pathname,
		int mode, int flags);

int pmemfile_flock(PMEMfilepool *, PMEMfile *file, int operation);

int pmemfile_chown(PMEMfilepool *, const char *pathname, uid_t owner,
		gid_t group);
int pmemfile_fchown(PMEMfilepool *, PMEMfile *file, uid_t owner, gid_t group);
int pmemfile_lchown(PMEMfilepool *, const char *pathname, uid_t owner,
		gid_t group);
int pmemfile_fchownat(PMEMfilepool *, PMEMfile *dir, const char *pathname,
		uid_t owner, gid_t group, int flags);

// De we need dup, dup2 in libpmemfile-posix? Maybe, dunno...
PMEMfile *pmemfile_dup(PMEMfilepool *, PMEMfile *);
PMEMfile *pmemfile_dup2(PMEMfilepool *, PMEMfile *file, PMEMfile *file2);

// Memory mapping pmemfiles, these need extra suppport in the preloadable lib
void *pmemfile_mmap(PMEMfilepool *, void *addr, size_t len,
		int prot, int flags, PMEMfile *file, off_t off);
int pmemfile_munmap(PMEMfilepool *, void *addr, size_t len);
void *pmemfile_mremap(PMEMfilepool *, void *old_addr, size_t old_size,
			size_t new_size, int flags, void *new_addr);
int pmemfile_msync(PMEMfilepool *, void *addr, size_t len, int flags);
int pmemfile_mprotect(PMEMfilepool *, void *addr, size_t len, int prot);

struct iovec;
ssize_t pmemfile_readv(PMEMfilepool *, PMEMfile *file,
	const struct iovec *iov, int iovcnt);
ssize_t pmemfile_writev(PMEMfilepool *, PMEMfile *file,
	const struct iovec *iov, int iovcnt);
ssize_t pmemfile_preadv(PMEMfilepool *, PMEMfile *file,
	const struct iovec *iov, int iovcnt, off_t offset);
ssize_t pmemfile_pwritev(PMEMfilepool *, PMEMfile *file,
	const struct iovec *iov, int iovcnt, off_t offset);

struct utimbuf;
int pmemfile_utime(PMEMfilepool *, const char *filename,
		const struct utimbuf *times);
int pmemfile_utimes(PMEMfilepool *, const char *filename,
		const struct timeval times[2]);
int pmemfile_futimes(PMEMfilepool *, PMEMfile *file,
		const struct timeval tv[2]);
int pmemfile_lutimes(PMEMfilepool *, const char *filename,
		const struct timeval tv[2]);
int pmemfile_utimensat(PMEMfilepool *, PMEMfile *dir, const char *pathname,
		const struct timespec times[2], int flags);
int pmemfile_futimens(PMEMfilepool *, PMEMfile *file,
		const struct timespec times[2]);

mode_t pmemfile_umask(PMEMfilepool *, mode_t mask);

ssize_t pmemfile_copy_file_range(PMEMfilepool *,
		PMEMfile *file_in, loff_t *off_in,
		PMEMfile *file_out, loff_t *off_out,
		size_t len, unsigned flags);

/*
 * Other:
 * 	fallocate
 * 	futimesat
 *	sendfile
 *	tee
 *	splice
 *	vmsplice
 *	statfs
 *	fstatfs
 *	statvfs
 *	fstatvfs
 *	pathconf
 *	fpathconf
 *	name_to_handle_at
 *	open_by_handle_at
 *	ioctl
 *
 * AIO:
 *	aio_read
 *	aio_write
 *	aio_fsync
 *	aio_error
 *	aio_return
 *	aio_suspend
 *	aio_cancel
 *	lio_listio
 *	io_submit
 *	io_cancel
 *	io_destroy
 *	io_getevents
 *	io_setup
 */
#endif
