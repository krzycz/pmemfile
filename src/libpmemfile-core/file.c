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
 * file.c -- basic file operations
 */

#include <errno.h>

#include "callbacks.h"
#include "data.h"
#include "dir.h"
#include "file.h"

#include "inode.h"
#include "inode_array.h"
#include "internal.h"
#include "locks.h"
#include "os_thread.h"
#include "out.h"
#include "pool.h"
#include "util.h"

static bool
is_tmpfile(int flags)
{
	return (flags & PMEMFILE_O_TMPFILE) == PMEMFILE_O_TMPFILE;
}

/*
 * check_flags -- (internal) open(2) flags tester
 */
static int
check_flags(int flags)
{
	if (flags & PMEMFILE_O_APPEND) {
		LOG(LSUP, "O_APPEND");
		flags &= ~PMEMFILE_O_APPEND;
	}

	if (flags & PMEMFILE_O_ASYNC) {
		LOG(LSUP, "O_ASYNC is not supported");
		errno = EINVAL;
		return -1;
	}

	if (flags & PMEMFILE_O_CREAT) {
		LOG(LTRC, "O_CREAT");
		flags &= ~PMEMFILE_O_CREAT;
	}

	// XXX: move to interposing layer
	if (flags & PMEMFILE_O_CLOEXEC) {
		LOG(LINF, "O_CLOEXEC is always enabled");
		flags &= ~PMEMFILE_O_CLOEXEC;
	}

	if (flags & PMEMFILE_O_DIRECT) {
		LOG(LINF, "O_DIRECT is always enabled");
		flags &= ~PMEMFILE_O_DIRECT;
	}

	/* O_TMPFILE contains O_DIRECTORY */
	if ((flags & PMEMFILE_O_TMPFILE) == PMEMFILE_O_TMPFILE) {
		LOG(LTRC, "O_TMPFILE");
		flags &= ~PMEMFILE_O_TMPFILE;
	}

	if (flags & PMEMFILE_O_DIRECTORY) {
		LOG(LSUP, "O_DIRECTORY");
		flags &= ~PMEMFILE_O_DIRECTORY;
	}

	if (flags & PMEMFILE_O_DSYNC) {
		LOG(LINF, "O_DSYNC is always enabled");
		flags &= ~PMEMFILE_O_DSYNC;
	}

	if (flags & PMEMFILE_O_EXCL) {
		LOG(LTRC, "O_EXCL");
		flags &= ~PMEMFILE_O_EXCL;
	}

	if (flags & PMEMFILE_O_NOCTTY) {
		LOG(LINF, "O_NOCTTY is always enabled");
		flags &= ~PMEMFILE_O_NOCTTY;
	}

	if (flags & PMEMFILE_O_NOATIME) {
		LOG(LTRC, "O_NOATIME");
		flags &= ~PMEMFILE_O_NOATIME;
	}

	if (flags & PMEMFILE_O_NOFOLLOW) {
		LOG(LTRC, "O_NOFOLLOW");
		flags &= ~PMEMFILE_O_NOFOLLOW;
	}

	if (flags & PMEMFILE_O_NONBLOCK) {
		LOG(LINF, "O_NONBLOCK is ignored");
		flags &= ~PMEMFILE_O_NONBLOCK;
	}

	if (flags & PMEMFILE_O_PATH) {
		LOG(LSUP, "O_PATH is not supported (yet)");
		errno = EINVAL;
		return -1;
	}

	if (flags & PMEMFILE_O_SYNC) {
		LOG(LINF, "O_SYNC is always enabled");
		flags &= ~PMEMFILE_O_SYNC;
	}

	if (flags & PMEMFILE_O_TRUNC) {
		LOG(LTRC, "O_TRUNC");
		flags &= ~PMEMFILE_O_TRUNC;
	}

	if ((flags & PMEMFILE_O_ACCMODE) == PMEMFILE_O_RDONLY) {
		LOG(LTRC, "O_RDONLY");
		flags -= PMEMFILE_O_RDONLY;
	}

	if ((flags & PMEMFILE_O_ACCMODE) == PMEMFILE_O_WRONLY) {
		LOG(LTRC, "O_WRONLY");
		flags -= PMEMFILE_O_WRONLY;
	}

	if ((flags & PMEMFILE_O_ACCMODE) == PMEMFILE_O_RDWR) {
		LOG(LTRC, "O_RDWR");
		flags -= PMEMFILE_O_RDWR;
	}

	if (flags) {
		ERR("unknown flag 0x%x\n", flags);
		errno = EINVAL;
		return -1;
	}

	return 0;
}

