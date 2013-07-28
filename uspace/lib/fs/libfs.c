/*
 * Copyright (c) 2009 Jakub Jermar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup libfs
 * @{
 */
/**
 * @file
 * Glue code which is common to all FS implementations.
 */

#include "libfs.h"
#include "../../srv/vfs/vfs.h"
#include <macros.h>
#include <errno.h>
#include <async.h>
#include <as.h>
#include <assert.h>
#include <dirent.h>
#include <mem.h>
#include <sys/stat.h>
#include <stdlib.h>

#define on_error(rc, action) \
	do { \
		if ((rc) != EOK) \
			action; \
	} while (0)

#define combine_rc(rc1, rc2) \
	((rc1) == EOK ? (rc2) : (rc1))

#define answer_and_return(rid, rc) \
	do { \
		async_answer_0((rid), (rc)); \
		return; \
	} while (0)

#define DPRINTF(...)

#define LOG_EXIT(rc) \
	DPRINTF("Exiting %s() with rc = %d at line %d\n", __FUNC__, rc, __LINE__);

static fs_reg_t reg;

static vfs_out_ops_t *vfs_out_ops = NULL;
static libfs_ops_t *libfs_ops = NULL;

static void libfs_mount(libfs_ops_t *, fs_handle_t, ipc_callid_t, ipc_call_t *);
static void libfs_unmount(libfs_ops_t *, ipc_callid_t, ipc_call_t *);
static void libfs_link(libfs_ops_t *, fs_handle_t, ipc_callid_t,
    ipc_call_t *);
static void libfs_lookup(libfs_ops_t *, fs_handle_t, ipc_callid_t,
    ipc_call_t *);
static void libfs_stat(libfs_ops_t *, fs_handle_t, ipc_callid_t, ipc_call_t *);
static void libfs_open_node(libfs_ops_t *, fs_handle_t, ipc_callid_t,
    ipc_call_t *);

static void vfs_out_mounted(ipc_callid_t rid, ipc_call_t *req)
{
	service_id_t service_id = (service_id_t) IPC_GET_ARG1(*req);
	char *opts;
	int rc;
	
	/* Accept the mount options. */
	rc = async_data_write_accept((void **) &opts, true, 0, 0, 0, NULL);
	if (rc != EOK) {
		async_answer_0(rid, rc);
		return;
	}

	fs_index_t index;
	aoff64_t size;
	unsigned lnkcnt;
	rc = vfs_out_ops->mounted(service_id, opts, &index, &size, &lnkcnt);

	if (rc == EOK)
		async_answer_4(rid, EOK, index, LOWER32(size), UPPER32(size),
		    lnkcnt);
	else
		async_answer_0(rid, rc);

	free(opts);
}

static void vfs_out_mount(ipc_callid_t rid, ipc_call_t *req)
{
	libfs_mount(libfs_ops, reg.fs_handle, rid, req);
}

static void vfs_out_unmounted(ipc_callid_t rid, ipc_call_t *req)
{
	service_id_t service_id = (service_id_t) IPC_GET_ARG1(*req);
	int rc; 

	rc = vfs_out_ops->unmounted(service_id);

	async_answer_0(rid, rc);
}

static void vfs_out_unmount(ipc_callid_t rid, ipc_call_t *req)
{
		
	libfs_unmount(libfs_ops, rid, req);
}

static void vfs_out_link(ipc_callid_t rid, ipc_call_t *req)
{
	libfs_link(libfs_ops, reg.fs_handle, rid, req);
}

static void vfs_out_lookup(ipc_callid_t rid, ipc_call_t *req)
{
	libfs_lookup(libfs_ops, reg.fs_handle, rid, req);
}

static void vfs_out_read(ipc_callid_t rid, ipc_call_t *req)
{
	service_id_t service_id = (service_id_t) IPC_GET_ARG1(*req);
	fs_index_t index = (fs_index_t) IPC_GET_ARG2(*req);
	aoff64_t pos = (aoff64_t) MERGE_LOUP32(IPC_GET_ARG3(*req),
	    IPC_GET_ARG4(*req));
	size_t rbytes;
	int rc;

	rc = vfs_out_ops->read(service_id, index, pos, &rbytes);

	if (rc == EOK)
		async_answer_1(rid, EOK, rbytes);
	else
		async_answer_0(rid, rc);
}

