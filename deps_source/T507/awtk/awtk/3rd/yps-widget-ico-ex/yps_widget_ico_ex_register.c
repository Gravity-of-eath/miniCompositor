/**
 * File:   yps_widget_ico_ex.c
 * Author: 云片松
 * Brief:  这是一个图标控件的升级版，支持一次性设置多个图片，提供接口可以通过id进行显示指定图片
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
 * 2023-4-19 wangdongpo created
 *
 */


#include "tkc/mem.h"
#include "tkc/utils.h"
#include "yps_widget_ico_ex_register.h"
#include "base/widget_factory.h"
#include "yps_widget_ico_ex/yps_widget_ico_ex.h"

ret_t yps_widget_ico_ex_register(void) {
  return widget_factory_register(widget_factory(), WIDGET_TYPE_YPS_WIDGET_ICO_EX, yps_widget_ico_ex_create);
}

const char* yps_widget_ico_ex_supported_render_mode(void) {
  return "OpenGL|AGGE-BGR565|AGGE-BGRA8888|AGGE-MONO";
}
