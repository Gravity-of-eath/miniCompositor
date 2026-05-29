/**
 * File:   yps_progress_bar.c
 * Author: yps
 * Brief:  yps_progress_bar
 *
 * Copyright (c) 2024 - 2024 
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
 * 2024-7-4 taylor.yao created
 *
 */


#include "tkc/mem.h"
#include "tkc/utils.h"
#include "yps_progress_bar_register.h"
#include "base/widget_factory.h"
#include "yps_progress_bar/yps_progress_bar.h"

ret_t yps_progress_bar_register(void) {
  return widget_factory_register(widget_factory(), WIDGET_TYPE_YPS_PROGRESS_BAR, yps_progress_bar_create);
}

const char* yps_progress_bar_supported_render_mode(void) {
  return "OpenGL|AGGE-BGR565|AGGE-BGRA8888|AGGE-MONO";
}
