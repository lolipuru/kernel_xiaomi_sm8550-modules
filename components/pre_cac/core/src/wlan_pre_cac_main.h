/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * DOC: Declare private API which shall be used internally only
 * in pre_cac component. This file shall include prototypes of
 * various notification handlers and logging functions.
 *
 * Note: This API should be never accessed out of pre_cac component.
 */

#ifndef _WLAN_PRE_CAC_MAIN_H_
#define _WLAN_PRE_CAC_MAIN_H_

#include <qdf_types.h>
#include "wlan_objmgr_vdev_obj.h"
#include "wlan_pre_cac_public_struct.h"

#define pre_cac_log(level, args...) \
	QDF_TRACE(QDF_MODULE_ID_WLAN_PRE_CAC, level, ## args)

#define pre_cac_logfl(level, format, args...) \
	pre_cac_log(level, FL(format), ## args)

#define pre_cac_fatal(format, args...) \
		      pre_cac_logfl(QDF_TRACE_LEVEL_FATAL, format, ## args)
#define pre_cac_err(format, args...) \
		    pre_cac_logfl(QDF_TRACE_LEVEL_ERROR, format, ## args)
#define pre_cac_warn(format, args...) \
		     pre_cac_logfl(QDF_TRACE_LEVEL_WARN, format, ## args)
#define pre_cac_info(format, args...) \
		     pre_cac_logfl(QDF_TRACE_LEVEL_INFO, format, ## args)
#define pre_cac_debug(format, args...) \
		      pre_cac_logfl(QDF_TRACE_LEVEL_DEBUG, format, ## args)

#define pre_cac_nofl_err(format, args...) \
	pre_cac_log(QDF_TRACE_LEVEL_ERROR, format, ## args)
#define pre_cac_nofl_warn(format, args...) \
		     pre_cac_log(QDF_TRACE_LEVEL_WARN, format, ## args)
#define pre_cac_nofl_info(format, args...) \
		     pre_cac_log(QDF_TRACE_LEVEL_INFO, format, ## args)
#define pre_cac_nofl_debug(format, args...) \
		      pre_cac_log(QDF_TRACE_LEVEL_DEBUG, format, ## args)

#define PRE_CAC_ENTER() pre_cac_debug("enter")
#define PRE_CAC_EXIT() pre_cac_debug("exit")

/**
 * struct pre_cac_vdev_priv - Private object to be stored in vdev
 * @is_pre_cac_on: status of pre_cac
 */
struct pre_cac_vdev_priv {
	bool is_pre_cac_on;
};

/**
 * struct pre_cac_psoc_priv - Private object to be stored in psoc
 * @pre_cac_work: pre cac work handler
 */
struct pre_cac_psoc_priv {
	qdf_work_t pre_cac_work;
};

/**
 * pre_cac_vdev_create_notification(): Handler for vdev create notify.
 * @vdev: vdev which is going to be created by objmgr
 * @arg: argument for notification handler.
 *
 * Allocate and attach vdev private object.
 *
 * Return: QDF_STATUS status in case of success else return error.
 */
QDF_STATUS pre_cac_vdev_create_notification(struct wlan_objmgr_vdev *vdev,
					    void *arg);

/**
 * pre_cac_vdev_destroy_notification(): Handler for vdev destroy notify.
 * @vdev: vdev which is going to be destroyed by objmgr
 * @arg: argument for notification handler.
 *
 * Deallocate and detach vdev private object.
 *
 * Return QDF_STATUS status in case of success else return error
 */
QDF_STATUS
pre_cac_vdev_destroy_notification(struct wlan_objmgr_vdev *vdev,
				  void *arg);

/**
 * pre_cac_psoc_create_notification() - Handler for psoc create notify.
 * @psoc: psoc which is going to be created by objmgr
 * @arg: argument for notification handler.
 *
 * Allocate and attach psoc private object.
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
pre_cac_psoc_create_notification(struct wlan_objmgr_psoc *psoc, void *arg);

/**
 * pre_cac_psoc_destroy_notification() - Handler for psoc destroy notify.
 * @psoc: psoc which is going to be destroyed by objmgr
 * @arg: argument for notification handler.
 *
 * Deallocate and detach psoc private object.
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
pre_cac_psoc_destroy_notification(struct wlan_objmgr_psoc *psoc, void *arg);

struct pre_cac_vdev_priv *
pre_cac_vdev_get_priv_fl(struct wlan_objmgr_vdev *vdev,
			 const char *func, uint32_t line);

/**
 * pre_cac_vdev_get_priv_fl(): Wrapper to retrieve vdev priv obj
 * @vdev: vdev pointer
 *
 * Wrapper for pre_cac to get vdev private object pointer.
 *
 * Return: Private object of vdev
 */
#define pre_cac_vdev_get_priv(vdev) \
			      pre_cac_vdev_get_priv_fl(vdev, __func__, __LINE__)

struct pre_cac_psoc_priv *
pre_cac_psoc_get_priv_fl(struct wlan_objmgr_psoc *psoc,
			 const char *func, uint32_t line);

/**
 * pre_cac_psoc_get_priv_fl(): Wrapper to retrieve psoc priv obj
 * @psoc: psoc pointer
 *
 * Wrapper for pre_cac to get psoc private object pointer.
 *
 * Return: pre_cac psoc private object
 */
#define pre_cac_psoc_get_priv(vdev) \
			      pre_cac_psoc_get_priv_fl(vdev, __func__, __LINE__)

/**
 * pre_cac_init() - pre cac component initialization.
 *
 * This function initializes the pre cac component and registers
 * the handlers which are invoked on vdev creation.
 *
 * Return: For successful registration - QDF_STATUS_SUCCESS,
 *         else QDF_STATUS error codes.
 */
QDF_STATUS pre_cac_init(void);

/**
 * pre_cac_deinit() - pre cac component deinit.
 *
 * This function deinits pre cac component.
 *
 * Return: None
 */
void pre_cac_deinit(void);

/**
 * pre_cac_set_osif_cb(): set pre cac osif callbacks
 * @osif_pre_cac_ops: pre cac ops
 *
 *
 * Return: None
 */
void pre_cac_set_osif_cb(struct pre_cac_ops *osif_pre_cac_ops);
#endif /* end of _WLAN_PRE_CAC_MAIN_H_ */
