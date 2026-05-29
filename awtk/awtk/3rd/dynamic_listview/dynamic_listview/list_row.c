/**
 * File:   list_row.c
 * Author: AWTK Develop Team
 * Brief:  list_row
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
 * 2020-07-17 Li XianJing <xianjimli@hotmail.com> created
 *
 */

#include "tkc/mem.h"
#include "tkc/utils.h"
#include "list_row.h"
#include "base/widget_vtable.h"

ret_t list_row_set_index(widget_t* widget, uint32_t index) {
  list_row_t* list_row = LIST_ROW(widget);
  return_value_if_fail(list_row != NULL, RET_BAD_PARAMS);
  list_row->index = index;
  return RET_OK;
}


ret_t list_row_set_view_type(widget_t* widget, uint32_t view_type) {
  list_row_t* list_row = LIST_ROW(widget);
  return_value_if_fail(list_row != NULL, RET_BAD_PARAMS);
  list_row->view_type = view_type;
  return RET_OK;
}

uint32_t list_row_get_index(widget_t* widget) {
   list_row_t* list_row = LIST_ROW(widget);
  return_value_if_fail(list_row != NULL, RET_BAD_PARAMS);
  return list_row->index;
}

uint32_t list_row_get_view_type(widget_t* widget) {
   list_row_t* list_row = LIST_ROW(widget);
  return_value_if_fail(list_row != NULL, RET_BAD_PARAMS);
  return list_row->view_type;
}

static ret_t list_row_get_prop(widget_t* widget, const char* name, value_t* v) {
  list_row_t* list_row = LIST_ROW(widget);
  return_value_if_fail(list_row != NULL && name != NULL && v != NULL, RET_BAD_PARAMS);

  if (tk_str_eq(LIST_ROW_PROP_INDEX, name)) {
    value_set_uint32(v, list_row->index);
    return RET_OK;
  }
 if (tk_str_eq(LIST_ROW_PROP_TYPE, name)) {
    value_set_uint32(v, list_row->view_type);
    return RET_OK;
  }
  return RET_NOT_FOUND;
}

static ret_t list_row_set_prop(widget_t* widget, const char* name, const value_t* v) {
  return_value_if_fail(widget != NULL && name != NULL && v != NULL, RET_BAD_PARAMS);

  if (tk_str_eq(LIST_ROW_PROP_INDEX, name)) {
    list_row_set_index(widget, value_uint32(v));
    return RET_OK;
  }
if (tk_str_eq(LIST_ROW_PROP_TYPE, name)) {
    list_row_set_view_type(widget, value_uint32(v));
    return RET_OK;
  }
  return RET_NOT_FOUND;
}

const char* s_list_row_properties[] = {LIST_ROW_PROP_INDEX, LIST_ROW_PROP_TYPE, NULL};

TK_DECL_VTABLE(list_row) = {.size = sizeof(list_row_t),
                             .type = WIDGET_TYPE_LIST_ROW,
                             .clone_properties = s_list_row_properties,
                             .persistent_properties = s_list_row_properties,
                             .parent = TK_PARENT_VTABLE(widget),
                             .create = list_row_create,
                             .set_prop = list_row_set_prop,
                             .get_prop = list_row_get_prop};

widget_t* list_row_create(widget_t* parent, xy_t x, xy_t y, wh_t w, wh_t h) {
  widget_t* widget = widget_create(parent, TK_REF_VTABLE(list_row), x, y, w, h);
  list_row_t* list_row = LIST_ROW(widget);
  return_value_if_fail(list_row != NULL, NULL);

  return widget;
}

widget_t* list_row_cast(widget_t* widget) {
  return_value_if_fail(WIDGET_IS_INSTANCE_OF(widget, list_row), NULL);

  return widget;
}