static void vfs_out_write(ipc_callid_t rid, ipc_call_t *req)
{
	service_id_t service_id = (service_id_t) IPC_GET_ARG1(*req);
	fs_index_t index = (fs_index_t) IPC_GET_ARG2(*req);
	aoff64_t pos = (aoff64_t) MERGE_LOUP32(IPC_GET_ARG3(*req),
	    IPC_GET_ARG4(*req));
	size_t wbytes;
	aoff64_t nsize;
	int rc;

	rc = vfs_out_ops->write(service_id, index, pos, &wbytes, &nsize);

	if (rc == EOK)
		async_answer_3(rid, EOK, wbytes, LOWER32(nsize), UPPER32(nsize));
	else
		async_answer_0(rid, rc);
}

static void vfs_out_truncate(ipc_callid_t rid, ipc_call_t *req)
{
	service_id_t service_id = (service_id_t) IPC_GET_ARG1(*req);
	fs_index_t index = (fs_index_t) IPC_GET_ARG2(*req);
	aoff64_t size = (aoff64_t) MERGE_LOUP32(IPC_GET_ARG3(*req),
	    IPC_GET_ARG4(*req));
	int rc;

	rc = vfs_out_ops->truncate(service_id, index, size);

	async_answer_0(rid, rc);
}

static void vfs_out_close(ipc_callid_t rid, ipc_call_t *req)
{
	service_id_t service_id = (service_id_t) IPC_GET_ARG1(*req);
	fs_index_t index = (fs_index_t) IPC_GET_ARG2(*req);
	int rc;

	rc = vfs_out_ops->close(service_id, index);

	async_answer_0(rid, rc);
}

static void vfs_out_destroy(ipc_callid_t rid, ipc_call_t *req)
{
	service_id_t service_id = (service_id_t) IPC_GET_ARG1(*req);
	fs_index_t index = (fs_index_t) IPC_GET_ARG2(*req);
	int rc;

	rc = vfs_out_ops->destroy(service_id, index);

	async_answer_0(rid, rc);
}

static void vfs_out_open_node(ipc_callid_t rid, ipc_call_t *req)
{
	libfs_open_node(libfs_ops, reg.fs_handle, rid, req);
}

static void vfs_out_stat(ipc_callid_t rid, ipc_call_t *req)
{
	libfs_stat(libfs_ops, reg.fs_handle, rid, req);
}

static void vfs_out_sync(ipc_callid_t rid, ipc_call_t *req)
{
	service_id_t service_id = (service_id_t) IPC_GET_ARG1(*req);
	fs_index_t index = (fs_index_t) IPC_GET_ARG2(*req);
	int rc;

	rc = vfs_out_ops->sync(service_id, index);

	async_answer_0(rid, rc);
}

static void vfs_connection(ipc_callid_t iid, ipc_call_t *icall, void *arg)
{
	if (iid) {
		/*
		 * This only happens for connections opened by
		 * IPC_M_CONNECT_ME_TO calls as opposed to callback connections
		 * created by IPC_M_CONNECT_TO_ME.
		 */
		async_answer_0(iid, EOK);
	}
	
	while (true) {
		ipc_call_t call;
		ipc_callid_t callid = async_get_call(&call);
		
		if (!IPC_GET_IMETHOD(call))
			return;
		
		switch (IPC_GET_IMETHOD(call)) {
		case VFS_OUT_MOUNTED:
			vfs_out_mounted(callid, &call);
			break;
		case VFS_OUT_MOUNT:
			vfs_out_mount(callid, &call);
			break;
		case VFS_OUT_UNMOUNTED:
			vfs_out_unmounted(callid, &call);
			break;
		case VFS_OUT_UNMOUNT:
			vfs_out_unmount(callid, &call);
			break;
		case VFS_OUT_LINK:
			vfs_out_link(callid, &call);
			break;
		case VFS_OUT_LOOKUP:
			vfs_out_lookup(callid, &call);
			break;
		case VFS_OUT_READ:
			vfs_out_read(callid, &call);
			break;
		case VFS_OUT_WRITE:
			vfs_out_write(callid, &call);
			break;
		case VFS_OUT_TRUNCATE:
			vfs_out_truncate(callid, &call);
			break;
		case VFS_OUT_CLOSE:
			vfs_out_close(callid, &call);
			break;
		case VFS_OUT_DESTROY:
			vfs_out_destroy(callid, &call);
			break;
		case VFS_OUT_OPEN_NODE:
			vfs_out_open_node(callid, &call);
			break;
		case VFS_OUT_STAT:
			vfs_out_stat(callid, &call);
			break;
		case VFS_OUT_SYNC:
			vfs_out_sync(callid, &call);
			break;
		default:
			async_answer_0(callid, ENOTSUP);
			break;
		}
	}
}

