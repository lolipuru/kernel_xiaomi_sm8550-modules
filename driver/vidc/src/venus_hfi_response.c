// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#include "hfi_packet.h"
#include "venus_hfi.h"
#include "venus_hfi_response.h"
#include "msm_vidc_debug.h"
#include "msm_vidc_driver.h"
#include "msm_vdec.h"

#define in_range(range, val) (((range.begin) < (val)) && ((range.end) > (val)))

extern struct msm_vidc_core *g_core;
struct msm_vidc_hfi_range {
	u32 begin;
	u32 end;
	int (*handle)(struct msm_vidc_inst *inst, struct hfi_packet *pkt);
};

void print_psc_properties(const char *str, struct msm_vidc_inst *inst,
	struct msm_vidc_subscription_params subsc_params)
{
	if (!inst || !str)
		return;

	i_vpr_h(inst,
		"%s: resolution %#x, crop offsets[0] %#x, crop offsets[1] %#x, bit depth %d, coded frames %d "
		"fw min count %d, poc %d, color info %d, profile %d, level %d, tier %d ",
		str, subsc_params.bitstream_resolution,
		subsc_params.crop_offsets[0], subsc_params.crop_offsets[1],
		subsc_params.bit_depth, subsc_params.coded_frames,
		subsc_params.fw_min_count, subsc_params.pic_order_cnt,
		subsc_params.color_info, subsc_params.profile, subsc_params.level,
		subsc_params.tier);
}

static void print_sfr_message(struct msm_vidc_core *core)
{
	struct msm_vidc_sfr *vsfr = NULL;
	u32 vsfr_size = 0;
	void *p = NULL;

	vsfr = (struct msm_vidc_sfr *)core->sfr.align_virtual_addr;
	if (vsfr) {
		if (vsfr->bufSize != core->sfr.mem_size) {
			d_vpr_e("Invalid SFR buf size %d actual %d\n",
				vsfr->bufSize, core->sfr.mem_size);
			return;
		}
		vsfr_size = vsfr->bufSize - sizeof(u32);
		p = memchr(vsfr->rg_data, '\0', vsfr_size);
		/* SFR isn't guaranteed to be NULL terminated */
		if (p == NULL)
			vsfr->rg_data[vsfr_size - 1] = '\0';

		d_vpr_e("SFR Message from FW: %s\n", vsfr->rg_data);
	}
}

u32 vidc_port_from_hfi(struct msm_vidc_inst *inst,
	enum hfi_packet_port_type hfi_port)
{
	enum msm_vidc_port_type port = MAX_PORT;

	if (is_decode_session(inst)) {
		switch (hfi_port) {
		case HFI_PORT_BITSTREAM:
			port = INPUT_PORT;
			break;
		case HFI_PORT_RAW:
			port = OUTPUT_PORT;
			break;
		default:
			i_vpr_e(inst, "%s: invalid hfi port type %d\n",
				__func__, hfi_port);
			break;
		}
	} else if (is_encode_session(inst)) {
		switch (hfi_port) {
		case HFI_PORT_RAW:
			port = INPUT_PORT;
			break;
		case HFI_PORT_BITSTREAM:
			port = OUTPUT_PORT;
			break;
		default:
			i_vpr_e(inst, "%s: invalid hfi port type %d\n",
				__func__, hfi_port);
			break;
		}
	} else {
		i_vpr_e(inst, "%s: invalid domain %#x\n",
			__func__, inst->domain);
	}

	return port;
}

bool is_valid_hfi_port(struct msm_vidc_inst *inst, u32 port,
	u32 buffer_type, const char *func)
{
	if (!inst) {
		i_vpr_e(inst, "%s: invalid params\n", func);
		return false;
	}

	if (port == HFI_PORT_NONE &&
		buffer_type != HFI_BUFFER_ARP &&
		buffer_type != HFI_BUFFER_PERSIST)
		goto invalid;

	if (port != HFI_PORT_BITSTREAM && port != HFI_PORT_RAW)
		goto invalid;

	return true;

invalid:
	i_vpr_e(inst, "%s: invalid port %#x buffer_type %u\n",
			func, port, buffer_type);
	return false;
}

bool is_valid_hfi_buffer_type(struct msm_vidc_inst *inst,
	u32 buffer_type, const char *func)
{
	if (!inst) {
		i_vpr_e(inst, "%s: invalid params\n", func);
		return false;
	}

	if (buffer_type != HFI_BUFFER_BITSTREAM &&
	    buffer_type != HFI_BUFFER_RAW &&
	    buffer_type != HFI_BUFFER_METADATA &&
	    buffer_type != HFI_BUFFER_BIN &&
	    buffer_type != HFI_BUFFER_ARP &&
	    buffer_type != HFI_BUFFER_COMV &&
	    buffer_type != HFI_BUFFER_NON_COMV &&
	    buffer_type != HFI_BUFFER_LINE &&
	    buffer_type != HFI_BUFFER_DPB &&
	    buffer_type != HFI_BUFFER_PERSIST) {
		i_vpr_e(inst, "%s: invalid buffer type %#x\n",
			func, buffer_type);
		return false;
	}
	return true;
}

static int signal_session_msg_receipt(struct msm_vidc_inst *inst,
	enum signal_session_response cmd)
{
	if (cmd < MAX_SIGNAL)
		complete(&inst->completions[cmd]);
	return 0;
}

int validate_packet(u8 *response_pkt, u8 *core_resp_pkt,
	u32 core_resp_pkt_size, const char *func)
{
	u8 *response_limit;
	u32 response_pkt_size = 0;

	if (!response_pkt || !core_resp_pkt || !core_resp_pkt_size) {
		d_vpr_e("%s: invalid params\n", func);
		return -EINVAL;
	}

	response_limit = core_resp_pkt + core_resp_pkt_size;

	if (response_pkt < core_resp_pkt || response_pkt > response_limit) {
		d_vpr_e("%s: invalid packet address\n", func);
		return -EINVAL;
	}

	response_pkt_size = *(u32 *)response_pkt;
	if (!response_pkt_size) {
		d_vpr_e("%s: response packet size cannot be zero\n", func);
		return -EINVAL;
	}

	if (response_pkt + response_pkt_size > response_limit) {
		d_vpr_e("%s: invalid packet size %d\n",
			func, *(u32 *)response_pkt);
		return -EINVAL;
	}
	return 0;
}

static bool check_last_flag(struct msm_vidc_inst *inst,
	struct hfi_packet *pkt)
{
	struct hfi_buffer *buffer;

	if (!inst || !pkt) {
		d_vpr_e("%s: invalid params %d\n", __func__);
		return false;
	}

