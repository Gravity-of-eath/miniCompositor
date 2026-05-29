/**
 * File:   dynamic_listview.c
 * Author: AWTK Develop Team
 * Brief:  表格视图。
 *
 * Copyright (c) 2020 - 2020 Guangzhou ZHIYUAN Electronics Co.,Ltd.
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
 * 2020-07-15 Li XianJing <xianjimli@hotmail.com> created
 *
 */

#include "tkc/mem.h"
#include "tkc/utils.h"
#include "dynamic_listview_register.h"
#include "base/widget_factory.h" 
#include "dynamic_listview/dynamic_listview.h" 
#include "dynamic_listview/list_row.h" 

ret_t dynamic_listview_register(void) { 
  widget_factory_register(widget_factory(), WIDGET_TYPE_LIST_ROW, list_row_create);
widget_factory_register(widget_factory(), WIDGET_TYPE_DYNAMIC_LISTVIEW, dynamic_listview_create);
  return widget_factory_register(widget_factory(), WIDGET_TYPE_DYNAMIC_LISTVIEW, dynamic_listview_create);
}

const char* dynamic_listview_supported_render_mode(void) {
  return "OpenGL|AGGE-BGR565|AGGE-BGRA8888|AGGE-MONO";
}