/** Register file system server.
 *
 * This function abstracts away the tedious registration protocol from
 * file system implementations and lets them to reuse this registration glue
 * code.
 *
 * @param sess Session for communication with VFS.
 * @param info VFS info structure supplied by the file system
 *             implementation.
 * @param vops Address of the vfs_out_ops_t structure.
 * @param lops Address of the libfs_ops_t structure.
 *
 * @return EOK on success or a non-zero error code on errror.
 *
 */
int fs_register(async_sess_t *sess, vfs_info_t *info, vfs_out_ops_t *vops,
    libfs_ops_t *lops)
{
	/*
	 * Tell VFS that we are here and want to get registered.
	 * We use the async framework because VFS will answer the request
	 * out-of-order, when it knows that the operation succeeded or failed.
	 */
	
	async_exch_t *exch = async_exchange_begin(sess);
	
	ipc_call_t answer;
	aid_t req = async_send_0(exch, VFS_IN_REGISTER, &answer);
	
	/*
	 * Send our VFS info structure to VFS.
	 */
	int rc = async_data_write_start(exch, info, sizeof(*info));
	
	if (rc != EOK) {
		async_exchange_end(exch);
		async_forget(req);
		return rc;
	}
	
	/*
	 * Set VFS_OUT and libfs operations.
	 */
	vfs_out_ops = vops;
	libfs_ops = lops;

	/*
	 * Ask VFS for callback connection.
	 */
	async_connect_to_me(exch, 0, 0, 0, vfs_connection, NULL);
	
	/*
	 * Request sharing the Path Lookup Buffer with VFS.
	 */
	rc = async_share_in_start_0_0(exch, PLB_SIZE, (void *) &reg.plb_ro);
	if (reg.plb_ro == AS_MAP_FAILED) {
		async_exchange_end(exch);
		async_forget(req);
		return ENOMEM;
	}
	
	async_exchange_end(exch);
	
	if (rc) {
		async_forget(req);
		return rc;
	}
	 
	/*
	 * Pick up the answer for the request to the VFS_IN_REQUEST call.
	 */
	async_wait_for(req, NULL);
	reg.fs_handle = (int) IPC_GET_ARG1(answer);
	
	/*
	 * Tell the async framework that other connections are to be handled by
	 * the same connection fibril as well.
	 */
	async_set_client_connection(vfs_connection);
	
	return IPC_GET_RETVAL(answer);
}

void fs_node_initialize(fs_node_t *fn)
{
	memset(fn, 0, sizeof(fs_node_t));
}

void libfs_mount(libfs_ops_t *ops, fs_handle_t fs_handle, ipc_callid_t rid,
    ipc_call_t *req)
{
	service_id_t mp_service_id = (service_id_t) IPC_GET_ARG1(*req);
	fs_index_t mp_fs_index = (fs_index_t) IPC_GET_ARG2(*req);
	fs_handle_t mr_fs_handle = (fs_handle_t) IPC_GET_ARG3(*req);
	service_id_t mr_service_id = (service_id_t) IPC_GET_ARG4(*req);
	
	async_sess_t *mountee_sess = async_clone_receive(EXCHANGE_PARALLEL);
	if (mountee_sess == NULL) {
		async_answer_0(rid, EINVAL);
		return;
	}
	
	fs_node_t *fn;
	int res = ops->node_get(&fn, mp_service_id, mp_fs_index);
	if ((res != EOK) || (!fn)) {
		async_hangup(mountee_sess);
		async_data_write_void(combine_rc(res, ENOENT));
		async_answer_0(rid, combine_rc(res, ENOENT));
		return;
	}
	
	if (fn->mp_data.mp_active) {
		async_hangup(mountee_sess);
		(void) ops->node_put(fn);
		async_data_write_void(EBUSY);
		async_answer_0(rid, EBUSY);
		return;
	}
	
	async_exch_t *exch = async_exchange_begin(mountee_sess);
	async_sess_t *sess = async_clone_establish(EXCHANGE_PARALLEL, exch);
	
	if (!sess) {
		async_exchange_end(exch);
		async_hangup(mountee_sess);
		(void) ops->node_put(fn);
		async_data_write_void(errno);
		async_answer_0(rid, errno);
		return;
	}
	
	ipc_call_t answer;
	int rc = async_data_write_forward_1_1(exch, VFS_OUT_MOUNTED,
	    mr_service_id, &answer);
	async_exchange_end(exch);
	
	if (rc == EOK) {
		fn->mp_data.mp_active = true;
		fn->mp_data.fs_handle = mr_fs_handle;
		fn->mp_data.service_id = mr_service_id;
		fn->mp_data.sess = mountee_sess;
	}
	
	/*
	 * Do not release the FS node so that it stays in memory.
	 */
	async_answer_4(rid, rc, IPC_GET_ARG1(answer), IPC_GET_ARG2(answer),
	    IPC_GET_ARG3(answer), IPC_GET_ARG4(answer));
}