	buffer = (struct hfi_buffer *)((u8 *)pkt + sizeof(struct hfi_packet));
	if (buffer->flags & HFI_BUF_FW_FLAG_LAST) {
		i_vpr_h(inst, "%s: received last flag on FBD, index: %d\n",
			__func__, buffer->index);
		return true;
	}
	return false;
}

static int handle_session_info(struct msm_vidc_inst *inst,
	struct hfi_packet *pkt)
{

	int rc = 0;
	char *info;

	switch (pkt->type) {
	case HFI_INFO_UNSUPPORTED:
		info = "unsupported";
		break;
	case HFI_INFO_DATA_CORRUPT:
		info = "data corrupt";
		inst->hfi_frame_info.data_corrupt = 1;
		break;
	default:
		info = "unknown";
		break;
	}

	i_vpr_e(inst, "session info (%#x): %s\n", pkt->type, info);

	return rc;
}

static int handle_session_error(struct msm_vidc_inst *inst,
	struct hfi_packet *pkt)
{
	int rc = 0;
	char *error;

	switch (pkt->type) {
	case HFI_ERROR_MAX_SESSIONS:
		error = "exceeded max sessions";
		break;
	case HFI_ERROR_UNKNOWN_SESSION:
		error = "unknown session id";
		break;
	case HFI_ERROR_INVALID_STATE:
		error = "invalid operation for current state";
		break;
	case HFI_ERROR_INSUFFICIENT_RESOURCES:
		error = "insufficient resources";
		break;
	case HFI_ERROR_BUFFER_NOT_SET:
		error = "internal buffers not set";
		break;
	case HFI_ERROR_FATAL:
		error = "fatal error";
		break;
	default:
		error = "unknown";
		break;
	}

	i_vpr_e(inst, "session error (%#x): %s\n", pkt->type, error);

	rc = msm_vidc_change_inst_state(inst, MSM_VIDC_ERROR, __func__);
	return rc;
}

static int handle_system_error(struct msm_vidc_core *core,
	struct hfi_packet *pkt)
{
	d_vpr_e("%s: system error received\n", __func__);
	print_sfr_message(core);
	msm_vidc_core_deinit(core, true);
	return 0;
}

static int handle_system_init(struct msm_vidc_core *core,
	struct hfi_packet *pkt)
{
	if (pkt->flags & HFI_FW_FLAGS_SYSTEM_ERROR) {
		d_vpr_e("%s: received system error\n", __func__);
		return 0;
	}

	if (pkt->flags & HFI_FW_FLAGS_SUCCESS) {
		d_vpr_h("%s: successful\n", __func__);
		complete(&core->init_done);
	} else {
		d_vpr_h("%s: unhandled. flags=%d\n", __func__, pkt->flags);
	}

	return 0;
}

static int handle_session_open(struct msm_vidc_inst *inst,
	struct hfi_packet *pkt)
{
	if (pkt->flags & HFI_FW_FLAGS_SESSION_ERROR) {
		i_vpr_e(inst, "%s: received session error\n", __func__);
		msm_vidc_change_inst_state(inst, MSM_VIDC_ERROR, __func__);
		return 0;
	}

	if (pkt->flags & HFI_FW_FLAGS_SUCCESS)
		i_vpr_h(inst, "%s: successful\n", __func__);

	return 0;
}

static int handle_session_close(struct msm_vidc_inst *inst,
	struct hfi_packet *pkt)
{
	if (pkt->flags & HFI_FW_FLAGS_SESSION_ERROR) {
		i_vpr_e(inst, "%s: received session error\n", __func__);
		msm_vidc_change_inst_state(inst, MSM_VIDC_ERROR, __func__);
	}

	if (pkt->flags & HFI_FW_FLAGS_SUCCESS)
		i_vpr_h(inst, "%s: successful\n", __func__);

	signal_session_msg_receipt(inst, SIGNAL_CMD_CLOSE);
	return 0;
}

static int handle_session_start(struct msm_vidc_inst *inst,
	struct hfi_packet *pkt)
{
	if (pkt->flags & HFI_FW_FLAGS_SESSION_ERROR) {
		i_vpr_e(inst, "%s: received session error\n", __func__);
		msm_vidc_change_inst_state(inst, MSM_VIDC_ERROR, __func__);
		return 0;
	}

	if (pkt->flags & HFI_FW_FLAGS_SUCCESS)
		i_vpr_h(inst, "%s: successful for port %d\n",
			__func__, pkt->port);
	return 0;
}

static int handle_session_stop(struct msm_vidc_inst *inst,
	struct hfi_packet *pkt)
{
	int signal_type = -1;

	if (pkt->flags & HFI_FW_FLAGS_SESSION_ERROR) {
		i_vpr_e(inst, "%s: received session error\n", __func__);
		msm_vidc_change_inst_state(inst, MSM_VIDC_ERROR, __func__);
	}

	if (pkt->flags & HFI_FW_FLAGS_SUCCESS)
		i_vpr_h(inst, "%s: successful for port %d\n",
			__func__, pkt->port);

	if (is_encode_session(inst)) {
		if (pkt->port == HFI_PORT_RAW) {
			signal_type = SIGNAL_CMD_STOP_INPUT;
		} else if (pkt->port == HFI_PORT_BITSTREAM) {
			signal_type = SIGNAL_CMD_STOP_OUTPUT;
		} else {
			i_vpr_e(inst, "%s: invalid port: %d\n",
				__func__, pkt->port);
			return -EINVAL;
		}
	} else if (is_decode_session(inst)) {
		if (pkt->port == HFI_PORT_RAW) {
			signal_type = SIGNAL_CMD_STOP_OUTPUT;
		} else if (pkt->port == HFI_PORT_BITSTREAM) {
			signal_type = SIGNAL_CMD_STOP_INPUT;
		} else {
			i_vpr_e(inst, "%s: invalid port: %d\n",
				__func__, pkt->port);
			return -EINVAL;
		}
	} else {
		i_vpr_e(inst, "%s: invalid session\n", __func__);
		return -EINVAL;
	}

	if (signal_type != -1)
		signal_session_msg_receipt(inst, signal_type);
	return 0;
}

static int handle_session_drain(struct msm_vidc_inst *inst,
	struct hfi_packet *pkt)
{
	if (pkt->flags & HFI_FW_FLAGS_SESSION_ERROR) {
		i_vpr_e(inst, "%s: received session error\n", __func__);
		msm_vidc_change_inst_state(inst, MSM_VIDC_ERROR, __func__);
		return 0;
	}

	if (pkt->flags & HFI_FW_FLAGS_SUCCESS)
		i_vpr_h(inst, "%s: successful\n", __func__);
	return 0;
}