static struct pmemfile_vinode *
create_file(PMEMfilepool *pfp, const char *filename, size_t namelen,
		struct pmemfile_vinode *parent_vinode,
		int flags, mode_t mode)
{
	struct pmemfile_time t;

	rwlock_tx_wlock(&parent_vinode->rwlock);

	struct pmemfile_vinode *vinode = inode_alloc(pfp,
			PMEMFILE_S_IFREG | mode, &t,
			parent_vinode, NULL, filename, namelen);

	if (is_tmpfile(flags))
		vinode_orphan(pfp, vinode);
	else
		vinode_add_dirent(pfp, parent_vinode, filename,
				namelen, vinode, &t);

	rwlock_tx_unlock_on_commit(&parent_vinode->rwlock);

	return vinode;
}

static void
open_file(struct pmemfile_vinode *vinode, int flags)
{
	if ((flags & PMEMFILE_O_DIRECTORY) && !vinode_is_dir(vinode))
		pmemfile_tx_abort(ENOTDIR);

	if (flags & PMEMFILE_O_TRUNC) {
		if (!vinode_is_regular_file(vinode)) {
			LOG(LUSR, "truncating non regular file");
			pmemfile_tx_abort(EINVAL);
		}

		if ((flags & PMEMFILE_O_ACCMODE) == PMEMFILE_O_RDONLY) {
			LOG(LUSR, "O_TRUNC without write permissions");
			pmemfile_tx_abort(EACCES);
		}

		rwlock_tx_wlock(&vinode->rwlock);

		vinode_truncate(vinode);

		rwlock_tx_unlock_on_commit(&vinode->rwlock);
	}
}

/*
 * _pmemfile_openat -- open file
 */
static PMEMfile *
_pmemfile_openat(PMEMfilepool *pfp, struct pmemfile_vinode *dir,
		const char *pathname, int flags, ...)
{
	LOG(LDBG, "pathname %s flags 0x%x", pathname, flags);

	const char *orig_pathname = pathname;

	if (check_flags(flags))
		return NULL;

	va_list ap;
	va_start(ap, flags);
	mode_t mode = 0;

	/* NOTE: O_TMPFILE contains O_DIRECTORY */
	if ((flags & PMEMFILE_O_CREAT) || is_tmpfile(flags)) {
		mode = va_arg(ap, mode_t);
		LOG(LDBG, "mode %o", mode);
		mode &= PMEMFILE_ALLPERMS;
	}
	va_end(ap);

	int error = 0;
	PMEMfile *file = NULL;

	struct pmemfile_path_info info;
	struct pmemfile_vinode *volatile vinode;
	struct pmemfile_vinode *vparent;
	bool path_info_changed;
	size_t namelen;

	resolve_pathat(pfp, dir, pathname, &info, 0);

	do {
		path_info_changed = false;
		vparent = info.vinode;
		vinode = NULL;
		if (vparent == NULL) {
			error = ELOOP;
			goto end;
		}

		if (!vinode_is_dir(vparent)) {
			error = ENOTDIR;
			goto end;
		}

		if (more_than_1_component(info.remaining)) {
			error = ENOENT;
			goto end;
		}

		namelen = component_length(info.remaining);

		if (namelen == 0) {
			ASSERT(vparent == pfp->root);
			vinode = vinode_ref(pfp, vparent);
		} else {
			vinode = vinode_lookup_dirent(pfp, info.vinode,
					info.remaining, namelen, 0);
		}

		if (vinode && vinode_is_symlink(vinode)) {
			if (flags & PMEMFILE_O_NOFOLLOW) {
				error = ELOOP;
				goto end;
			}

			/*
			 * From open manpage: "When these two flags (O_CREAT &
			 * O_EXCL) are specified, symbolic links are not
			 * followed: if pathname is a symbolic link, then open()
			 * fails regardless of where the symbolic link points
			 * to."
			 *
			 * When only O_CREAT is specified, symlinks *are*
			 * followed.
			 */
			if ((flags & (PMEMFILE_O_CREAT|PMEMFILE_O_EXCL)) ==
					(PMEMFILE_O_CREAT|PMEMFILE_O_EXCL))
				break;

			resolve_symlink(pfp, vinode, &info);
			path_info_changed = true;
		}
	} while (path_info_changed);

	if (vinode && !vinode_is_dir(vinode) && strchr(info.remaining, '/')) {
		error = ENOTDIR;
		goto end;
	}

	if (is_tmpfile(flags)) {
		if (!vinode) {
			error = ENOENT;
			goto end;
		}

		if (!vinode_is_dir(vinode)) {
			error = ENOTDIR;
			goto end;
		}

		if ((flags & PMEMFILE_O_ACCMODE) == PMEMFILE_O_RDONLY) {
			error = EINVAL;
			goto end;
		}
	} else if ((flags & (PMEMFILE_O_CREAT | PMEMFILE_O_EXCL)) ==
			(PMEMFILE_O_CREAT | PMEMFILE_O_EXCL)) {
		if (vinode) {
			LOG(LUSR, "file %s already exists", pathname);
			error = EEXIST;
			goto end;
		}

		if (!vinode_is_dir(vparent)) {
			error = ENOTDIR;
			goto end;
		}
	} else if ((flags & PMEMFILE_O_CREAT) == PMEMFILE_O_CREAT) {
		/* nothing to be done here */
	} else {
		if (!vinode) {
			error = ENOENT;
			goto end;
		}
	}

	if (is_tmpfile(flags)) {
		vinode_unref_tx(pfp, vparent);
		vparent = vinode;
		vinode = NULL;
	}

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		if (vinode == NULL) {
			vinode = create_file(pfp, info.remaining, namelen,
					vparent, flags, mode);
		} else {
			open_file(vinode, flags);
		}

		file = calloc(1, sizeof(*file));
		if (!file)
			pmemfile_tx_abort(errno);

		file->vinode = vinode;

		if ((flags & PMEMFILE_O_ACCMODE) == PMEMFILE_O_RDONLY)
			file->flags = PFILE_READ;
		else if ((flags & PMEMFILE_O_ACCMODE) == PMEMFILE_O_WRONLY)
			file->flags = PFILE_WRITE;
		else if ((flags & PMEMFILE_O_ACCMODE) == PMEMFILE_O_RDWR)
			file->flags = PFILE_READ | PFILE_WRITE;

		if (flags & PMEMFILE_O_NOATIME)
			file->flags |= PFILE_NOATIME;
		if (flags & PMEMFILE_O_APPEND)
			file->flags |= PFILE_APPEND;
	} TX_ONABORT {
		error = errno;
	} TX_END