void libfs_unmount(libfs_ops_t *ops, ipc_callid_t rid, ipc_call_t *req)
{
	service_id_t mp_service_id = (service_id_t) IPC_GET_ARG1(*req);
	fs_index_t mp_fs_index = (fs_index_t) IPC_GET_ARG2(*req);
	fs_node_t *fn;
	int res;

	res = ops->node_get(&fn, mp_service_id, mp_fs_index);
	if ((res != EOK) || (!fn)) {
		async_answer_0(rid, combine_rc(res, ENOENT));
		return;
	}

	/*
	 * We are clearly expecting to find the mount point active.
	 */
	if (!fn->mp_data.mp_active) {
		(void) ops->node_put(fn);
		async_answer_0(rid, EINVAL);
		return;
	}

	/*
	 * Tell the mounted file system to unmount.
	 */
	async_exch_t *exch = async_exchange_begin(fn->mp_data.sess);
	res = async_req_1_0(exch, VFS_OUT_UNMOUNTED, fn->mp_data.service_id);
	async_exchange_end(exch);

	/*
	 * If everything went well, perform the clean-up on our side.
	 */
	if (res == EOK) {
		async_hangup(fn->mp_data.sess);
		fn->mp_data.mp_active = false;
		fn->mp_data.fs_handle = 0;
		fn->mp_data.service_id = 0;
		fn->mp_data.sess = NULL;
		
		/* Drop the reference created in libfs_mount(). */
		(void) ops->node_put(fn);
	}

	(void) ops->node_put(fn);
	async_answer_0(rid, res);
}

static char plb_get_char(unsigned pos)
{
	return reg.plb_ro[pos % PLB_SIZE];
}

static int plb_get_component(char *dest, unsigned *sz, unsigned *ppos, unsigned last)
{
	unsigned pos = *ppos;
	unsigned size = 0;
	
	if (pos == last) {
		*sz = 0;
		return ERANGE;
	}

	char c = plb_get_char(pos); 
	if (c == '/') {
		pos++;
	}
	
	for (int i = 0; i <= NAME_MAX; i++) {
		c = plb_get_char(pos);
		if (pos == last || c == '/') {
			dest[i] = 0;
			*ppos = pos;
			*sz = size;
			return EOK;
		}
		dest[i] = c;
		pos++;
		size++;
	}
	return ENAMETOOLONG;
}

static int receive_fname(char *buffer)
{
	size_t size;
	ipc_callid_t wcall;
	
	if (!async_data_write_receive(&wcall, &size)) {
		return ENOENT;
	}
	if (size > NAME_MAX + 1) {
		async_answer_0(wcall, ERANGE);
		return ERANGE;
	}
	return async_data_write_finalize(wcall, buffer, size);
}

/** Link a file at a path.
 */