static int get_driver_buffer_flags(struct msm_vidc_inst *inst, u32 hfi_flags)
{
	u32 driver_flags = 0;

	if (inst->hfi_frame_info.picture_type & HFI_PICTURE_IDR) {
		driver_flags |= MSM_VIDC_BUF_FLAG_KEYFRAME;
	} else if (inst->hfi_frame_info.picture_type & HFI_PICTURE_P) {
		driver_flags |= MSM_VIDC_BUF_FLAG_PFRAME;
	} else if (inst->hfi_frame_info.picture_type & HFI_PICTURE_B) {
		driver_flags |= MSM_VIDC_BUF_FLAG_BFRAME;
	} else if (inst->hfi_frame_info.picture_type & HFI_PICTURE_I) {
		if (inst->codec == MSM_VIDC_VP9)
			driver_flags |= MSM_VIDC_BUF_FLAG_KEYFRAME;
	} else if (inst->hfi_frame_info.picture_type & HFI_PICTURE_CRA) {
		driver_flags |= MSM_VIDC_BUF_FLAG_KEYFRAME;
	} else if (inst->hfi_frame_info.picture_type & HFI_PICTURE_BLA) {
		driver_flags |= MSM_VIDC_BUF_FLAG_KEYFRAME;
	}

	if (inst->hfi_frame_info.data_corrupt)
		driver_flags |= MSM_VIDC_BUF_FLAG_ERROR;

	if (inst->hfi_frame_info.no_output) {
		if (inst->capabilities->cap[META_BUF_TAG].value &&
			!(hfi_flags & HFI_BUF_FW_FLAG_CODEC_CONFIG))
			driver_flags |= MSM_VIDC_BUF_FLAG_ERROR;
	}

	if (hfi_flags & HFI_BUF_FW_FLAG_CODEC_CONFIG)
		driver_flags |= MSM_VIDC_BUF_FLAG_CODECCONFIG;

	if (hfi_flags & HFI_BUF_FW_FLAG_LAST)
		driver_flags |= MSM_VIDC_BUF_FLAG_LAST;

	return driver_flags;
}

static int handle_input_buffer(struct msm_vidc_inst *inst,
	struct hfi_buffer *buffer)
{
	int rc = 0;
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_buffer *buf;
	struct msm_vidc_core *core;
	u32 frame_size, batch_size;
	bool found;

	if (!inst || !buffer || !inst->capabilities || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;
	buffers = msm_vidc_get_buffers(inst, MSM_VIDC_BUF_INPUT, __func__);
	if (!buffers)
		return -EINVAL;

	found = false;
	list_for_each_entry(buf, &buffers->list, list) {
		if (buf->device_addr == buffer->base_address) {
			found = true;
			break;
		}
	}
	if (!found) {
		i_vpr_e(inst, "%s: buffer not found for idx %d addr %#x\n",
			__func__, buffer->index, buffer->base_address);
		return -EINVAL;
	}

	/* attach dequeued flag for, only last frame in the batch */
	if (msm_vidc_is_super_buffer(inst)) {
		frame_size = call_session_op(core, buffer_size, inst, MSM_VIDC_BUF_INPUT);
		batch_size = inst->capabilities->cap[SUPER_FRAME].value;
		if (!frame_size || !batch_size) {
			i_vpr_e(inst, "%s: invalid size: frame %u, batch %u\n",
				__func__, frame_size, batch_size);
			return -EINVAL;
		}
		if (buffer->addr_offset / frame_size < batch_size - 1) {
			i_vpr_h(inst, "%s: superframe last buffer not reached: %u, %u, %u\n",
				__func__, buffer->addr_offset, frame_size, batch_size);
			return 0;
		}
	}
	buf->data_offset = buffer->data_offset;
	buf->data_size = buffer->data_size;
	buf->attr &= ~MSM_VIDC_ATTR_QUEUED;
	buf->attr |= MSM_VIDC_ATTR_DEQUEUED;

	buf->flags = 0;
	buf->flags = get_driver_buffer_flags(inst, buffer->flags);

	print_vidc_buffer(VIDC_HIGH, "high", "dqbuf", inst, buf);
	msm_vidc_debugfs_update(inst, MSM_VIDC_DEBUGFS_EVENT_EBD);

	return rc;
}

static int handle_output_buffer(struct msm_vidc_inst *inst,
	struct hfi_buffer *buffer)
{
	int rc = 0;
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_buffer *buf;
	bool found;

	buffers = msm_vidc_get_buffers(inst, MSM_VIDC_BUF_OUTPUT, __func__);
	if (!buffers)
		return -EINVAL;

	found = false;
	list_for_each_entry(buf, &buffers->list, list) {
		if (buf->device_addr == buffer->base_address) {
			found = true;
			break;
		}
	}
	if (!found) {
		i_vpr_e(inst, "%s: invalid idx %d daddr %#x\n",
			__func__, buffer->index, buffer->base_address);
		return -EINVAL;
	}
	buf->data_offset = buffer->data_offset;
	buf->data_size = buffer->data_size;
	buf->timestamp = buffer->timestamp;

	buf->attr &= ~MSM_VIDC_ATTR_QUEUED;
	buf->attr |= MSM_VIDC_ATTR_DEQUEUED;

	if (is_encode_session(inst)) {
		/* encoder output is not expected to be corrupted */
		if (inst->hfi_frame_info.data_corrupt) {
			i_vpr_e(inst, "%s: encode output is corrupted\n", __func__);
			msm_vidc_change_inst_state(inst, MSM_VIDC_ERROR, __func__);
		}
	}

	/*
	 * reset data size to zero for last flag buffer.
	 * reset RO flag for last flag buffer.
	 */
	if (buffer->flags & HFI_BUF_FW_FLAG_LAST) {
		if (buffer->data_size) {
			i_vpr_e(inst, "%s: reset data size to zero for last flag buffer\n",
				__func__);
			buffer->data_size = 0;
		}
		if (buffer->flags & HFI_BUF_FW_FLAG_READONLY) {
			i_vpr_e(inst, "%s: reset RO flag for last flag buffer\n",
				__func__);
			buffer->flags &= ~HFI_BUF_FW_FLAG_READONLY;
		}
	}

	if (buffer->flags & HFI_BUF_FW_FLAG_READONLY)
		buf->attr |= MSM_VIDC_ATTR_READ_ONLY;
	else
		buf->attr &= ~MSM_VIDC_ATTR_READ_ONLY;


	buf->flags = 0;
	buf->flags = get_driver_buffer_flags(inst, buffer->flags);

	print_vidc_buffer(VIDC_HIGH, "high", "dqbuf", inst, buf);
	msm_vidc_debugfs_update(inst, MSM_VIDC_DEBUGFS_EVENT_FBD);

	return rc;
}

static int handle_input_metadata_buffer(struct msm_vidc_inst *inst,
	struct hfi_buffer *buffer)
{
	int rc = 0;
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_buffer *buf;
	struct msm_vidc_core *core;
	u32 frame_size, batch_size;
	bool found;