end:
	path_info_cleanup(pfp, &info);

	if (error) {
		if (vinode != NULL)
			vinode_unref_tx(pfp, vinode);

		errno = error;
		LOG(LDBG, "!");

		return NULL;
	}

	ASSERT(file != NULL);
	os_mutex_init(&file->mutex);

	LOG(LDBG, "pathname %s opened inode 0x%lx", orig_pathname,
			file->vinode->tinode.oid.off);
	return file;
}

/*
 * pmemfile_openat -- open file
 */
PMEMfile *
pmemfile_openat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		int flags, ...)
{
	if (!pathname) {
		LOG(LUSR, "NULL pathname");
		errno = ENOENT;
		return NULL;
	}

	va_list ap;
	va_start(ap, flags);
	mode_t mode = 0;
	if ((flags & PMEMFILE_O_CREAT) || is_tmpfile(flags))
		mode = va_arg(ap, mode_t);
	va_end(ap);

	struct pmemfile_vinode *at;
	bool at_unref;

	at = pool_get_dir_for_path(pfp, dir, pathname, &at_unref);

	PMEMfile *ret = _pmemfile_openat(pfp, at, pathname, flags, mode);

	if (at_unref) {
		int error;
		if (ret == NULL)
			error = errno;

		vinode_unref_tx(pfp, at);

		if (ret == NULL)
			errno = error;
	}

	return ret;
}

/*
 * pmemfile_open -- open file
 */
PMEMfile *
pmemfile_open(PMEMfilepool *pfp, const char *pathname, int flags, ...)
{
	va_list ap;
	va_start(ap, flags);
	mode_t mode = 0;
	if ((flags & PMEMFILE_O_CREAT) || is_tmpfile(flags))
		mode = va_arg(ap, mode_t);
	va_end(ap);

	return pmemfile_openat(pfp, PMEMFILE_AT_CWD, pathname, flags, mode);
}

PMEMfile *
pmemfile_create(PMEMfilepool *pfp, const char *pathname, mode_t mode)
{
	return pmemfile_open(pfp, pathname, PMEMFILE_O_CREAT |
			PMEMFILE_O_WRONLY | PMEMFILE_O_TRUNC, mode);
}

/*
 * pmemfile_open_parent -- open a parent directory and return filename
 *
 * Together with *at interfaces it's very useful for path resolution when
 * pmemfile is mounted in place other than "/".
 */