void libfs_link(libfs_ops_t *ops, fs_handle_t fs_handle, ipc_callid_t rid, ipc_call_t *req)
{
	service_id_t parent_sid = IPC_GET_ARG1(*req);
	fs_index_t parent_index = IPC_GET_ARG2(*req);
	fs_index_t child_index = IPC_GET_ARG3(*req);
	
	char component[NAME_MAX + 1];
	int rc = receive_fname(component);
	if (rc != EOK) {
		async_answer_0(rid, rc);
		return;
	}

	fs_node_t *parent = NULL;
	rc = ops->node_get(&parent, parent_sid, parent_index);
	if (parent == NULL) {
		async_answer_0(rid, rc == EOK ? EBADF : rc);
		return;
	}
	
	fs_node_t *child = NULL;
	rc = ops->node_get(&child, parent_sid, child_index);
	if (child == NULL) {
		async_answer_0(rid, rc == EOK ? EBADF : rc);
		ops->node_put(parent);
		return;
	}
	
	rc = ops->link(parent, child, component);
	ops->node_put(parent);
	ops->node_put(child);
	async_answer_0(rid, rc);
}

/** Lookup VFS triplet by name in the file system name space.
 *
 * The path passed in the PLB must be in the canonical file system path format
 * as returned by the canonify() function.
 *
 * @param ops       libfs operations structure with function pointers to
 *                  file system implementation
 * @param fs_handle File system handle of the file system where to perform
 *                  the lookup.
 * @param rid       Request ID of the VFS_OUT_LOOKUP request.
 * @param request   VFS_OUT_LOOKUP request data itself.
 *
 */