	if (!inst || !buffer || !inst->capabilities || !inst->core) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}
	core = inst->core;
	buffers = msm_vidc_get_buffers(inst, MSM_VIDC_BUF_INPUT_META, __func__);
	if (!buffers)
		return -EINVAL;

	found = false;
	list_for_each_entry(buf, &buffers->list, list) {
		if (buf->device_addr == buffer->base_address) {
			found = true;
			break;
		}
	}
	if (!found) {
		i_vpr_e(inst, "%s: invalid idx %d daddr %#x\n",
			__func__, buffer->index, buffer->base_address);
		return -EINVAL;
	}
	/* attach dequeued flag for, only last frame in the batch */
	if (msm_vidc_is_super_buffer(inst)) {
		frame_size = call_session_op(core, buffer_size, inst, MSM_VIDC_BUF_INPUT_META);
		batch_size = inst->capabilities->cap[SUPER_FRAME].value;
		if (!frame_size || !batch_size) {
			i_vpr_e(inst, "%s: invalid size: frame %u, batch %u\n",
				__func__, frame_size, batch_size);
			return -EINVAL;
		}
		if (buffer->addr_offset / frame_size < batch_size - 1) {
			i_vpr_h(inst, "%s: superframe last buffer not reached: %u, %u, %u\n",
				__func__, buffer->addr_offset, frame_size, batch_size);
			return 0;
		}
	}
	buf->data_size = buffer->data_size;
	buf->attr &= ~MSM_VIDC_ATTR_QUEUED;
	buf->attr |= MSM_VIDC_ATTR_DEQUEUED;
	buf->flags = 0;
	if (buffer->flags & HFI_BUF_FW_FLAG_LAST)
		buf->flags |= MSM_VIDC_BUF_FLAG_LAST;

	print_vidc_buffer(VIDC_HIGH, "high", "dqbuf", inst, buf);
	return rc;
}

static int handle_output_metadata_buffer(struct msm_vidc_inst *inst,
	struct hfi_buffer *buffer)
{
	int rc = 0;
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_buffer *buf;
	bool found;

	buffers = msm_vidc_get_buffers(inst, MSM_VIDC_BUF_OUTPUT_META, __func__);
	if (!buffers)
		return -EINVAL;

	found = false;
	list_for_each_entry(buf, &buffers->list, list) {
		if (buf->device_addr == buffer->base_address) {
			found = true;
			break;
		}
	}
	if (!found) {
		i_vpr_e(inst, "%s: invalid idx %d daddr %#x\n",
			__func__, buffer->index, buffer->base_address);
		return -EINVAL;
	}

	buf->data_size = buffer->data_size;
	buf->attr &= ~MSM_VIDC_ATTR_QUEUED;
	buf->attr |= MSM_VIDC_ATTR_DEQUEUED;
	buf->flags = 0;
	if (buffer->flags & HFI_BUF_FW_FLAG_LAST)
		buf->flags |= MSM_VIDC_BUF_FLAG_LAST;

	print_vidc_buffer(VIDC_HIGH, "high", "dqbuf", inst, buf);
	return rc;
}

static int handle_dequeue_buffers(struct msm_vidc_inst* inst)
{
	int rc = 0;
	int i;
	struct msm_vidc_buffers* buffers;
	struct msm_vidc_buffer* buf;
	struct msm_vidc_buffer* dummy;
	enum msm_vidc_buffer_type buffer_type[] = {
		MSM_VIDC_BUF_INPUT_META,
		MSM_VIDC_BUF_INPUT,
		MSM_VIDC_BUF_OUTPUT_META,
		MSM_VIDC_BUF_OUTPUT,
	};

	for (i = 0; i < ARRAY_SIZE(buffer_type); i++) {
		buffers = msm_vidc_get_buffers(inst, buffer_type[i], __func__);
		if (!buffers)
			return -EINVAL;

		list_for_each_entry_safe(buf, dummy, &buffers->list, list) {
			if (buf->attr & MSM_VIDC_ATTR_DEQUEUED) {
				buf->attr &= ~MSM_VIDC_ATTR_DEQUEUED;
				/*
				 * do not send vb2_buffer_done when fw returns
				 * same buffer again
				 */
				if (buf->attr & MSM_VIDC_ATTR_BUFFER_DONE) {
					print_vidc_buffer(VIDC_HIGH, "high",
						"vb2 done already", inst, buf);
				} else {
					buf->attr |= MSM_VIDC_ATTR_BUFFER_DONE;
					msm_vidc_vb2_buffer_done(inst, buf);
				}
				/* do not unmap / delete read only buffer */
				if (!(buf->attr & MSM_VIDC_ATTR_READ_ONLY))
					msm_vidc_put_driver_buf(inst, buf);
			}
		}
	}

	return rc;
}

static int handle_dpb_buffer(struct msm_vidc_inst *inst,
	struct hfi_buffer *buffer)
{
	int rc = 0;
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_buffer *buf;
	bool found;

	buffers = msm_vidc_get_buffers(inst, MSM_VIDC_BUF_DPB, __func__);
	if (!buffers)
		return -EINVAL;

	found = false;
	list_for_each_entry(buf, &buffers->list, list) {
		if (buf->device_addr == buffer->base_address) {
			found = true;
			break;
		}
	}
	if (found) {
		rc = msm_vidc_destroy_internal_buffer(inst, buf);
	} else {
		i_vpr_e(inst, "%s: invalid idx %d daddr %#x\n",
			__func__, buffer->index, buffer->base_address);
		return -EINVAL;
	}
	return rc;
}

static int handle_persist_buffer(struct msm_vidc_inst *inst,
	struct hfi_buffer *buffer)
{
	int rc = 0;
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_buffer *buf;
	bool found;

	buffers = msm_vidc_get_buffers(inst, MSM_VIDC_BUF_PERSIST, __func__);
	if (!buffers)
		return -EINVAL;

	found = false;
	list_for_each_entry(buf, &buffers->list, list) {
		if (buf->device_addr == buffer->base_address) {
			found = true;
			break;
		}
	}
	if (found) {
		rc = msm_vidc_destroy_internal_buffer(inst, buf);
	} else {
		i_vpr_e(inst, "%s: invalid idx %d daddr %#x\n",
			__func__, buffer->index, buffer->base_address);
		return -EINVAL;
	}
	return rc;
}

static int handle_line_buffer(struct msm_vidc_inst *inst,
	struct hfi_buffer *buffer)
{
	int rc = 0;
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_buffer *buf;
	bool found;

	buffers = msm_vidc_get_buffers(inst, MSM_VIDC_BUF_LINE, __func__);
	if (!buffers)
		return -EINVAL;