PMEMfile *
pmemfile_open_parent(PMEMfilepool *pfp, PMEMfile *dir, char *path,
		size_t path_size, int flags)
{
	PMEMfile *ret = NULL;
	struct pmemfile_vinode *at;
	bool at_unref;
	int error = 0;

	at = pool_get_dir_for_path(pfp, dir, path, &at_unref);

	struct pmemfile_path_info info;
	resolve_pathat(pfp, at, path, &info, flags);

	struct pmemfile_vinode *vparent;
	bool path_info_changed;

	do {
		path_info_changed = false;
		vparent = info.vinode;

		if (vparent == NULL) {
			error = ELOOP;
			goto end;
		}

		if (flags & PMEMFILE_OPEN_PARENT_SYMLINK_FOLLOW) {
			if (more_than_1_component(info.remaining))
				break;

			size_t namelen = component_length(info.remaining);

			if (namelen == 0)
				break;

			struct pmemfile_vinode *vinode =
					vinode_lookup_dirent(pfp, info.vinode,
					info.remaining, namelen, 0);

			if (vinode) {
				if (vinode_is_symlink(vinode)) {
					resolve_symlink(pfp, vinode, &info);
					path_info_changed = true;
				} else {
					vinode_unref_tx(pfp, vinode);
				}
			}
		}
	} while (path_info_changed);

	ret = calloc(1, sizeof(*ret));
	if (!ret) {
		error = errno;
		goto end;
	}

	ret->vinode = vinode_ref(pfp, vparent);
	ret->flags = PFILE_READ | PFILE_NOATIME;
	os_mutex_init(&ret->mutex);
	size_t len = strlen(info.remaining);
	if (len >= path_size)
		len = path_size - 1;
	memmove(path, info.remaining, len);
	path[len] = 0;

end:
	path_info_cleanup(pfp, &info);

	if (at_unref)
		vinode_unref_tx(pfp, at);

	if (error) {
		errno = error;
		return NULL;
	}

	return ret;
}

/*
 * pmemfile_close -- close file
 */
void
pmemfile_close(PMEMfilepool *pfp, PMEMfile *file)
{
	LOG(LDBG, "inode 0x%lx path %s", file->vinode->tinode.oid.off,
			pmfi_path(file->vinode));

	vinode_unref_tx(pfp, file->vinode);

	os_mutex_destroy(&file->mutex);

	free(file);
}

static int
_pmemfile_linkat(PMEMfilepool *pfp,
		struct pmemfile_vinode *olddir, const char *oldpath,
		struct pmemfile_vinode *newdir, const char *newpath,
		int flags)
{
	LOG(LDBG, "oldpath %s newpath %s", oldpath, newpath);

	if (oldpath[0] == 0 && (flags & PMEMFILE_AT_EMPTY_PATH)) {
		LOG(LSUP, "AT_EMPTY_PATH not supported yet");
		errno = EINVAL;
		return -1;
	}

	if ((flags & ~(PMEMFILE_AT_SYMLINK_FOLLOW | PMEMFILE_AT_EMPTY_PATH))
			!= 0) {
		errno = EINVAL;
		return -1;
	}

	struct pmemfile_path_info src, dst = { NULL, NULL };
	struct pmemfile_vinode *src_vinode;

	resolve_pathat(pfp, olddir, oldpath, &src, 0);

	int error = 0;
	bool src_path_info_changed;

	do {
		src_path_info_changed = false;
		src_vinode = NULL;

		if (src.vinode == NULL) {
			error = ELOOP;
			goto end;
		}

		if (!vinode_is_dir(src.vinode)) {
			error = ENOTDIR;
			goto end;
		}

		if (more_than_1_component(src.remaining)) {
			error = ENOENT;
			goto end;
		}

		size_t src_namelen = component_length(src.remaining);

		src_vinode = vinode_lookup_dirent(pfp, src.vinode,
				src.remaining, src_namelen, 0);
		if (!src_vinode) {
			error = ENOENT;
			goto end;
		}

		if (vinode_is_dir(src_vinode)) {
			error = EPERM;
			goto end;
		}

		if (strchr(src.remaining, '/')) {
			error = ENOTDIR;
			goto end;
		}

		if (vinode_is_symlink(src_vinode) &&
				(flags & PMEMFILE_AT_SYMLINK_FOLLOW)) {
			resolve_symlink(pfp, src_vinode, &src);
			src_path_info_changed = true;
		}
	} while (src_path_info_changed);

	resolve_pathat(pfp, newdir, newpath, &dst, 0);

	if (dst.vinode == NULL) {
		error = ELOOP;
		goto end;
	}

	if (!vinode_is_dir(dst.vinode)) {
		error = ENOTDIR;
		goto end;
	}

	if (more_than_1_component(dst.remaining)) {
		error = ENOENT;
		goto end;
	}

	// XXX: handle protected_hardlinks (see man 5 proc)

	size_t dst_namelen = component_length(dst.remaining);

	os_rwlock_wrlock(&dst.vinode->rwlock);

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		struct pmemfile_time t;
		file_get_time(&t);
		vinode_add_dirent(pfp, dst.vinode, dst.remaining, dst_namelen,
				src_vinode, &t);
	} TX_ONABORT {
		error = errno;
	} TX_END

	os_rwlock_unlock(&dst.vinode->rwlock);

	if (error == 0) {
		vinode_clear_debug_path(pfp, src_vinode);
		vinode_set_debug_path(pfp, dst.vinode, src_vinode,
				dst.remaining, dst_namelen);
	}

