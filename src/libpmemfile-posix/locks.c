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

#include "callbacks.h"
#include "inode.h"
#include "internal.h"
#include "locks.h"

#include "os_thread.h"
#include "out.h"

/*
 * rwlock_unlock_cb -- wrapper around os_rwlock_unlock to be used as a callback
 */
static void
rwlock_unlock_cb(PMEMfilepool *pfp, os_rwlock_t *arg)
{
	(void) pfp;

	os_rwlock_unlock(arg);
}

/*
 * rwlock_tx_wlock -- transactional read-write lock
 */
void
rwlock_tx_wlock(os_rwlock_t *l)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	cb_push_front(TX_STAGE_ONABORT, (cb_basic)rwlock_unlock_cb, l);

	os_rwlock_wrlock(l);
}

/*
 * rwlock_tx_unlock_on_commit -- transactional read-write unlock
 */
void
rwlock_tx_unlock_on_commit(os_rwlock_t *l)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	cb_push_back(TX_STAGE_ONCOMMIT, (cb_basic)rwlock_unlock_cb, l);
}

/*
 * mutex_unlock_cb -- wrapper around pmemobj_mutex_unlock to be used as
 * a callback
 */
static void
mutex_unlock_cb(PMEMfilepool *pfp, PMEMmutex *mutexp)
{
	pmemobj_mutex_unlock_nofail(pfp->pop, mutexp);
}

/*
 * mutex_tx_unlock_on_abort -- postpones pmemobj_mutex_unlock to
 * transaction abort
 */
void
mutex_tx_unlock_on_abort(PMEMmutex *mutexp)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	cb_push_front(TX_STAGE_ONABORT, (cb_basic)mutex_unlock_cb, mutexp);
}

/*
 * mutex_tx_lock -- transactional pmemobj_mutex_lock
 */
void
mutex_tx_lock(PMEMfilepool *pfp, PMEMmutex *mutexp)
{
	cb_push_front(TX_STAGE_ONABORT, (cb_basic)mutex_unlock_cb, mutexp);

	pmemobj_mutex_lock_nofail(pfp->pop, mutexp);
}

/*
 * mutex_tx_unlock_on_commit -- postpones pmemobj_mutex_unlock to
 * transaction commit
 */
void
mutex_tx_unlock_on_commit(PMEMmutex *mutexp)
{
	ASSERTeq(pmemobj_tx_stage(), TX_STAGE_WORK);

	cb_push_back(TX_STAGE_ONCOMMIT, (cb_basic)mutex_unlock_cb, mutexp);
}
