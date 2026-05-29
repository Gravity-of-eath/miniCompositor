/**
 * File:   spine2d.c
 * Author: AWTK Develop Team
 * Brief:  基于spine的骨骼动画。
 *
 * Copyright (c) 2025 - 2025 Guangzhou ZHIYUAN Electronics Co.,Ltd.
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
 * 2025-01-25 Li XianJing <xianjimli@hotmail.com> created
 *
 */


#include "tkc/mem.h"
#include "tkc/utils.h"
#include "spine2d_register.h"
#include "base/widget_factory.h"
#include "spine2d/spine2d.h"
#include "base/opengl.h"

ret_t spine2d_register(void) {
	opengl_init();
  return widget_factory_register(widget_factory(), WIDGET_TYPE_SPINE2D, spine2d_create);
}

const char* spine2d_supported_render_mode(void) {
  return "OpenGL";
}