end:
	path_info_cleanup(pfp, &dst);
	path_info_cleanup(pfp, &src);

	if (src_vinode)
		vinode_unref_tx(pfp, src_vinode);

	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

int
pmemfile_linkat(PMEMfilepool *pfp, PMEMfile *olddir, const char *oldpath,
		PMEMfile *newdir, const char *newpath, int flags)
{
	struct pmemfile_vinode *olddir_at, *newdir_at;
	bool olddir_at_unref, newdir_at_unref;

	if (!oldpath || !newpath) {
		LOG(LUSR, "NULL pathname");
		errno = ENOENT;
		return -1;
	}

	olddir_at = pool_get_dir_for_path(pfp, olddir, oldpath,
			&olddir_at_unref);
	newdir_at = pool_get_dir_for_path(pfp, newdir, newpath,
			&newdir_at_unref);

	int ret = _pmemfile_linkat(pfp, olddir_at, oldpath, newdir_at, newpath,
			flags);
	int error;
	if (ret)
		error = errno;

	if (olddir_at_unref)
		vinode_unref_tx(pfp, olddir_at);

	if (newdir_at_unref)
		vinode_unref_tx(pfp, newdir_at);

	if (ret)
		errno = error;

	return ret;
}

/*
 * pmemfile_link -- make a new name for a file
 */
int
pmemfile_link(PMEMfilepool *pfp, const char *oldpath, const char *newpath)
{
	struct pmemfile_vinode *at;

	if (!oldpath || !newpath) {
		LOG(LUSR, "NULL pathname");
		errno = ENOENT;
		return -1;
	}

	if (oldpath[0] == '/' && newpath[0] == '/')
		at = NULL;
	else
		at = pool_get_cwd(pfp);

	int ret = _pmemfile_linkat(pfp, at, oldpath, at, newpath, 0);

	if (at) {
		int error;
		if (ret)
			error = errno;

		vinode_unref_tx(pfp, at);

		if (ret)
			errno = error;
	}

	return ret;
}

static int
_pmemfile_unlinkat(PMEMfilepool *pfp, struct pmemfile_vinode *dir,
		const char *pathname)
{
	LOG(LDBG, "pathname %s", pathname);

	int error = 0;

	struct pmemfile_path_info info;
	resolve_pathat(pfp, dir, pathname, &info, 0);
	struct pmemfile_vinode *vparent = info.vinode;
	struct pmemfile_vinode *volatile vinode = NULL;
	volatile bool parent_refed = false;

	if (vparent == NULL) {
		error = ELOOP;
		goto end;
	}

	if (!vinode_is_dir(vparent)) {
		error = ENOTDIR;
		goto end;
	}

	if (more_than_1_component(info.remaining)) {
		error = ENOENT;
		goto end;
	}

	size_t namelen = component_length(info.remaining);

	if (strchr(info.remaining, '/')) {
		error = ENOTDIR;
		goto end;
	}

	os_rwlock_wrlock(&vparent->rwlock);

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		vinode_unlink_dirent(pfp, vparent, info.remaining, namelen,
				&vinode, &parent_refed, true);
	} TX_ONABORT {
		error = errno;
	} TX_END

	os_rwlock_unlock(&vparent->rwlock);

end:
	path_info_cleanup(pfp, &info);

	if (vinode)
		vinode_unref_tx(pfp, vinode);

	if (error) {
		if (parent_refed)
			vinode_unref_tx(pfp, vparent);
		errno = error;
		return -1;
	}

	return 0;
}

int
pmemfile_unlinkat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		int flags)
{
	struct pmemfile_vinode *at;
	bool at_unref;

	if (!pathname) {
		errno = ENOENT;
		return -1;
	}

	at = pool_get_dir_for_path(pfp, dir, pathname, &at_unref);

	int ret;

	if (flags & PMEMFILE_AT_REMOVEDIR)
		ret = _pmemfile_rmdirat(pfp, at, pathname);
	else {
		if (flags != 0) {
			errno = EINVAL;
			ret = -1;
		} else {
			ret = _pmemfile_unlinkat(pfp, at, pathname);
		}
	}

	if (at_unref) {
		int error;
		if (ret)
			error = errno;

		vinode_unref_tx(pfp, at);

		if (ret)
			errno = error;
	}

	return ret;
}

/*
 * pmemfile_unlink -- delete a name and possibly the file it refers to
 */
int
pmemfile_unlink(PMEMfilepool *pfp, const char *pathname)
{
	return pmemfile_unlinkat(pfp, PMEMFILE_AT_CWD, pathname, 0);
}