	found = false;
	list_for_each_entry(buf, &buffers->list, list) {
		if (buf->device_addr == buffer->base_address) {
			found = true;
			break;
		}
	}
	if (found) {
		rc = msm_vidc_destroy_internal_buffer(inst, buf);
	} else {
		i_vpr_e(inst, "%s: invalid idx %d daddr %#x\n",
			__func__, buffer->index, buffer->base_address);
		return -EINVAL;
	}
	return rc;
}

static int handle_non_comv_buffer(struct msm_vidc_inst *inst,
	struct hfi_buffer *buffer)
{
	int rc = 0;
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_buffer *buf;
	bool found;

	buffers = msm_vidc_get_buffers(inst, MSM_VIDC_BUF_NON_COMV, __func__);
	if (!buffers)
		return -EINVAL;

	found = false;
	list_for_each_entry(buf, &buffers->list, list) {
		if (buf->device_addr == buffer->base_address) {
			found = true;
			break;
		}
	}
	if (found) {
		rc = msm_vidc_destroy_internal_buffer(inst, buf);
	} else {
		i_vpr_e(inst, "%s: invalid idx %d daddr %#x\n",
			__func__, buffer->index, buffer->base_address);
		return -EINVAL;
	}
	return rc;
}

static int handle_comv_buffer(struct msm_vidc_inst *inst,
	struct hfi_buffer *buffer)
{
	int rc = 0;
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_buffer *buf;
	bool found;

	buffers = msm_vidc_get_buffers(inst, MSM_VIDC_BUF_COMV, __func__);
	if (!buffers)
		return -EINVAL;

	found = false;
	list_for_each_entry(buf, &buffers->list, list) {
		if (buf->device_addr == buffer->base_address) {
			found = true;
			break;
		}
	}
	if (found) {
		rc = msm_vidc_destroy_internal_buffer(inst, buf);
	} else {
		i_vpr_e(inst, "%s: invalid idx %d daddr %#x\n",
			__func__, buffer->index, buffer->base_address);
		return -EINVAL;
	}
	return rc;
}

static int handle_bin_buffer(struct msm_vidc_inst *inst,
	struct hfi_buffer *buffer)
{
	int rc = 0;
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_buffer *buf;
	bool found;

	buffers = msm_vidc_get_buffers(inst, MSM_VIDC_BUF_BIN, __func__);
	if (!buffers)
		return -EINVAL;

	found = false;
	list_for_each_entry(buf, &buffers->list, list) {
		if (buf->device_addr == buffer->base_address) {
			found = true;
			break;
		}
	}
	if (found) {
		rc = msm_vidc_destroy_internal_buffer(inst, buf);
	} else {
		i_vpr_e(inst, "%s: invalid idx %d daddr %#x\n",
			__func__, buffer->index, buffer->base_address);
		return -EINVAL;
	}
	return rc;
}

static int handle_arp_buffer(struct msm_vidc_inst *inst,
	struct hfi_buffer *buffer)
{
	int rc = 0;
	struct msm_vidc_buffers *buffers;
	struct msm_vidc_buffer *buf;
	bool found;

	buffers = msm_vidc_get_buffers(inst, MSM_VIDC_BUF_ARP, __func__);
	if (!buffers)
		return -EINVAL;

	found = false;
	list_for_each_entry(buf, &buffers->list, list) {
		if (buf->device_addr == buffer->base_address) {
			found = true;
			break;
		}
	}
	if (found) {
		rc = msm_vidc_destroy_internal_buffer(inst, buf);
	} else {
		i_vpr_e(inst, "%s: invalid idx %d daddr %#x\n",
			__func__, buffer->index, buffer->base_address);
		return -EINVAL;
	}
	return rc;
}

static int handle_session_buffer(struct msm_vidc_inst *inst,
	struct hfi_packet *pkt)
{
	int rc = 0;
	struct hfi_buffer *buffer;
	u32 buf_type = 0, port_type = 0;

	if (pkt->flags & HFI_FW_FLAGS_SESSION_ERROR) {
		i_vpr_e(inst, "%s: received session error\n", __func__);
		msm_vidc_change_inst_state(inst, MSM_VIDC_ERROR, __func__);
		return 0;
	}

	if (pkt->payload_info == HFI_PAYLOAD_NONE) {
		i_vpr_h(inst, "%s: received hfi buffer packet without payload\n",
			__func__);
		return 0;
	}

	port_type = pkt->port;

	buffer = (struct hfi_buffer *)((u8 *)pkt + sizeof(struct hfi_packet));
	buf_type = buffer->type;
	if (!is_valid_hfi_buffer_type(inst, buf_type, __func__)) {
		msm_vidc_change_inst_state(inst, MSM_VIDC_ERROR, __func__);
		return 0;
	}

	if (!is_valid_hfi_port(inst, port_type, buf_type, __func__)) {
		msm_vidc_change_inst_state(inst, MSM_VIDC_ERROR, __func__);
		return 0;
	}

	if (is_encode_session(inst)) {
		if (port_type == HFI_PORT_BITSTREAM) {
			if (buf_type == HFI_BUFFER_METADATA)
				rc = handle_output_metadata_buffer(inst, buffer);
			else if (buf_type == HFI_BUFFER_BITSTREAM)
				rc = handle_output_buffer(inst, buffer);
			else if (buf_type == HFI_BUFFER_BIN)
				rc = handle_bin_buffer(inst, buffer);
			else if (buf_type == HFI_BUFFER_COMV)
				rc = handle_comv_buffer(inst, buffer);
			else if (buf_type == HFI_BUFFER_NON_COMV)
				rc = handle_non_comv_buffer(inst, buffer);
			else if (buf_type == HFI_BUFFER_LINE)
				rc = handle_line_buffer(inst, buffer);
			else if (buf_type == HFI_BUFFER_ARP)
				rc = handle_arp_buffer(inst, buffer);
			else if (buf_type == HFI_BUFFER_DPB)
				rc = handle_dpb_buffer(inst, buffer);
			else
				i_vpr_e(inst, "%s: unknown bitstream port buffer type %#x\n",
					__func__, buf_type);
		} else if (port_type == HFI_PORT_RAW) {
			if (buf_type == HFI_BUFFER_METADATA)
				rc = handle_input_metadata_buffer(inst, buffer);
			else if (buf_type == HFI_BUFFER_RAW)
				rc = handle_input_buffer(inst, buffer);
			else
				i_vpr_e(inst, "%s: unknown raw port buffer type %#x\n",
					__func__, buf_type);
		}
	} else if (is_decode_session(inst)) {
		if (port_type == HFI_PORT_BITSTREAM) {
			if (buf_type == HFI_BUFFER_METADATA)
				rc = handle_input_metadata_buffer(inst, buffer);
			else if (buf_type == HFI_BUFFER_BITSTREAM)
				rc = handle_input_buffer(inst, buffer);
			else if (buf_type == HFI_BUFFER_BIN)
				rc = handle_bin_buffer(inst, buffer);
			else if (buf_type == HFI_BUFFER_COMV)
				rc = handle_comv_buffer(inst, buffer);
			else if (buf_type == HFI_BUFFER_NON_COMV)
				rc = handle_non_comv_buffer(inst, buffer);
			else if (buf_type == HFI_BUFFER_LINE)
				rc = handle_line_buffer(inst, buffer);
			else if (buf_type == HFI_BUFFER_PERSIST)
				rc = handle_persist_buffer(inst, buffer);
			else
				i_vpr_e(inst, "%s: unknown bitstream port buffer type %#x\n",
					__func__, buf_type);
		} else if (port_type == HFI_PORT_RAW) {
			if (buf_type == HFI_BUFFER_METADATA)
				rc = handle_output_metadata_buffer(inst, buffer);
			else if (buf_type == HFI_BUFFER_RAW)
				rc = handle_output_buffer(inst, buffer);
			else if (buf_type == HFI_BUFFER_DPB)
				rc = handle_dpb_buffer(inst, buffer);
			else
				i_vpr_e(inst, "%s: unknown raw port buffer type %#x\n",
					__func__, buf_type);
		}
	} else {
		i_vpr_e(inst, "%s: invalid session %d\n",
			__func__, inst->domain);
		return -EINVAL;
	}

