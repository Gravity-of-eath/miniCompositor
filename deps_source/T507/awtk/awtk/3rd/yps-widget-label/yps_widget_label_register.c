/**
 * File:   yps_widget_label.c
 * Author: 云片松
 * Brief:  标签控件扩展，支持不同字体
 *
 * Copyright (c) 2023 - 2023 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * License file for more details.
 *
 */

/**
 * History:
 * ================================================================
 * 2023-4-11 wdp created
 *
 */


#include "tkc/mem.h"
#include "tkc/utils.h"
#include "yps_widget_label_register.h"
#include "base/widget_factory.h"
#include "yps_widget_label/yps_widget_label.h"

ret_t yps_widget_label_register(void) {
  return widget_factory_register(widget_factory(), WIDGET_TYPE_YPS_WIDGET_LABEL, yps_widget_label_create);
}

const char* yps_widget_label_supported_render_mode(void) {
  return "OpenGL|AGGE-BGR565|AGGE-BGRA8888|AGGE-MONO";
}