static int
_pmemfile_renameat2(PMEMfilepool *pfp,
		struct pmemfile_vinode *olddir, const char *oldpath,
		struct pmemfile_vinode *newdir, const char *newpath,
		unsigned flags)
{
	LOG(LDBG, "oldpath %s newpath %s", oldpath, newpath);

	if (flags) {
		LOG(LSUP, "0 flags supported in rename");
		errno = EINVAL;
		return -1;
	}

	struct pmemfile_vinode *volatile dst_unlinked = NULL;
	struct pmemfile_vinode *volatile src_unlinked = NULL;
	volatile bool dst_parent_refed = false;
	volatile bool src_parent_refed = false;
	struct pmemfile_vinode *src_vinode = NULL, *dst_vinode = NULL;

	struct pmemfile_path_info src, dst;
	resolve_pathat(pfp, olddir, oldpath, &src, 0);
	resolve_pathat(pfp, newdir, newpath, &dst, 0);

	int error = 0;

	if (src.vinode == NULL) {
		error = ELOOP;
		goto end;
	}

	if (dst.vinode == NULL) {
		error = ELOOP;
		goto end;
	}

	if (!vinode_is_dir(src.vinode)) {
		error = ENOTDIR;
		goto end;
	}

	if (!vinode_is_dir(dst.vinode)) {
		error = ENOTDIR;
		goto end;
	}

	if (more_than_1_component(src.remaining)) {
		error = ENOENT;
		goto end;
	}

	size_t src_namelen = component_length(src.remaining);

	if (more_than_1_component(dst.remaining)) {
		error = ENOENT;
		goto end;
	}

	size_t dst_namelen = component_length(dst.remaining);

	src_vinode = vinode_lookup_dirent(pfp, src.vinode, src.remaining,
			src_namelen, 0);
	if (!src_vinode) {
		error = ENOENT;
		goto end;
	}

	dst_vinode = vinode_lookup_dirent(pfp, dst.vinode, dst.remaining,
			dst_namelen, 0);

	struct pmemfile_vinode *src_parent = src.vinode;
	struct pmemfile_vinode *dst_parent = dst.vinode;

	if (vinode_is_dir(src_vinode)) {
		LOG(LSUP, "renaming directories is not supported yet");
		error = ENOTSUP;
		goto end;
	}

	if (src_parent == dst_parent)
		os_rwlock_wrlock(&dst_parent->rwlock);
	else if (src_parent < dst_parent) {
		os_rwlock_wrlock(&src_parent->rwlock);
		os_rwlock_wrlock(&dst_parent->rwlock);
	} else {
		os_rwlock_wrlock(&dst_parent->rwlock);
		os_rwlock_wrlock(&src_parent->rwlock);
	}

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		// XXX, when src dir == dst dir we can just update dirent,
		// without linking and unlinking

		vinode_unlink_dirent(pfp, dst_parent, dst.remaining,
				dst_namelen, &dst_unlinked, &dst_parent_refed,
				false);

		struct pmemfile_time t;
		file_get_time(&t);
		vinode_add_dirent(pfp, dst_parent, dst.remaining, dst_namelen,
				src_vinode, &t);

		vinode_unlink_dirent(pfp, src_parent, src.remaining,
				src_namelen, &src_unlinked, &src_parent_refed,
				true);

		if (src_unlinked != src_vinode)
			// XXX restart? lookups under lock?
			pmemfile_tx_abort(ENOENT);

	} TX_ONABORT {
		error = errno;
	} TX_END

	if (src_parent == dst_parent)
		os_rwlock_unlock(&dst_parent->rwlock);
	else {
		os_rwlock_unlock(&src_parent->rwlock);
		os_rwlock_unlock(&dst_parent->rwlock);
	}

	if (dst_parent_refed)
		vinode_unref_tx(pfp, dst_parent);

	if (src_parent_refed)
		vinode_unref_tx(pfp, src_parent);

	if (dst_unlinked)
		vinode_unref_tx(pfp, dst_unlinked);

	if (src_unlinked)
		vinode_unref_tx(pfp, src_unlinked);

	if (error == 0) {
		vinode_clear_debug_path(pfp, src_vinode);
		vinode_set_debug_path(pfp, dst.vinode, src_vinode,
				dst.remaining, dst_namelen);
	}

end:
	path_info_cleanup(pfp, &dst);
	path_info_cleanup(pfp, &src);

	if (dst_vinode)
		vinode_unref_tx(pfp, dst_vinode);
	if (src_vinode)
		vinode_unref_tx(pfp, src_vinode);

	if (error) {
		if (dst_parent_refed)
			vinode_unref_tx(pfp, dst.vinode);

		errno = error;
		return -1;
	}

	return 0;
}