	return rc;
}

static int handle_port_settings_change(struct msm_vidc_inst *inst,
	struct hfi_packet *pkt)
{
	int rc = 0;

	i_vpr_h(inst, "%s: Received port settings change, type %d\n",
		__func__, pkt->port);

	if (pkt->port == HFI_PORT_RAW) {
		print_psc_properties("OUTPUT_PSC", inst, inst->subcr_params[OUTPUT_PORT]);
		rc = msm_vdec_output_port_settings_change(inst);
	} else if (pkt->port == HFI_PORT_BITSTREAM) {
		print_psc_properties("INPUT_PSC", inst, inst->subcr_params[INPUT_PORT]);
		rc = msm_vdec_input_port_settings_change(inst);
	} else {
		i_vpr_e(inst, "%s: invalid port type: %#x\n",
			__func__, pkt->port);
		rc = -EINVAL;
	}

	return rc;
}

static int handle_session_subscribe_mode(struct msm_vidc_inst *inst,
	struct hfi_packet *pkt)
{
	if (pkt->flags & HFI_FW_FLAGS_SESSION_ERROR) {
		i_vpr_e(inst, "%s: received session error\n", __func__);
		msm_vidc_change_inst_state(inst, MSM_VIDC_ERROR, __func__);
	}

	if (pkt->flags & HFI_FW_FLAGS_SUCCESS)
		i_vpr_h(inst, "%s: successful\n", __func__);
	return 0;
}

static int handle_session_delivery_mode(struct msm_vidc_inst *inst,
	struct hfi_packet *pkt)
{
	if (pkt->flags & HFI_FW_FLAGS_SESSION_ERROR) {
		i_vpr_e(inst, "%s: received session error\n", __func__);
		msm_vidc_change_inst_state(inst, MSM_VIDC_ERROR, __func__);
	}

	if (pkt->flags & HFI_FW_FLAGS_SUCCESS)
		i_vpr_h(inst, "%s: successful\n", __func__);
	return 0;
}

static int handle_session_resume(struct msm_vidc_inst *inst,
	struct hfi_packet *pkt)
{
	if (pkt->flags & HFI_FW_FLAGS_SESSION_ERROR) {
		i_vpr_e(inst, "%s: received session error\n", __func__);
		msm_vidc_change_inst_state(inst, MSM_VIDC_ERROR, __func__);
	}

	if (pkt->flags & HFI_FW_FLAGS_SUCCESS)
		i_vpr_h(inst, "%s: successful\n", __func__);
	return 0;
}

static int handle_session_command(struct msm_vidc_inst *inst,
	struct hfi_packet *pkt)
{
	switch (pkt->type) {
	case HFI_CMD_OPEN:
		return handle_session_open(inst, pkt);
	case HFI_CMD_CLOSE:
		return handle_session_close(inst, pkt);
	case HFI_CMD_START:
		return handle_session_start(inst, pkt);
	case HFI_CMD_STOP:
		return handle_session_stop(inst, pkt);
	case HFI_CMD_DRAIN:
		return handle_session_drain(inst, pkt);
	case HFI_CMD_BUFFER:
		return handle_session_buffer(inst, pkt);
	case HFI_CMD_SETTINGS_CHANGE:
		return handle_port_settings_change(inst, pkt);
	case HFI_CMD_SUBSCRIBE_MODE:
		return handle_session_subscribe_mode(inst, pkt);
	case HFI_CMD_DELIVERY_MODE:
		return handle_session_delivery_mode(inst, pkt);
	case HFI_CMD_RESUME:
		return handle_session_resume(inst, pkt);
	default:
		i_vpr_e(inst, "%s: Unsupported command type: %#x\n",
			__func__, pkt->type);
		return -EINVAL;
	}
	return 0;
}

static int handle_session_property(struct msm_vidc_inst *inst,
	struct hfi_packet *pkt)
{
	int rc = 0;
	u32 port;
	u32 *payload_ptr;

	i_vpr_h(inst, "%s: property type %#x\n", __func__, pkt->type);

	port = vidc_port_from_hfi(inst, pkt->port);
	if (port >= MAX_PORT) {
		i_vpr_e(inst,
				"%s: invalid port: %d for property %#x\n",
				__func__, pkt->port, pkt->type);
		return -EINVAL;
	}
	payload_ptr = (u32 *)((u8 *)pkt + sizeof(struct hfi_packet));