void libfs_lookup(libfs_ops_t *ops, fs_handle_t fs_handle, ipc_callid_t rid, ipc_call_t *req)
{
	unsigned first = IPC_GET_ARG1(*req);
	unsigned len = IPC_GET_ARG2(*req);
	service_id_t service_id = IPC_GET_ARG3(*req);
	fs_index_t index = IPC_GET_ARG4(*req);
	int lflag = IPC_GET_ARG5(*req);
	
	DPRINTF("Entered libfs_lookup()\n");
	
	// TODO: Validate flags.
	
	unsigned next = first;
	unsigned last = first + len;
	
	char component[NAME_MAX + 1];
	int rc;
	
	fs_node_t *par = NULL;
	fs_node_t *cur = NULL;
	fs_node_t *tmp = NULL;
	unsigned clen = 0;
	
	if (index == (fs_index_t)-1) {
		rc = ops->root_get(&cur, service_id);
	} else {
		rc = ops->node_get(&cur, service_id, index);
	}
	if (rc != EOK) {
		async_answer_0(rid, rc);
		LOG_EXIT(rc);
		goto out;
	}
	
	assert(cur != NULL);
	
	if (cur->mp_data.mp_active) {
		async_exch_t *exch = async_exchange_begin(cur->mp_data.sess);
		async_forward_slow(rid, exch, VFS_OUT_LOOKUP, next, last - next,
		    cur->mp_data.service_id, (fs_index_t) -1, lflag, IPC_FF_ROUTE_FROM_ME);
		async_exchange_end(exch);
		
		(void) ops->node_put(cur);
		DPRINTF("Passing to another filesystem instance.\n");
		return;
	}
	
	/* Find the file and its parent. */
	
	while (next != last) {
		if (cur == NULL) {
			async_answer_0(rid, ENOENT);
			LOG_EXIT(ENOENT);
			goto out;
		}
		if (!ops->is_directory(cur)) {
			async_answer_0(rid, ENOTDIR);
			LOG_EXIT(ENOTDIR);
			goto out;
		}
		
		/* Collect the component */
		rc = plb_get_component(component, &clen, &next, last);
		assert(rc != ERANGE);
		if (rc != EOK) {
			async_answer_0(rid, rc);
			LOG_EXIT(rc);
			goto out;
		}
		
		if (clen == 0) {
			/* The path is just "/". */
			break;
		}
		
		assert(component[clen] == 0);
		
		/* Match the component */
		rc = ops->match(&tmp, cur, component);
		if (rc != EOK) {
			async_answer_0(rid, rc);
			LOG_EXIT(rc);
			goto out;
		}
		
		/*
		 * If the matching component is a mount point, there are two
		 * legitimate semantics of the lookup operation. The first is
		 * the commonly used one in which the lookup crosses each mount
		 * point into the mounted file system. The second semantics is
		 * used mostly during unmount() and differs from the first one
		 * only in that the last mount point in the looked up path,
		 * which is also its last component, is not crossed.
		 */

		if ((tmp) && (tmp->mp_data.mp_active) &&
		    (!(lflag & L_MP) || (next < last))) {
			async_exch_t *exch = async_exchange_begin(tmp->mp_data.sess);
			async_forward_slow(rid, exch, VFS_OUT_LOOKUP, next,
			    last - next, tmp->mp_data.service_id, (fs_index_t) -1, lflag,
			    IPC_FF_ROUTE_FROM_ME);
			async_exchange_end(exch);
			DPRINTF("Passing to another filesystem instance.\n");
			goto out;
		}
		
		/* Descend one level */
		if (par) {
			rc = ops->node_put(par);
			if (rc != EOK) {
				async_answer_0(rid, rc);
				LOG_EXIT(rc); 
				goto out;
			}
		}
		
		par = cur;
		cur = tmp;
		tmp = NULL;
	}
	
	/* At this point, par is either NULL or a directory.
	 * If cur is NULL, the looked up file does not exist yet.
	 */
	 
	assert(par == NULL || ops->is_directory(par));
	assert(par != NULL || cur != NULL);
	
	/* Check for some error conditions. */
	
	if (cur && (lflag & L_FILE) && (ops->is_directory(cur))) {
		async_answer_0(rid, EISDIR);
		LOG_EXIT(EISDIR);
		goto out;
	}
	
	if (cur && (lflag & L_DIRECTORY) && (ops->is_file(cur))) {
		async_answer_0(rid, ENOTDIR);
		LOG_EXIT(ENOTDIR);
		goto out;
	}
	
	/* Unlink. */
	
	if (lflag & L_UNLINK) {
		if (!cur) {
			async_answer_0(rid, ENOENT);
			LOG_EXIT(ENOENT);
			goto out;
		}
		if (!par) {
			async_answer_0(rid, EINVAL);
			LOG_EXIT(EINVAL);
			goto out;
		}
		
		unsigned int old_lnkcnt = ops->lnkcnt_get(cur);
		rc = ops->unlink(par, cur, component);
		if (rc == EOK) {
			aoff64_t size = ops->size_get(cur);
			async_answer_5(rid, fs_handle, service_id,
			    ops->index_get(cur), LOWER32(size), UPPER32(size),
			    old_lnkcnt);
			LOG_EXIT(EOK);
		} else {
			async_answer_0(rid, rc);
			LOG_EXIT(rc);
		}
		goto out;
	}
	
	/* Create. */
	
	if (lflag & L_CREATE) {
		if (cur && (lflag & L_EXCLUSIVE)) {
			async_answer_0(rid, EEXIST);
			LOG_EXIT(EEXIST);
			goto out;
		}
	
		if (!cur) {
			rc = ops->create(&cur, service_id, lflag & (L_FILE|L_DIRECTORY));
			if (rc != EOK) {
				async_answer_0(rid, rc);
				LOG_EXIT(rc);
				goto out;
			}
			if (!cur) {
				async_answer_0(rid, ENOSPC);
				LOG_EXIT(ENOSPC);
				goto out;
			}
			
			rc = ops->link(par, cur, component);
			if (rc != EOK) {
				(void) ops->destroy(cur);
				cur = NULL;
				async_answer_0(rid, rc);
				LOG_EXIT(rc);
				goto out;
			}
		}
	}
	
	/* Return. */
	
	if (!cur) {
		async_answer_0(rid, ENOENT);
		LOG_EXIT(ENOENT);
		goto out;
	}
	
	if (lflag & L_OPEN) {
		rc = ops->node_open(cur);
		if (rc != EOK) {
			async_answer_0(rid, rc);
			LOG_EXIT(rc);
			goto out;
		}
	}
	
	aoff64_t size = ops->size_get(cur);
	async_answer_5(rid, fs_handle, service_id,
		ops->index_get(cur), LOWER32(size), UPPER32(size),
		ops->lnkcnt_get(cur));
	
	LOG_EXIT(EOK);
out:
	if (par) {
		(void) ops->node_put(par);
	}
	
	if (cur) {
		(void) ops->node_put(cur);
	}
	
	if (tmp) {
		(void) ops->node_put(tmp);
	}
}