int
pmemfile_rename(PMEMfilepool *pfp, const char *old_path, const char *new_path)
{
	struct pmemfile_vinode *at;

	if (!old_path || !new_path) {
		LOG(LUSR, "NULL pathname");
		errno = ENOENT;
		return -1;
	}

	if (old_path[0] == '/' && new_path[0] == '/')
		at = NULL;
	else
		at = pool_get_cwd(pfp);

	int ret = _pmemfile_renameat2(pfp, at, old_path, at, new_path, 0);

	if (at) {
		int error;
		if (ret)
			error = errno;

		vinode_unref_tx(pfp, at);

		if (ret)
			errno = error;
	}

	return ret;
}

int
pmemfile_renameat2(PMEMfilepool *pfp, PMEMfile *old_at, const char *old_path,
		PMEMfile *new_at, const char *new_path, unsigned flags)
{
	struct pmemfile_vinode *olddir_at, *newdir_at;
	bool olddir_at_unref, newdir_at_unref;

	if (!old_path || !new_path) {
		LOG(LUSR, "NULL pathname");
		errno = ENOENT;
		return -1;
	}

	olddir_at = pool_get_dir_for_path(pfp, old_at, old_path,
			&olddir_at_unref);
	newdir_at = pool_get_dir_for_path(pfp, new_at, new_path,
			&newdir_at_unref);

	int ret = _pmemfile_renameat2(pfp, olddir_at, old_path, newdir_at,
			new_path, flags);
	int error;
	if (ret)
		error = errno;

	if (olddir_at_unref)
		vinode_unref_tx(pfp, olddir_at);

	if (newdir_at_unref)
		vinode_unref_tx(pfp, newdir_at);

	if (ret)
		errno = error;

	return ret;
}

int
pmemfile_renameat(PMEMfilepool *pfp, PMEMfile *old_at, const char *old_path,
		PMEMfile *new_at, const char *new_path)
{
	return pmemfile_renameat2(pfp, old_at, old_path, new_at, new_path, 0);
}

static int
_pmemfile_symlinkat(PMEMfilepool *pfp, const char *target,
		struct pmemfile_vinode *dir, const char *linkpath)
{
	LOG(LDBG, "target %s linkpath %s", target, linkpath);

	int error = 0;

	struct pmemfile_path_info info;
	resolve_pathat(pfp, dir, linkpath, &info, 0);
	struct pmemfile_vinode *vinode = NULL;

	struct pmemfile_vinode *vparent = info.vinode;

	if (vparent == NULL) {
		error = ELOOP;
		goto end;
	}

	if (!vinode_is_dir(vparent)) {
		error = ENOTDIR;
		goto end;
	}

	if (more_than_1_component(info.remaining)) {
		error = ENOENT;
		goto end;
	}

	size_t namelen = component_length(info.remaining);

	vinode = vinode_lookup_dirent(pfp, info.vinode, info.remaining,
			namelen, 0);
	if (vinode) {
		error = EEXIST;
		goto end;
	}

	size_t len = strlen(target);

	if (len >= PMEMFILE_IN_INODE_STORAGE) {
		error = ENAMETOOLONG;
		goto end;
	}

	os_rwlock_wrlock(&vparent->rwlock);

	TX_BEGIN_CB(pfp->pop, cb_queue, pfp) {
		struct pmemfile_time t;

		vinode = inode_alloc(pfp, PMEMFILE_S_IFLNK |
				PMEMFILE_ACCESSPERMS, &t, vparent, NULL,
				info.remaining, namelen);
		struct pmemfile_inode *inode = vinode->inode;
		pmemobj_memcpy_persist(pfp->pop, inode->file_data.data, target,
				len);
		inode->size = len;

		vinode_add_dirent(pfp, vparent, info.remaining, namelen,
				vinode, &t);
	} TX_ONABORT {
		error = errno;
		vinode = NULL;
	} TX_END

	os_rwlock_unlock(&vparent->rwlock);

end:
	path_info_cleanup(pfp, &info);

	if (vinode)
		vinode_unref_tx(pfp, vinode);

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

	if (!target || !linkpath) {
		errno = ENOENT;
		return -1;
	}

	at = pool_get_dir_for_path(pfp, newdir, linkpath, &at_unref);

	int ret = _pmemfile_symlinkat(pfp, target, at, linkpath);

	if (at_unref) {
		int error;
		if (ret)
			error = errno;

		vinode_unref_tx(pfp, at);

		if (ret)
			errno = error;
	}

	return ret;
}

int
pmemfile_symlink(PMEMfilepool *pfp, const char *target, const char *linkpath)
{
	return pmemfile_symlinkat(pfp, target, PMEMFILE_AT_CWD, linkpath);
}