	switch (pkt->type) {
	case HFI_PROP_BITSTREAM_RESOLUTION:
		inst->subcr_params[port].bitstream_resolution = payload_ptr[0];
		break;
	case HFI_PROP_CROP_OFFSETS:
		inst->subcr_params[port].crop_offsets[0] = payload_ptr[0];
		inst->subcr_params[port].crop_offsets[1] = payload_ptr[1];
		break;
	case HFI_PROP_LUMA_CHROMA_BIT_DEPTH:
		inst->subcr_params[port].bit_depth = payload_ptr[0];
		break;
	case HFI_PROP_CODED_FRAMES:
		inst->subcr_params[port].coded_frames = payload_ptr[0];
		break;
	case HFI_PROP_BUFFER_FW_MIN_OUTPUT_COUNT:
		inst->subcr_params[port].fw_min_count = payload_ptr[0];
		break;
	case HFI_PROP_PIC_ORDER_CNT_TYPE:
		inst->subcr_params[port].pic_order_cnt = payload_ptr[0];
		break;
	case HFI_PROP_SIGNAL_COLOR_INFO:
		inst->subcr_params[port].color_info = payload_ptr[0];
		break;
	case HFI_PROP_PROFILE:
		inst->subcr_params[port].profile = payload_ptr[0];
		break;
	case HFI_PROP_LEVEL:
		inst->subcr_params[port].level = payload_ptr[0];
		break;
	case HFI_PROP_TIER:
		inst->subcr_params[port].tier = payload_ptr[0];
		break;
	case HFI_PROP_PICTURE_TYPE:
		if (is_encode_session(inst) && port == INPUT_PORT) {
			rc = -EINVAL;
			i_vpr_e(inst,
				"%s: invalid port: %d for property %#x\n",
				__func__, pkt->port, pkt->type);
			break;
		}
		inst->hfi_frame_info.picture_type = payload_ptr[0];
		break;
	case HFI_PROP_NO_OUTPUT:
		if (port != INPUT_PORT) {
			rc = -EINVAL;
			i_vpr_e(inst,
				"%s: invalid port: %d for property %#x\n",
				__func__, pkt->port, pkt->type);
			break;
		}
		inst->hfi_frame_info.no_output = 1;
		break;
	default:
		i_vpr_e(inst, "%s: invalid port settings property %#x\n",
			__func__, pkt->type);
		return -EINVAL;
	}

	return rc;
}

static int handle_image_version_property(struct msm_vidc_core *core,
	struct hfi_packet *pkt)
{
	u32 i = 0;
	u8 *str_image_version;
	u32 req_bytes;

	req_bytes = pkt->size - sizeof(*pkt);
	if (req_bytes < VENUS_VERSION_LENGTH - 1) {
		d_vpr_e("%s: bad_pkt: %d\n", __func__, req_bytes);
		return -EINVAL;
	}
	str_image_version = (u8 *)pkt + sizeof(struct hfi_packet);
	/*
	 * The version string returned by firmware includes null
	 * characters at the start and in between. Replace the null
	 * characters with space, to print the version info.
	 */
	for (i = 0; i < VENUS_VERSION_LENGTH - 1; i++) {
		if (str_image_version[i] != '\0')
			core->fw_version[i] = str_image_version[i];
		else
			core->fw_version[i] = ' ';
	}
	core->fw_version[i] = '\0';

	d_vpr_h("%s: F/W version: %s\n", __func__, core->fw_version);
	return 0;
}

static int handle_system_property(struct msm_vidc_core *core,
	struct hfi_packet *pkt)
{
	int rc = 0;

	if (pkt->flags & HFI_FW_FLAGS_SYSTEM_ERROR) {
		d_vpr_e("%s: received system error for property type %#x\n",
			__func__, pkt->type);
		return handle_system_error(core, pkt);
	}

	switch (pkt->type) {
	case HFI_PROP_IMAGE_VERSION:
		rc = handle_image_version_property(core, pkt);
		break;
	default:
		d_vpr_h("%s: property type %#x successful\n",
			__func__, pkt->type);
		break;
	}
	return rc;
}

static int handle_system_response(struct msm_vidc_core *core,
	struct hfi_header *hdr)
{
	int rc = 0;
	struct hfi_packet *packet;
	u8 *pkt;
	int i;

	pkt = (u8 *)((u8 *)hdr + sizeof(struct hfi_header));

	for (i = 0; i < hdr->num_packets; i++) {
		if (validate_packet((u8 *)pkt, core->response_packet,
				core->packet_size, __func__)) {
			rc = -EINVAL;
			goto exit;
		}
		packet = (struct hfi_packet *)pkt;
		if (packet->type == HFI_CMD_INIT) {
			rc = handle_system_init(core, packet);
		} else if (packet->type > HFI_SYSTEM_ERROR_BEGIN &&
			   packet->type < HFI_SYSTEM_ERROR_END) {
			rc = handle_system_error(core, packet);
		} else if (packet->type > HFI_PROP_BEGIN &&
			   packet->type < HFI_PROP_CODEC) {
			rc = handle_system_property(core, packet);
		} else {
			d_vpr_e("%s: Unknown packet type: %#x\n",
			__func__, packet->type);
			rc = -EINVAL;
			goto exit;
		}
		pkt += packet->size;
	}
exit:
	return rc;
}

int handle_session_response_work(struct msm_vidc_inst *inst,
		struct response_work *resp_work)
{
	int rc = 0;
	struct hfi_header *hdr = NULL;
	struct hfi_packet *packet;
	u8 *pkt, *start_pkt;
	u32 hfi_cmd_type = 0;
	int i, j;
	struct msm_vidc_hfi_range be[] = {
		{HFI_SESSION_ERROR_BEGIN, HFI_SESSION_ERROR_END, handle_session_error},
		{HFI_INFORMATION_BEGIN,   HFI_INFORMATION_END,   handle_session_info},
		{HFI_PROP_BEGIN,          HFI_PROP_END,          handle_session_property},
		{HFI_CMD_BEGIN,           HFI_CMD_END,           handle_session_command},
	};

	if (!inst || !resp_work) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	hdr = (struct hfi_header *)resp_work->data;
	if (!hdr) {
		i_vpr_e(inst, "%s: invalid params\n", __func__);
		return -EINVAL;
	}

	hfi_cmd_type = 0;
	pkt = (u8 *)((u8 *)hdr + sizeof(struct hfi_header));
	start_pkt = pkt;

	/* validate all packets */
	for (i = 0; i < hdr->num_packets; i++) {
		packet = (struct hfi_packet * ) pkt;
		if (validate_packet(pkt, resp_work->data,
				resp_work->data_size, __func__)) {
			rc = -EINVAL;
			goto exit;
		}
		pkt += packet->size;
	}

	if (resp_work->type == RESP_WORK_INPUT_PSC)
		msm_vdec_init_input_subcr_params(inst);

	memset(&inst->hfi_frame_info, 0,
		sizeof(struct msm_vidc_hfi_frame_info));
	for (i = 0; i < ARRAY_SIZE(be); i++) {
		pkt = start_pkt;
		for (j = 0; j < hdr->num_packets; j++) {
			packet = (struct hfi_packet * ) pkt;
			if (in_range(be[i], packet->type)) {
				if (hfi_cmd_type == HFI_CMD_SETTINGS_CHANGE) {
					i_vpr_e(inst,
						"%s: invalid packet type %d in port settings change\n",
						__func__, packet->type);
					rc = -EINVAL;
				}
				hfi_cmd_type = packet->type;
				rc = be[i].handle(inst, packet);
				if (rc)
					goto exit;
			}
			pkt += packet->size;
		}
	}

	if (hfi_cmd_type == HFI_CMD_BUFFER) {
		rc = handle_dequeue_buffers(inst);
		if (rc)
			goto exit;
	}

	memset(&inst->hfi_frame_info, 0,
		sizeof(struct msm_vidc_hfi_frame_info));

exit:
	return rc;
}

