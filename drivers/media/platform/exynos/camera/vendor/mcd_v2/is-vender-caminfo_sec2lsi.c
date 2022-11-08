/*
 * Samsung Exynos SoC series Sensor driver
 *
 *
 * Copyright (c) 2020 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>

#include "is-config.h"
#include "is-vender-caminfo_sec2lsi.h"
#include "is-vender-specific.h"
#include "is-sec-define.h"
#include "is-device-sensor-peri.h"
#include "is-sysfs.h"

static void *is_caminfo_search_rom_extend_data(const struct rom_extend_cal_addr *extend_data, char *name);
static bool is_need_use_standard_cal(uint32_t camID);

static int is_vender_caminfo_sec2lsi_open(struct inode *inode, struct file *file)
{
	is_vender_caminfo_sec2lsi *p_vender_caminfo;

	p_vender_caminfo = vzalloc(sizeof(is_vender_caminfo_sec2lsi));

	mutex_init(&p_vender_caminfo->mlock);

	file->private_data = p_vender_caminfo;

	return 0;
}

static int is_vender_caminfo_sec2lsi_release(struct inode *inode, struct file *file)
{
	is_vender_caminfo_sec2lsi *p_vender_caminfo = (is_vender_caminfo_sec2lsi *)file->private_data;

	if (p_vender_caminfo)
		vfree(p_vender_caminfo);

	return 0;
}

static void is_vender_caminfo_cmd_get_factory_supported_id(void *user_data)
{
	int i;
	caminfo_supported_list_sec2lsi supported_list;
	struct is_common_cam_info *common_camera_infos;

	is_get_common_cam_info(&common_camera_infos);
	if (!common_camera_infos) {
		err("common_camera_infos is NULL");
		return;
	}
	
	supported_list.size = common_camera_infos->max_supported_camera;

	for (i = 0; i < supported_list.size; i++) {
		supported_list.data[i] = common_camera_infos->supported_camera_ids[i];
	}

	copy_to_user(user_data, (void *)&supported_list, sizeof(caminfo_supported_list_sec2lsi));
}

static void is_vender_caminfo_sec2lsi_cmd_get_module_info(void* user_data)
{
	struct is_core *core = dev_get_drvdata(is_dev);
	struct is_vender_specific *specific = core->vender.private_data;
	const struct is_vender_rom_addr *rom_addr = NULL;
	caminfo_romdata_sec2lsi caminfo;
	struct is_rom_info *finfo;
	char *cal_buf;
	
	copy_from_user((void *)&caminfo, user_data, sizeof(caminfo_romdata_sec2lsi));
	info("%s in for ROM[%d]", __func__, caminfo.camID);

	if (is_need_use_standard_cal(caminfo.camID) == false) {
		info("%s : ROM[%d] does not use standard cal", __func__, caminfo.camID);
		return;
	}

	if (sec2lsi_conversion_done[caminfo.camID] == false) {

		is_sec_get_cal_buf(caminfo.camID, &cal_buf);
		is_sec_get_sysfs_finfo_by_position(caminfo.camID, &finfo);
		rom_addr = specific->rom_cal_map_addr[caminfo.camID];

		if (rom_addr->rom_header_main_module_info_start_addr > 0) {
			copy_to_user(caminfo.mdInfo, &cal_buf[rom_addr->rom_header_main_module_info_start_addr],
				sizeof(uint8_t) * SEC2LSI_MODULE_INFO_SIZE);
		} else {
			info("%s : rom_header_main_module_info_start_addr is invalid for cam %d",
				__func__, caminfo.camID);
		}

		copy_to_user(user_data, &caminfo, sizeof(caminfo_romdata_sec2lsi));
		info("%s : mdinfo size:%u, mdinfo addre:%x", __func__, SEC2LSI_MODULE_INFO_SIZE, caminfo.mdInfo);
	} else {
		info("%s already did for ROM_[%d]", __func__, caminfo.camID);
	}
}

static void is_vender_caminfo_sec2lsi_cmd_get_buff(void* user_data)
{
	struct is_core *core = dev_get_drvdata(is_dev);
	struct is_vender_specific *specific = core->vender.private_data;
	const struct is_vender_rom_addr *rom_addr = NULL;
	struct rom_standard_cal_data *standard_cal_data = NULL;
	caminfo_romdata_sec2lsi caminfo;
	struct is_rom_info *finfo;
	char *cal_buf;

	copy_from_user((void *)&caminfo, user_data, sizeof(caminfo_romdata_sec2lsi));
	info("%s in for ROM[%d]", __func__, caminfo.camID);

	if (is_need_use_standard_cal(caminfo.camID) == false) {
		info("%s : ROM[%d] does not use standard cal", __func__, caminfo.camID);
		return;
	}

	if (sec2lsi_conversion_done[caminfo.camID] == false) {

		is_sec_get_cal_buf(caminfo.camID, &cal_buf);
		is_sec_get_sysfs_finfo_by_position(caminfo.camID, &finfo);
		rom_addr = specific->rom_cal_map_addr[caminfo.camID];

		if (rom_addr->extend_cal_addr) {
			standard_cal_data = (struct rom_standard_cal_data *)is_caminfo_search_rom_extend_data(
				rom_addr->extend_cal_addr, EXTEND_STANDARD_CAL);
			if (standard_cal_data && standard_cal_data->rom_awb_sec2lsi_start_addr >= 0) {
				caminfo.awb_size = standard_cal_data->rom_awb_end_addr
					- standard_cal_data->rom_awb_start_addr + 1;
				caminfo.lsc_size = standard_cal_data->rom_shading_end_addr
					- standard_cal_data->rom_shading_start_addr + 1;
			}
		}

		if (finfo->awb_start_addr > 0) {
			info("%s rom[%d] awb_start_addr is 0x%08X size is %d", __func__,
				caminfo.camID, finfo->awb_start_addr, caminfo.awb_size);
			copy_to_user(caminfo.secBuf, &cal_buf[finfo->awb_start_addr],
				sizeof(uint8_t) * caminfo.awb_size);
		} else {
			info("%s sensor %d does not have awb info", __func__, caminfo.camID);
		}

		if (finfo->shading_start_addr > 0) {
			info("%s rom[%d] shading_start_addr is 0x%08X size is %d", __func__,
				caminfo.camID, finfo->shading_start_addr, caminfo.lsc_size);
			copy_to_user(caminfo.secBuf + caminfo.awb_size, &cal_buf[finfo->shading_start_addr],
				sizeof(uint8_t) * caminfo.lsc_size);
		} else {
			info("%s sensor %d does not have shading info", __func__, caminfo.camID);
		}
		copy_to_user(user_data, &caminfo, sizeof(caminfo_romdata_sec2lsi));
		info("%s : awb size:%u, lsc size:%u, secBuf addre:%x", __func__,
			caminfo.awb_size, caminfo.lsc_size, caminfo.secBuf);
	} else {
		info("%s already did for ROM_[%d]", __func__, caminfo.camID);
	}
}

static void is_vender_caminfo_sec2lsi_cmd_set_buff(void* user_data)
{
	struct is_core *core = dev_get_drvdata(is_dev);
	struct is_vender_specific *specific = core->vender.private_data;
	const struct is_vender_rom_addr *rom_addr = NULL;
	struct rom_standard_cal_data *standard_cal_data = NULL;
	caminfo_romdata_sec2lsi caminfo;

	struct is_rom_info *finfo;
	char *cal_buf;

	u32 awb_length, lsc_length;
	u32 buf_idx, i, tmp;

	awb_length = lsc_length = 0;
	copy_from_user((void *)&caminfo, user_data, sizeof(caminfo_romdata_sec2lsi));
	info("%s in for ROM[%d]", __func__, caminfo.camID);

	if (is_need_use_standard_cal(caminfo.camID) == false) {
		info("%s : ROM[%d] does not use standard cal", __func__, caminfo.camID);
		return;
	}
	
	if (sec2lsi_conversion_done[caminfo.camID] == false) {
#ifdef USES_STANDARD_CAL_RELOAD
		if (!is_sec_sec2lsi_check_cal_reload())
#endif
		{
			sec2lsi_conversion_done[caminfo.camID] = true;
		}
		is_sec_get_cal_buf(caminfo.camID, &cal_buf);
		is_sec_get_sysfs_finfo_by_position(caminfo.camID, &finfo);
		rom_addr = specific->rom_cal_map_addr[caminfo.camID];

		if (rom_addr->extend_cal_addr) {
			standard_cal_data = (struct rom_standard_cal_data *)is_caminfo_search_rom_extend_data(
				rom_addr->extend_cal_addr, EXTEND_STANDARD_CAL);
			if (standard_cal_data && standard_cal_data->rom_awb_sec2lsi_start_addr >= 0) {
				finfo->awb_start_addr = standard_cal_data->rom_awb_sec2lsi_start_addr;
				finfo->awb_end_addr = standard_cal_data->rom_awb_sec2lsi_checksum_addr
					+ (SEC2LSI_CHECKSUM_SIZE - 1);
				finfo->awb_section_crc_addr = standard_cal_data->rom_awb_sec2lsi_checksum_addr;
				awb_length = standard_cal_data->rom_awb_sec2lsi_checksum_len;
			}
			if (standard_cal_data && standard_cal_data->rom_shading_sec2lsi_start_addr >= 0) {
				finfo->shading_start_addr = standard_cal_data->rom_shading_sec2lsi_start_addr;
				finfo->shading_end_addr = standard_cal_data->rom_shading_sec2lsi_checksum_addr
					+ (SEC2LSI_CHECKSUM_SIZE - 1);
				finfo->shading_section_crc_addr = standard_cal_data->rom_shading_sec2lsi_checksum_addr;
				lsc_length = standard_cal_data->rom_shading_sec2lsi_checksum_len;
			}
			
			//Header data changes
			if (rom_addr->rom_header_main_shading_end_addr > 0) {
				buf_idx = rom_addr->rom_header_main_shading_end_addr;
				tmp = finfo->shading_end_addr;
			} else {
				if (standard_cal_data->rom_header_standard_cal_end_addr > 0) {
					buf_idx = standard_cal_data->rom_header_standard_cal_end_addr;
					tmp = standard_cal_data->rom_standard_cal_sec2lsi_end_addr;
				}
			}

			if (buf_idx > 0) {
				for (i = 0; i < 4; i++) {
					cal_buf[buf_idx + i] = tmp & 0xFF;
					tmp = tmp >> 8;
				}
			}	
		}

		if (finfo->awb_start_addr > 0) {
			info("%s  caminfo.awb_size is %d", __func__, caminfo.awb_size);
			copy_from_user(&cal_buf[finfo->awb_start_addr], caminfo.lsiBuf,
				sizeof(uint8_t) * (caminfo.awb_size));
		} else {
			info("%s sensor %d does not have awb info", __func__, caminfo.camID);
		}
		if (finfo->shading_start_addr > 0) {
			info("%s  caminfo.lsc_size is %d", __func__, caminfo.lsc_size);
			copy_from_user(&cal_buf[finfo->shading_start_addr], caminfo.lsiBuf +
				caminfo.awb_size, sizeof(uint8_t) * (caminfo.lsc_size));
		} else {
			info("%s sensor %d does not have shading info", __func__, caminfo.camID);
		}
		info("%s : awb size:%u, lsc size:%u, secBuf addre:%x", __func__, caminfo.awb_size,
			caminfo.lsc_size, caminfo.lsiBuf);

		if (!is_sec_check_awb_lsc_crc32_post_sec2lsi(cal_buf, caminfo.camID, awb_length, lsc_length))
			err("%s CRC check post sec2lsi failed!", __func__);
		else
			is_sec_readcal_dump_post_sec2lsi(core, cal_buf, caminfo.camID);

	} else {
		info("%s already did for ROM_[%d]", __func__, caminfo.camID);
	}
}

static void *is_caminfo_search_rom_extend_data(const struct rom_extend_cal_addr *extend_data, char *name)
{
	void *ret = NULL;

	const struct rom_extend_cal_addr *cur;
	cur = extend_data;

	while (cur != NULL) {
		if (!strcmp(cur->name, name)) {
			if (cur->data != NULL) {
				ret = (void *)cur->data;
			} else {
				warn("[%s] : Found -> %s, but no data \n", __func__, cur->name);
				ret = NULL;
			}
			break;
		}
		cur = cur->next;
	}

	return ret;
}

static bool is_need_use_standard_cal(uint32_t camID)
{
	struct is_core *core = dev_get_drvdata(is_dev);
	struct is_vender_specific *specific = core->vender.private_data;
	const struct is_vender_rom_addr *rom_addr = NULL;
	struct rom_standard_cal_data *standard_cal_data = NULL;

	rom_addr = specific->rom_cal_map_addr[camID];
	if (rom_addr->extend_cal_addr) {
		standard_cal_data = (struct rom_standard_cal_data *)is_caminfo_search_rom_extend_data(
			rom_addr->extend_cal_addr, EXTEND_STANDARD_CAL);
		if (standard_cal_data)
			return true;
	}

	return false;
}

static long is_vender_caminfo_sec2lsi_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	is_vender_caminfo_sec2lsi *p_vender_caminfo;
	caminfo_sec2lsi_ioctl_cmd ioctl_cmd;

	BUG_ON(!file->private_data);

	p_vender_caminfo = (is_vender_caminfo_sec2lsi *)file->private_data;

	mutex_lock(&p_vender_caminfo->mlock);

	copy_from_user((void *)&ioctl_cmd, (void *)arg, sizeof(caminfo_sec2lsi_ioctl_cmd));

	info("%s : cmd number:%u, arg:%x", __func__, ioctl_cmd.cmd, arg);
	switch (ioctl_cmd.cmd) {
	case CAMINFO_IOCTL_GET_SEC2LSI_BUFF:
		is_vender_caminfo_sec2lsi_cmd_get_buff(ioctl_cmd.data);
		break;
	case CAMINFO_IOCTL_SET_SEC2LSI_BUFF:
		is_vender_caminfo_sec2lsi_cmd_set_buff(ioctl_cmd.data);
		break;
	case CAMINFO_CMD_ID_GET_FACTORY_SUPPORTED_ID:
		is_vender_caminfo_cmd_get_factory_supported_id(ioctl_cmd.data);
		break;
	case CAMINFO_IOCTL_GET_MODULE_INFO:
		is_vender_caminfo_sec2lsi_cmd_get_module_info(ioctl_cmd.data);
		break;
	default:
		info("%s : not support cmd number:%u, arg:%x", __func__, ioctl_cmd.cmd, arg);
		break;
	}

	mutex_unlock(&p_vender_caminfo->mlock);

	return 0;
}

static struct file_operations is_vender_caminfo_sec2lsi_fops =
{
	.owner = THIS_MODULE,
	.open = is_vender_caminfo_sec2lsi_open,
	.release = is_vender_caminfo_sec2lsi_release,
	.unlocked_ioctl = is_vender_caminfo_sec2lsi_ioctl,
};

struct miscdevice is_vender_caminfo_sec2lsi_driver =
{
	.minor = MISC_DYNAMIC_MINOR,
	.name = "caminfo_sec2lsi",
	.fops = &is_vender_caminfo_sec2lsi_fops,
};

#ifndef MODULE
static int is_vender_caminfo_sec2lsi_init(void)
{
	info("%s\n", __func__);

	return misc_register(&is_vender_caminfo_sec2lsi_driver);
}

module_init(is_vender_caminfo_sec2lsi_init);

static void is_vender_caminfo_sec2lsi_exit(void)
{
	info("%s\n", __func__);

	misc_deregister(&is_vender_caminfo_sec2lsi_driver);

}

module_exit(is_vender_caminfo_sec2lsi_exit);
#endif

MODULE_DESCRIPTION("Exynos Caminfo Sec2Lsi driver");
MODULE_LICENSE("GPL v2");