static ssize_t
_pmemfile_readlinkat(PMEMfilepool *pfp, struct pmemfile_vinode *dir,
		const char *pathname, char *buf, size_t bufsiz)
{
	int error = 0;
	ssize_t ret = -1;
	struct pmemfile_vinode *vinode = NULL;
	struct pmemfile_path_info info;
	resolve_pathat(pfp, dir, pathname, &info, 0);

	if (info.vinode == NULL) {
		error = ELOOP;
		goto end;
	}

	if (!vinode_is_dir(info.vinode)) {
		error = ENOTDIR;
		goto end;
	}

	if (more_than_1_component(info.remaining)) {
		error = ENOENT;
		goto end;
	}

	size_t namelen = component_length(info.remaining);

	vinode = vinode_lookup_dirent(pfp, info.vinode, info.remaining,
			namelen, 0);
	if (!vinode) {
		error = ENOENT;
		goto end;
	}

	if (!vinode_is_symlink(vinode)) {
		error = EINVAL;
		goto end;
	}

	if (strchr(info.remaining, '/')) {
		error = ENOTDIR;
		goto end;
	}

	os_rwlock_rdlock(&vinode->rwlock);

	const char *data = vinode->inode->file_data.data;
	size_t len = strlen(data);
	if (len > bufsiz)
		len = bufsiz;
	memcpy(buf, data, len);
	ret = (ssize_t)len;

	os_rwlock_unlock(&vinode->rwlock);

end:
	path_info_cleanup(pfp, &info);

	if (vinode)
		vinode_unref_tx(pfp, vinode);

	if (error) {
		errno = error;
		return -1;
	}

	return ret;
}

ssize_t
pmemfile_readlinkat(PMEMfilepool *pfp, PMEMfile *dir, const char *pathname,
		char *buf, size_t bufsiz)
{
	struct pmemfile_vinode *at;
	bool at_unref;

	if (!pathname) {
		errno = ENOENT;
		return -1;
	}

	at = pool_get_dir_for_path(pfp, dir, pathname, &at_unref);

	ssize_t ret = _pmemfile_readlinkat(pfp, at, pathname, buf, bufsiz);

	if (at_unref) {
		/*
		 * initialized only because gcc 6.2 thinks "error" might not be
		 * initialized at the time of writing it back to "errno"
		 */
		int error = 0;
		if (ret < 0)
			error = errno;

		vinode_unref_tx(pfp, at);

		if (ret < 0)
			errno = error;
	}

	return ret;
}

ssize_t
pmemfile_readlink(PMEMfilepool *pfp, const char *pathname, char *buf,
		size_t bufsiz)
{
	return pmemfile_readlinkat(pfp, PMEMFILE_AT_CWD, pathname, buf, bufsiz);
}

int
pmemfile_fcntl(PMEMfilepool *pfp, PMEMfile *file, int cmd, ...)
{
	int ret = 0;

	(void) pfp;
	(void) file;

	switch (cmd) {
		case PMEMFILE_F_SETLK:
		case PMEMFILE_F_UNLCK:
			// XXX
			return 0;
		case PMEMFILE_F_GETFL:
			ret |= PMEMFILE_O_LARGEFILE;
			if (file->flags & PFILE_APPEND)
				ret |= PMEMFILE_O_APPEND;
			if (file->flags & PFILE_NOATIME)
				ret |= PMEMFILE_O_NOATIME;
			if ((file->flags & PFILE_READ) == PFILE_READ)
				ret |= PMEMFILE_O_RDONLY;
			if ((file->flags & PFILE_WRITE) == PFILE_WRITE)
				ret |= PMEMFILE_O_WRONLY;
			if ((file->flags & (PFILE_READ | PFILE_WRITE)) ==
					(PFILE_READ | PFILE_WRITE))
				ret |= PMEMFILE_O_RDWR;
			return ret;
	}

	errno = ENOTSUP;
	return -1;
}

/*
 * pmemfile_stats -- get pool statistics
 */
void
pmemfile_stats(PMEMfilepool *pfp, struct pmemfile_stats *stats)
{
	PMEMoid oid;
	unsigned inodes = 0, dirs = 0, block_arrays = 0, inode_arrays = 0,
			blocks = 0;

	POBJ_FOREACH(pfp->pop, oid) {
		unsigned t = (unsigned)pmemobj_type_num(oid);

		if (t == TOID_TYPE_NUM(struct pmemfile_inode))
			inodes++;
		else if (t == TOID_TYPE_NUM(struct pmemfile_dir))
			dirs++;
		else if (t == TOID_TYPE_NUM(struct pmemfile_block_array))
			block_arrays++;
		else if (t == TOID_TYPE_NUM(struct pmemfile_inode_array))
			inode_arrays++;
		else if (t == TOID_TYPE_NUM(char))
			blocks++;
		else
			FATAL("unknown type %u", t);
	}
	stats->inodes = inodes;
	stats->dirs = dirs;
	stats->block_arrays = block_arrays;
	stats->inode_arrays = inode_arrays;
	stats->blocks = blocks;
}