void handle_session_response_work_handler(struct work_struct *work)
{
	int rc = 0;
	struct msm_vidc_inst *inst;
	struct response_work *resp_work, *dummy = NULL;

	inst = container_of(work, struct msm_vidc_inst, response_work.work);
	inst = get_inst_ref(g_core, inst);
	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return;
	}

	mutex_lock(&inst->lock);
	list_for_each_entry_safe(resp_work, dummy, &inst->response_works, list) {
		switch (resp_work->type) {
		case RESP_WORK_INPUT_PSC:
		{
			enum msm_vidc_allow allow = MSM_VIDC_DISALLOW;

			allow = msm_vidc_allow_input_psc(inst);
			if (allow == MSM_VIDC_DISALLOW) {
				msm_vidc_change_inst_state(inst, MSM_VIDC_ERROR, __func__);
				break;
			} else if (allow == MSM_VIDC_DEFER) {
				/* continue to next entry processing */
				continue;
			} else if (allow == MSM_VIDC_ALLOW) {
				rc = handle_session_response_work(inst, resp_work);
				if (!rc)
					rc = msm_vidc_state_change_input_psc(inst);
				/* either handle input psc or state change failed */
				if (rc)
					msm_vidc_change_inst_state(inst, MSM_VIDC_ERROR, __func__);
			}
			break;
		}
		case RESP_WORK_OUTPUT_PSC:
			rc = handle_session_response_work(inst, resp_work);
			if (rc)
				msm_vidc_change_inst_state(inst, MSM_VIDC_ERROR, __func__);
			break;
		case RESP_WORK_LAST_FLAG:
			rc = handle_session_response_work(inst, resp_work);
			if (rc) {
				msm_vidc_change_inst_state(inst, MSM_VIDC_ERROR, __func__);
				break;
			}
			if (msm_vidc_allow_last_flag(inst)) {
				rc = msm_vidc_state_change_last_flag(inst);
				if (rc)
					msm_vidc_change_inst_state(inst, MSM_VIDC_ERROR, __func__);
			}
			break;
		default:
			i_vpr_e(inst, "%s: invalid response work type %d\n", __func__,
				resp_work->type);
			break;
		}
		list_del(&resp_work->list);
		kfree(resp_work->data);
		kfree(resp_work);
	}
	mutex_unlock(&inst->lock);

	put_inst(inst);
}

static int queue_response_work(struct msm_vidc_inst *inst,
	enum response_work_type type, void *hdr, u32 hdr_size)
{
	struct response_work *work;

	work = kzalloc(sizeof(struct response_work), GFP_KERNEL);
	if (!work)
		return -ENOMEM;
	INIT_LIST_HEAD(&work->list);
	work->type = type;
	work->data_size = hdr_size;
	work->data = kzalloc(hdr_size, GFP_KERNEL);
	if (!work->data)
		return -ENOMEM;
	memcpy(work->data, hdr, hdr_size);
	list_add_tail(&work->list, &inst->response_works);
	queue_delayed_work(inst->response_workq,
			&inst->response_work, msecs_to_jiffies(0));
	return 0;
}

static int handle_session_response(struct msm_vidc_core *core,
	struct hfi_header *hdr)
{
	int rc = 0;
	struct msm_vidc_inst *inst;
	struct hfi_packet *packet;
	u8 *pkt, *start_pkt;
	u32 hfi_cmd_type = 0;
	u32 hfi_port = 0;
	int i, j;
	struct msm_vidc_hfi_range be[] = {
		{HFI_SESSION_ERROR_BEGIN, HFI_SESSION_ERROR_END, handle_session_error},
		{HFI_INFORMATION_BEGIN,   HFI_INFORMATION_END,   handle_session_info},
		{HFI_PROP_BEGIN,          HFI_PROP_END,          handle_session_property},
		{HFI_CMD_BEGIN,           HFI_CMD_END,           handle_session_command},
	};

	inst = get_inst(core, hdr->session_id);
	if (!inst) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&inst->lock);
	hfi_cmd_type = 0;
	hfi_port = 0;
	pkt = (u8 *)((u8 *)hdr + sizeof(struct hfi_header));
	start_pkt = pkt;

	/* validate all packets */
	for (i = 0; i < hdr->num_packets; i++) {
		packet = (struct hfi_packet * ) pkt;
		if (validate_packet(pkt, core->response_packet,
				core->packet_size, __func__)) {
			rc = -EINVAL;
			goto exit;
		}
		pkt += packet->size;
	}

	pkt = start_pkt;
	for (j = 0; j < hdr->num_packets; j++) {
		packet = (struct hfi_packet * ) pkt;
		if (packet->type == HFI_CMD_SETTINGS_CHANGE) {
			if (packet->port == HFI_PORT_BITSTREAM)
				rc = queue_response_work(inst,
					RESP_WORK_INPUT_PSC,
					(void *)hdr, hdr->size);
			else if (packet->port == HFI_PORT_RAW)
				rc = queue_response_work(inst,
					RESP_WORK_OUTPUT_PSC,
					(void *)hdr, hdr->size);
			goto exit;
		} else if (packet->type == HFI_CMD_BUFFER &&
				packet->port == HFI_PORT_RAW &&
				check_last_flag(inst, packet)) {
			rc = queue_response_work(inst,
				RESP_WORK_LAST_FLAG,
				(void *)hdr, hdr->size);
			goto exit;
		}
		pkt += packet->size;
	}

	memset(&inst->hfi_frame_info, 0, sizeof(struct msm_vidc_hfi_frame_info));
	for (i = 0; i < ARRAY_SIZE(be); i++) {
		pkt = start_pkt;
		for (j = 0; j < hdr->num_packets; j++) {
			packet = (struct hfi_packet * ) pkt;
			if (in_range(be[i], packet->type)) {
				hfi_cmd_type = packet->type;
				rc = be[i].handle(inst, packet);
				if (rc)
					goto exit;
			}
			pkt += packet->size;
		}
	}

	if (hfi_cmd_type == HFI_CMD_BUFFER) {
		rc = handle_dequeue_buffers(inst);
		if (rc)
			goto exit;
	}

	memset(&inst->hfi_frame_info, 0, sizeof(struct msm_vidc_hfi_frame_info));

exit:
	mutex_unlock(&inst->lock);
	put_inst(inst);
	return rc;
}

int handle_response(struct msm_vidc_core *core, void *response)
{
	struct hfi_header *hdr;

	if (!core || !response) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	hdr = (struct hfi_header *)response;
	if (validate_packet((u8 *)hdr, core->response_packet,
			core->packet_size, __func__))
		return -EINVAL;

	if (!hdr->session_id)
		return handle_system_response(core, hdr);
	else
		return handle_session_response(core, hdr);

	return 0;
}