void libfs_stat(libfs_ops_t *ops, fs_handle_t fs_handle, ipc_callid_t rid,
    ipc_call_t *request)
{
	service_id_t service_id = (service_id_t) IPC_GET_ARG1(*request);
	fs_index_t index = (fs_index_t) IPC_GET_ARG2(*request);
	
	fs_node_t *fn;
	int rc = ops->node_get(&fn, service_id, index);
	on_error(rc, answer_and_return(rid, rc));
	
	ipc_callid_t callid;
	size_t size;
	if ((!async_data_read_receive(&callid, &size)) ||
	    (size != sizeof(struct stat))) {
		ops->node_put(fn);
		async_answer_0(callid, EINVAL);
		async_answer_0(rid, EINVAL);
		return;
	}
	
	struct stat stat;
	memset(&stat, 0, sizeof(struct stat));
	
	stat.fs_handle = fs_handle;
	stat.service_id = service_id;
	stat.index = index;
	stat.lnkcnt = ops->lnkcnt_get(fn);
	stat.is_file = ops->is_file(fn);
	stat.is_directory = ops->is_directory(fn);
	stat.size = ops->size_get(fn);
	stat.service = ops->service_get(fn);
	
	ops->node_put(fn);
	
	async_data_read_finalize(callid, &stat, sizeof(struct stat));
	async_answer_0(rid, EOK);
}

/** Open VFS triplet.
 *
 * @param ops     libfs operations structure with function pointers to
 *                file system implementation
 * @param rid     Request ID of the VFS_OUT_OPEN_NODE request.
 * @param request VFS_OUT_OPEN_NODE request data itself.
 *
 */
void libfs_open_node(libfs_ops_t *ops, fs_handle_t fs_handle, ipc_callid_t rid,
    ipc_call_t *request)
{
	service_id_t service_id = IPC_GET_ARG1(*request);
	fs_index_t index = IPC_GET_ARG2(*request);
	
	fs_node_t *fn;
	int rc = ops->node_get(&fn, service_id, index);
	on_error(rc, answer_and_return(rid, rc));
	
	if (fn == NULL) {
		async_answer_0(rid, ENOENT);
		return;
	}
	
	rc = ops->node_open(fn);
	aoff64_t size = ops->size_get(fn);
	async_answer_4(rid, rc, LOWER32(size), UPPER32(size),
	    ops->lnkcnt_get(fn),
	    (ops->is_file(fn) ? L_FILE : 0) | (ops->is_directory(fn) ? L_DIRECTORY : 0));
	
	(void) ops->node_put(fn);
}

static FIBRIL_MUTEX_INITIALIZE(instances_mutex);
static LIST_INITIALIZE(instances_list);

typedef struct {
	service_id_t service_id;
	link_t link;
	void *data;
} fs_instance_t;

int fs_instance_create(service_id_t service_id, void *data)
{
	fs_instance_t *inst = malloc(sizeof(fs_instance_t));
	if (!inst)
		return ENOMEM;

	link_initialize(&inst->link);
	inst->service_id = service_id;
	inst->data = data;

	fibril_mutex_lock(&instances_mutex);
	list_foreach(instances_list, link) {
		fs_instance_t *cur = list_get_instance(link, fs_instance_t,
		    link);

		if (cur->service_id == service_id) {
			fibril_mutex_unlock(&instances_mutex);
			free(inst);
			return EEXIST;
		}

		/* keep the list sorted */
		if (cur->service_id < service_id) {
			list_insert_before(&inst->link, &cur->link);
			fibril_mutex_unlock(&instances_mutex);
			return EOK;
		}
	}
	list_append(&inst->link, &instances_list);
	fibril_mutex_unlock(&instances_mutex);

	return EOK;
}

int fs_instance_get(service_id_t service_id, void **idp)
{
	fibril_mutex_lock(&instances_mutex);
	list_foreach(instances_list, link) {
		fs_instance_t *inst = list_get_instance(link, fs_instance_t,
		    link);

		if (inst->service_id == service_id) {
			*idp = inst->data;
			fibril_mutex_unlock(&instances_mutex);
			return EOK;
		}
	}
	fibril_mutex_unlock(&instances_mutex);
	return ENOENT;
}

int fs_instance_destroy(service_id_t service_id)
{
	fibril_mutex_lock(&instances_mutex);
	list_foreach(instances_list, link) {
		fs_instance_t *inst = list_get_instance(link, fs_instance_t,
		    link);

		if (inst->service_id == service_id) {
			list_remove(&inst->link);
			fibril_mutex_unlock(&instances_mutex);
			free(inst);
			return EOK;
		}
	}
	fibril_mutex_unlock(&instances_mutex);
	return ENOENT;
}

/** @}
 */
