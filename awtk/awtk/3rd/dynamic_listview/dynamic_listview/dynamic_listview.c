/**
 * File:   dynamic_listview.c
 * Author: AWTK Develop Team
 * Brief:  表格视图数据区。
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
#include "dynamic_listview.h"
#include "base/widget_vtable.h"
#include "list_row.h"
#include "widget_animators/widget_animator_scroll.h"
//#include "../includes/tkc/log.h"

#define PAGES_TO_LOAD 3
#define MAX_ROWS 5000 * 10000
static bool_t is_visible = FALSE;
static ret_t dynamic_listview_on_scroll(widget_t* widget);

bool_t dynamic_listview_get_visible() {
  return is_visible;
}
void dynamic_listview_set_visible(bool_t visible) {
  is_visible = visible;
}

static float dynamic_listview_rows_per_page(widget_t* widget) {
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL && dynamic_listview->row_height > 0, 1);

  return widget->h*1.0 / dynamic_listview->row_height * dynamic_listview->columns;
}

/*get the index of the first visible row*/
static int32_t dynamic_listview_get_vstart_index(widget_t* widget) {
  int32_t vstart_index = 0;
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL && dynamic_listview->row_height > 0, 1);
  vstart_index =
      dynamic_listview->yoffset / (int)(dynamic_listview->row_height) * dynamic_listview->columns;

  vstart_index = tk_max(0, vstart_index);

  return vstart_index;
}

ret_t dynamic_listview_set_row_height(widget_t* widget, uint32_t row_height) {
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL, RET_BAD_PARAMS);

  dynamic_listview->row_height = row_height;

  return RET_OK;
}

ret_t dynamic_listview_set_rows(widget_t* widget, uint32_t rows) {
  int32_t yoffset = 0;
  int32_t rows_per_page = 0;
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL, RET_BAD_PARAMS);
  return_value_if_fail(rows <= MAX_ROWS, RET_BAD_PARAMS);
  if(!is_visible) {
    return RET_STOP;
  }
  dynamic_listview->rows = rows;

  if (widget_count_children(widget) < 2) {
    return RET_OK;
  }

  rows_per_page = dynamic_listview_rows_per_page(widget);
  yoffset = dynamic_listview_get_virtual_h(widget);
  if (dynamic_listview->yoffset + dynamic_listview->row_height * rows_per_page / dynamic_listview->columns >= yoffset) {
    yoffset = yoffset - widget->h;
    dynamic_listview_set_yoffset(widget, yoffset);
  } else {
    dynamic_listview_on_scroll(widget);
    widget_dispatch_simple_event(widget, EVT_SCROLL);
    widget_invalidate_force(widget, NULL);
  }

  return RET_OK;
}

ret_t dynamic_listview_set_columns(widget_t* widget, uint32_t columns) {
  int32_t yoffset = 0;
  int32_t rows_per_page = 0;
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL, RET_BAD_PARAMS);
  // return_value_if_fail(columns <= MAX_ROWS, RET_BAD_PARAMS);
  dynamic_listview->columns = columns;

  if (widget_count_children(widget) < 2) {
    return RET_OK;
  }

  rows_per_page = dynamic_listview_rows_per_page(widget);
  yoffset = dynamic_listview_get_virtual_h(widget);
  if (dynamic_listview->yoffset +
          dynamic_listview->row_height * rows_per_page / dynamic_listview->columns >=
      yoffset) {
    yoffset = yoffset - widget->h;
    dynamic_listview_set_yoffset(widget, yoffset);
  } else {
    dynamic_listview_on_scroll(widget);
    widget_dispatch_simple_event(widget, EVT_SCROLL);
    widget_invalidate_force(widget, NULL);
  }

  return RET_OK;
}

ret_t dynamic_listview_set_yoffset(widget_t* widget, int32_t yoffset) {
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL, RET_BAD_PARAMS);

  if (dynamic_listview->yoffset != yoffset) {
    dynamic_listview->yoffset = yoffset;
    dynamic_listview_on_scroll(widget);
    widget_dispatch_simple_event(widget, EVT_SCROLL);
    widget_invalidate_force(widget, NULL);
  }

  return RET_OK;
}

ret_t dynamic_listview_add_yoffset(widget_t* widget, int32_t delta) {
  int yoffset = 0;
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL, RET_BAD_PARAMS);

  yoffset = dynamic_listview->yoffset + delta;

  return dynamic_listview_set_yoffset(widget, yoffset);
}

ret_t dynamic_listview_set_yslidable(widget_t* widget, bool_t yslidable) {
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL, RET_BAD_PARAMS);

  dynamic_listview->yslidable = yslidable;

  return RET_OK;
}

ret_t dynamic_listview_set_yspeed_scale(widget_t* widget, float_t yspeed_scale) {
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL, RET_BAD_PARAMS);

  dynamic_listview->yspeed_scale = yspeed_scale;

  return RET_OK;
}

static ret_t dynamic_listview_get_prop(widget_t* widget, const char* name, value_t* v) {
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL && name != NULL && v != NULL, RET_BAD_PARAMS);

  if (tk_str_eq(DYNAMIC_LISTVIEW_PROP_ROW_HEIGHT, name)) {
    value_set_uint32(v, dynamic_listview->row_height);
    return RET_OK;
  } else if (tk_str_eq(DYNAMIC_LISTVIEW_PROP_ROWS, name)) {
    value_set_uint32(v, dynamic_listview->rows);
    return RET_OK;
  } else if (tk_str_eq(DYNAMIC_LISTVIEW_PROP_COLUMNS, name)) {
    value_set_uint32(v, dynamic_listview->columns);
    return RET_OK;
  } else if (tk_str_eq(DYNAMIC_LISTVIEW_PROP_YOFFSET, name)) {
    value_set_int32(v, dynamic_listview->yoffset);
    return RET_OK;
  } else if (tk_str_eq(WIDGET_PROP_VIRTUAL_H, name)) {
    value_set_int64(v, dynamic_listview_get_virtual_h(widget));
    return RET_OK;
  } else if (tk_str_eq(DYNAMIC_LISTVIEW_PROP_YSLIDABLE, name)) {
    value_set_bool(v, dynamic_listview->yslidable);
    return RET_OK;
  } else if (tk_str_eq(DYNAMIC_LISTVIEW_PROP_YSPEED_SCALE, name)) {
    value_set_float(v, dynamic_listview->yspeed_scale);
    return RET_OK;
  }

  return RET_NOT_FOUND;
}

static ret_t dynamic_listview_set_prop(widget_t* widget, const char* name, const value_t* v) {
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL && name != NULL && v != NULL, RET_BAD_PARAMS);

  if (tk_str_eq(DYNAMIC_LISTVIEW_PROP_ROW_HEIGHT, name)) {
    dynamic_listview_set_row_height(widget, value_uint32(v));
    return RET_OK;
  } else if (tk_str_eq(DYNAMIC_LISTVIEW_PROP_ROWS, name)) {
    dynamic_listview_set_rows(widget, value_uint32(v));
    return RET_OK;
  } else if (tk_str_eq(DYNAMIC_LISTVIEW_PROP_COLUMNS, name)) {
    dynamic_listview_set_columns(widget, value_uint32(v));
    return RET_OK;
  } else if (tk_str_eq(DYNAMIC_LISTVIEW_PROP_YOFFSET, name)) {
    dynamic_listview_set_yoffset(widget, value_int32(v));
    return RET_OK;
  } else if (tk_str_eq(DYNAMIC_LISTVIEW_PROP_YSLIDABLE, name)) {
    dynamic_listview_set_yslidable(widget, value_bool(v));
    return RET_OK;
  } else if (tk_str_eq(DYNAMIC_LISTVIEW_PROP_YSPEED_SCALE, name)) {
    dynamic_listview_set_yspeed_scale(widget, value_float(v));
    return RET_OK;
  }

  return RET_NOT_FOUND;
}

static ret_t dynamic_listview_on_destroy(widget_t* widget) {
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(widget != NULL && dynamic_listview != NULL, RET_BAD_PARAMS);

  return RET_OK;
}

static ret_t dynamic_listview_on_paint_self(widget_t* widget, canvas_t* c) {
  return RET_OK;
}

static ret_t dynamic_listview_prepare_data(widget_t* widget) {
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  if(dynamic_listview == NULL || !is_visible) {
    return RET_STOP;
  }

  int32_t i = 0;
  int32_t nr = 0;
  int32_t row_height = dynamic_listview->row_height;
  int32_t start_index = dynamic_listview->start_index;
  int32_t rows_per_page = dynamic_listview_rows_per_page(widget);
  int32_t max_rows_to_load = PAGES_TO_LOAD * rows_per_page;
  return_value_if_fail(widget->children != NULL, RET_BAD_PARAMS);

  if ((start_index + max_rows_to_load) >= dynamic_listview->rows) {
    nr = dynamic_listview->rows - start_index;
  } else {
    nr = max_rows_to_load;
  }

  nr = tk_min(nr, dynamic_listview->rows);
  nr = tk_min(nr, widget_count_children(widget));
  max_rows_to_load = tk_min(max_rows_to_load, widget_count_children(widget));
  // log_warn("dynamic_listview_prepare_data nr= %d",nr);
  for (i = 0; i < nr; i++) {
      if(dynamic_listview == NULL || !is_visible) {
        return RET_STOP;
      }
    uint32_t index = start_index + i;
    widget_t* iter = widget_get_child(widget, i);
    event_t e = event_init(EVT_RESET, iter);

    uint32_t indexof_row = index % dynamic_listview->columns;
    uint32_t layout_row = index / dynamic_listview->columns;

    iter->x = indexof_row * dynamic_listview->width / dynamic_listview->columns;
    iter->y = layout_row * row_height;
    list_row_set_index(iter, index);
    widget_set_enable(iter, TRUE);
    widget_set_visible(iter, TRUE, FALSE);
    widget_dispatch_recursive(iter, &e);

    if (dynamic_listview->on_bind_view_holder != NULL &&
        dynamic_listview->on_get_item_view_type != NULL) {
      list_row_set_view_type(iter, dynamic_listview->on_get_item_view_type(
                                       dynamic_listview->on_get_item_view_type_ctx, index));

      dynamic_listview->on_bind_view_holder(dynamic_listview->on_bind_view_holder_ctx,
                                            dynamic_listview->on_get_item_view_type(
                                                dynamic_listview->on_get_item_view_type_ctx, index),
                                            index, iter);
    }
  }

  for (; i < max_rows_to_load; i++) {
    widget_t* iter = widget_get_child(widget, i);
    widget_set_visible(iter, FALSE, FALSE);
    widget_set_enable(iter, FALSE);
  }

  return RET_OK;
}

ret_t dynamic_listview_reload(widget_t* widget) {
  return dynamic_listview_prepare_data(widget);
}

static ret_t dynamic_listview_on_scroll(widget_t* widget) {
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  int32_t vstart_index = dynamic_listview_get_vstart_index(widget);
  int32_t rows_per_page = dynamic_listview_rows_per_page(widget);
  int32_t start_index = dynamic_listview->start_index;

  start_index = vstart_index - rows_per_page;
  dynamic_listview->start_index = tk_max(0, start_index);
  dynamic_listview_prepare_data(widget);

  return RET_OK;
}

ret_t dynamic_listview_ensure_children(widget_t* widget) {
  xy_t iw = 0;
  xy_t ih = 0;
  uint32_t i = 0;
  uint32_t nr = 0;
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  int32_t rows_per_page = dynamic_listview_rows_per_page(widget);
  return_value_if_fail(dynamic_listview->row_height > 0, RET_BAD_PARAMS);

  iw = widget->w;
  ih = dynamic_listview->row_height;
  nr = PAGES_TO_LOAD * rows_per_page;

  if (dynamic_listview->on_prepare_row != NULL) {
    dynamic_listview->on_prepare_row(dynamic_listview->on_prepare_row_ctx, widget, nr);
  } else {
    widget_t* twidget = widget_get_child(widget, 0);
    return_value_if_fail(twidget != NULL, RET_BAD_PARAMS);

    dynamic_listview->row_height = twidget->h;
    if (nr <= widget_count_children(widget)) {
      return RET_OK;
    }

    for (i = 0; i < nr - 1; i++) {
      ENSURE(widget_clone(twidget, widget) != NULL);
    }
  }

  nr = widget_count_children(widget);
  for (i = 0; i < nr; i++) {
    widget_t* iter = widget_get_child(widget, i);
    uint32_t indexof_row = i % dynamic_listview->columns;
    uint32_t layout_row = i / dynamic_listview->columns;
    widget_move_resize(iter, indexof_row * dynamic_listview->width / dynamic_listview->columns,
                       ih * layout_row, iw / dynamic_listview->columns, ih);
    widget_layout(iter);
    // log_warn(
    //     "  widget_count_children ========= i=%d indexof_row =%d   layout_row=%d    ih * "
    //     "layout_row=%d  iter.x=%d  iter.y=%d\n",
    //     i, indexof_row, layout_row, ih * layout_row, iter->x, iter->y);

    if (dynamic_listview != NULL && dynamic_listview->on_create_view_holder != NULL &&
        dynamic_listview->on_get_item_view_type != NULL) {
      dynamic_listview->on_create_view_holder(
          dynamic_listview->on_create_view_holder_ctx, i,
          dynamic_listview->on_get_item_view_type(dynamic_listview->on_get_item_view_type_ctx, i),
          iter);
    }
  }

  dynamic_listview_prepare_data(widget);
  widget_dispatch_simple_event(widget, EVT_SCROLL);

  return RET_OK;
}

static ret_t dynamic_listview_on_pointer_down(dynamic_listview_t* dynamic_listview,
                                              pointer_event_t* e) {
  velocity_t* v = &(dynamic_listview->velocity);

  velocity_reset(v);
  dynamic_listview->down.x = e->x;
  dynamic_listview->down.y = e->y;
  dynamic_listview->yoffset_save = dynamic_listview->yoffset;
  dynamic_listview->yoffset_end = dynamic_listview->yoffset;

  velocity_update(v, e->e.time, e->x, e->y);

  return RET_OK;
}

static ret_t dynamic_listview_on_scroll_done(void* ctx, event_t* e) {
  widget_t* widget = WIDGET(ctx);
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(ctx);
  return_value_if_fail(widget != NULL && dynamic_listview != NULL, RET_BAD_PARAMS);

  dynamic_listview->wa = NULL;
  widget_invalidate_force(widget, NULL);
  widget_dispatch_simple_event(widget, EVT_SCROLL_END);

  return RET_REMOVE;
}

ret_t dynamic_listview_scroll_to(widget_t* widget, int32_t yoffset_end, int32_t duration) {
  int32_t yoffset = 0;
  int32_t max_yoffset = 0;
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  float rows_per_page = dynamic_listview_rows_per_page(widget);
  return_value_if_fail(dynamic_listview != NULL, RET_FAIL);
  //计算出要加上几个item，计算列表内容的高度时，要用columns的倍数来算，否则高度会偏小，最后一行可能显示不全
  int64_t surplus = dynamic_listview->rows % dynamic_listview->columns;
  if(surplus > 0) {
    surplus = dynamic_listview->columns - surplus;
  }
  max_yoffset = (int)(dynamic_listview->row_height * (dynamic_listview->rows + surplus - rows_per_page) / dynamic_listview->columns);
  // log_warn("dynamic_listview    yoffset_end:%d max_yoffset:%d row_height:%d rows:%d surplus:%d rows_per_page:%d columns:%d\n",yoffset_end, max_yoffset,dynamic_listview->row_height,dynamic_listview->rows,surplus,rows_per_page,dynamic_listview->columns);
  if (yoffset_end > max_yoffset) {
    yoffset_end = max_yoffset;
  }

  if (yoffset_end < 0) {
    yoffset_end = 0;
  }

  if (yoffset_end == dynamic_listview->yoffset) {
    dynamic_listview_prepare_data(widget);
    return RET_OK;
  }

  yoffset = dynamic_listview->yoffset;
  if (dynamic_listview->wa != NULL) {
    return RET_OK;
  } else {
    dynamic_listview->wa =
        widget_animator_scroll_create(widget, TK_ANIMATING_TIME, 0, EASING_SIN_INOUT);
    return_value_if_fail(dynamic_listview->wa != NULL, RET_OOM);

    widget_animator_scroll_set_params(dynamic_listview->wa, 0, yoffset, 0, yoffset_end);
    widget_animator_on(dynamic_listview->wa, EVT_ANIM_END, dynamic_listview_on_scroll_done,
                       dynamic_listview);
    widget_animator_start(dynamic_listview->wa);
    widget_dispatch_simple_event(widget, EVT_SCROLL_START);
  }

  return RET_OK;
}

ret_t dynamic_listview_scroll_to_row(widget_t* widget, int32_t row, int32_t duration) {
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL, RET_FAIL);
  if(!is_visible) {
    return RET_STOP;
  }
  return dynamic_listview_scroll_to(widget, dynamic_listview->row_height * row, duration);
}

ret_t dynamic_listview_scroll_to_item(widget_t* widget, int32_t item) {
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL, RET_FAIL);
  if(!is_visible) {
    return RET_STOP;
  }
  return dynamic_listview_scroll_to(
      widget, dynamic_listview->row_height * (item / dynamic_listview->columns), TK_ANIMATING_TIME);
  return RET_OK;
}

ret_t dynamic_listview_scroll_to_yoffset(widget_t* widget, int32_t yoffset) {
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL, RET_FAIL);

  return dynamic_listview_scroll_to(widget, yoffset, TK_ANIMATING_TIME);
}

ret_t dynamic_listview_scroll_delta_to(widget_t* widget, int32_t yoffset_delta, int32_t duration) {
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL, RET_FAIL);

  dynamic_listview->yoffset_end = dynamic_listview->yoffset + yoffset_delta;

  return dynamic_listview_scroll_to(widget, dynamic_listview->yoffset_end, duration);
}

ret_t dynamic_listview_scroll_to_next_page(widget_t* widget) {
  dynamic_listview_scroll_delta_to(widget, widget->h, TK_ANIMATING_TIME);
  return RET_OK;
}

ret_t dynamic_listview_scroll_to_per_page(widget_t* widget) {
  dynamic_listview_scroll_delta_to(widget, -widget->h, TK_ANIMATING_TIME);
  return RET_OK;
}

static ret_t dynamic_listview_on_pointer_move(dynamic_listview_t* dynamic_listview,
                                              pointer_event_t* e) {
  widget_t* widget = WIDGET(dynamic_listview);
  velocity_t* v = &(dynamic_listview->velocity);
  int32_t dy = e->y - dynamic_listview->down.y;
  velocity_update(v, e->e.time, e->x, e->y);

  if (dynamic_listview->wa == NULL) {
    if (dynamic_listview->yslidable && dy) {
      int32_t yoffset = dynamic_listview->yoffset_save - dy;
      dynamic_listview_set_yoffset(widget, yoffset);
    }
  }

  dynamic_listview->first_move_after_down = FALSE;

  return RET_OK;
}

static ret_t dynamic_listview_on_pointer_up(dynamic_listview_t* dynamic_listview,
                                            pointer_event_t* e) {
  widget_t* widget = WIDGET(dynamic_listview);
  velocity_t* v = &(dynamic_listview->velocity);
  int32_t move_dy = e->y - dynamic_listview->down.y;

  velocity_update(v, e->e.time, e->x, e->y);
  if (dynamic_listview->yslidable) {
    int yv = v->yv;

    if (dynamic_listview->wa != NULL) {
      widget_animator_scroll_t* wa = (widget_animator_scroll_t*)dynamic_listview->wa;
      yv = -(wa->y_to - dynamic_listview->yoffset);
    }

    if (dynamic_listview->yslidable) {
      if (tk_abs(move_dy) > TK_CLICK_TOLERANCE) {
        dynamic_listview->yoffset_end =
            dynamic_listview->yoffset - yv * dynamic_listview->yspeed_scale;
      } else {
        dynamic_listview->yoffset_end =
            dynamic_listview->yoffset - yv / dynamic_listview->yspeed_scale;
      }
    }

    dynamic_listview_scroll_to(widget, dynamic_listview->yoffset_end, TK_ANIMATING_TIME);
  }

  return RET_OK;
}

static ret_t dynamic_listview_on_pointer_down_abort(dynamic_listview_t* dynamic_listview,
                                                    pointer_event_t* e) {
  widget_t* widget = WIDGET(dynamic_listview);

  if (dynamic_listview->yslidable) {
    dynamic_listview_scroll_to(widget, dynamic_listview->yoffset_end, TK_ANIMATING_TIME);
  }

  return RET_OK;
}

static bool_t dynamic_listview_is_dragged(widget_t* widget, pointer_event_t* evt) {
  int32_t delta = 0;
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  if (dynamic_listview->yslidable) {
    delta = evt->y - dynamic_listview->down.y;
  }

  return (tk_abs(delta) >= TK_DRAG_THRESHOLD);
}

static ret_t dynamic_listview_on_event(widget_t* widget, event_t* e) {
  ret_t ret = RET_OK;
  int32_t row_height = 0;
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(widget != NULL && dynamic_listview != NULL, RET_BAD_PARAMS);

  row_height = dynamic_listview->row_height;
  switch (e->type) {
    case EVT_WINDOW_OPEN: {
      dynamic_listview_ensure_children(widget);
      return RET_OK;
    }
    case EVT_WHEEL: {
      wheel_event_t* evt = (wheel_event_t*)e;
      int32_t delta = evt->dy > 0 ? row_height : -row_height;
      dynamic_listview_scroll_delta_to(widget, delta, TK_ANIMATING_TIME);
      ret = RET_STOP;
      break;
    }
    case EVT_KEY_DOWN: {
      key_event_t* evt = (key_event_t*)e;
      if (evt->key == TK_KEY_PAGEDOWN) {
        dynamic_listview_scroll_delta_to(widget, widget->h, TK_ANIMATING_TIME);
        ret = RET_STOP;
      } else if (evt->key == TK_KEY_PAGEUP) {
        dynamic_listview_scroll_delta_to(widget, -widget->h, TK_ANIMATING_TIME);
        ret = RET_STOP;
      } else if (evt->key == TK_KEY_UP) {
        dynamic_listview_scroll_delta_to(widget, -row_height, TK_ANIMATING_TIME);
        ret = RET_STOP;
      } else if (evt->key == TK_KEY_DOWN) {
        dynamic_listview_scroll_delta_to(widget, row_height, TK_ANIMATING_TIME);
        ret = RET_STOP;
      }
      break;
    }
    case EVT_POINTER_DOWN: {
      dynamic_listview->pressed = TRUE;
      dynamic_listview->dragged = FALSE;
      widget_grab(widget->parent, widget);
      dynamic_listview->first_move_after_down = TRUE;
      dynamic_listview_on_pointer_down(dynamic_listview, (pointer_event_t*)e);
      break;
    }
    case EVT_POINTER_DOWN_ABORT: {
      pointer_event_t evt = *(pointer_event_t*)e;
      widget_t* target = widget_find_target(widget, evt.x, evt.y);
      if (target == NULL || target->parent != widget) {
        dynamic_listview_on_pointer_down_abort(dynamic_listview, (pointer_event_t*)e);
      }
      if (dynamic_listview->pressed) {
        widget_ungrab(widget->parent, widget);
      }
      dynamic_listview->pressed = FALSE;
      dynamic_listview->dragged = FALSE;
      break;
    }
    case EVT_POINTER_UP: {
      pointer_event_t* evt = (pointer_event_t*)e;
      if (dynamic_listview->pressed && dynamic_listview_is_dragged(widget, evt)) {
        dynamic_listview_on_pointer_up(dynamic_listview, (pointer_event_t*)e);
      }
      dynamic_listview->pressed = FALSE;
      dynamic_listview->dragged = FALSE;
      widget_ungrab(widget->parent, widget);
      break;
    }
    case EVT_POINTER_MOVE: {
      pointer_event_t* evt = (pointer_event_t*)e;
      if (!dynamic_listview->pressed || !dynamic_listview->yslidable) {
        break;
      }

      if (dynamic_listview->dragged) {
        dynamic_listview_on_pointer_move(dynamic_listview, evt);
        widget_invalidate_force(widget, NULL);
      } else {
        if (dynamic_listview_is_dragged(widget, evt)) {
          pointer_event_t abort = *evt;

          abort.e.type = EVT_POINTER_DOWN_ABORT;
          widget_dispatch_event_to_target_recursive(widget, (event_t*)(&abort));

          dynamic_listview->dragged = TRUE;
        }
      }

      ret = dynamic_listview->dragged ? RET_STOP : RET_OK;
      break;
    }
    case EVT_RESIZE:
    case EVT_MOVE_RESIZE: {
      if (widget_is_window_opened(widget)) {
        dynamic_listview_ensure_children(widget);
      }
      break;
    }
    default:
      break;
  }

  return ret;
}

const char* s_dynamic_listview_properties[] = {DYNAMIC_LISTVIEW_PROP_ROW_HEIGHT,
                                               DYNAMIC_LISTVIEW_PROP_ROWS,
                                               DYNAMIC_LISTVIEW_PROP_COLUMNS,
                                               DYNAMIC_LISTVIEW_PROP_YOFFSET,
                                               DYNAMIC_LISTVIEW_PROP_YSLIDABLE,
                                               DYNAMIC_LISTVIEW_PROP_YSPEED_SCALE,
                                               NULL};

static ret_t dynamic_listview_paint_children(widget_t* widget, canvas_t* c) {
  int32_t i = 0;
  int32_t nr = 0;
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  int32_t start_index = dynamic_listview->start_index;
  int32_t vstart_index = dynamic_listview_get_vstart_index(widget);
  int32_t rows_per_page = dynamic_listview_rows_per_page(widget);
  return_value_if_fail(widget->children != NULL, RET_BAD_PARAMS);

  if ((vstart_index + rows_per_page) >= dynamic_listview->rows) {
    nr = dynamic_listview->rows - vstart_index;
  } else {
    nr = rows_per_page + dynamic_listview->columns;
  }
  // log_warn("dynamic_listview_paint_children nr=%d", nr);

  for (i = 0; i < nr; i++) {
    uint32_t index = vstart_index - start_index + i;
    widget_t* iter = widget_get_child(widget, index);
    widget_paint(iter, c);
  }

  return RET_OK;
}

static ret_t dynamic_listview_on_paint_children(widget_t* widget, canvas_t* c) {
  rect_t r_save;
  vgcanvas_t* vg = canvas_get_vgcanvas(c);
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  rect_t r = rect_init(c->ox, c->oy, widget->w, widget->h);
  int32_t yoffset = -dynamic_listview->yoffset;

  canvas_translate(c, 0, yoffset);
  canvas_get_clip_rect(c, &r_save);

  r = rect_intersect(&r, &r_save);

  if (vg != NULL) {
    vgcanvas_save(vg);
    vgcanvas_clip_rect(vg, (float_t)r.x, (float_t)r.y, (float_t)r.w, (float_t)r.h);
  }

  canvas_set_clip_rect(c, &r);
  dynamic_listview_paint_children(widget, c);
  canvas_set_clip_rect(c, &r_save);
  canvas_untranslate(c, 0, yoffset);

  if (vg != NULL) {
    vgcanvas_clip_rect(vg, (float_t)r_save.x, (float_t)r_save.y, (float_t)r_save.w,
                       (float_t)r_save.h);
    vgcanvas_restore(vg);
  }
  return RET_OK;
}

static widget_t* dynamic_listview_find_target(widget_t* widget, xy_t x, xy_t y) {
  return widget_find_target_default(widget, x, y);
}

static ret_t dynamic_listview_invalidate(widget_t* widget, const rect_t* r) {
  return widget_invalidate_default(widget, r);
}

ret_t dynamic_listview_set_on_bind_view_holder(
    widget_t* widget, dynamic_listview_on_bind_view_holder on_bind_view_holder, void* ctx) {
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL, RET_BAD_PARAMS);

  dynamic_listview->on_bind_view_holder_ctx = ctx;
  dynamic_listview->on_bind_view_holder = on_bind_view_holder;

  return RET_OK;
}

ret_t dynamic_listview_set_on_get_item_view_type(
    widget_t* widget, dynamic_listview_on_get_item_view_type on_get_item_view_type, void* ctx) {
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL, RET_BAD_PARAMS);

  dynamic_listview->on_get_item_view_type_ctx = ctx;
  dynamic_listview->on_get_item_view_type = on_get_item_view_type;

  return RET_OK;
}

ret_t dynamic_listview_set_on_create_view_holder(
    widget_t* widget, dynamic_listview_on_create_view_holder on_create_view_holder, void* ctx) {
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL, RET_BAD_PARAMS);

  dynamic_listview->on_create_view_holder_ctx = ctx;
  dynamic_listview->on_create_view_holder = on_create_view_holder;

  return RET_OK;
}

ret_t dynamic_listview_set_on_prepare_row(widget_t* widget,
                                          dynamic_listview_on_prepare_row_t on_prepare_row,
                                          void* ctx) {
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL, RET_BAD_PARAMS);

  dynamic_listview->on_prepare_row_ctx = ctx;
  dynamic_listview->on_prepare_row = on_prepare_row;

  return RET_OK;
}

static ret_t dynamic_listview_get_offset(widget_t* widget, xy_t* out_x, xy_t* out_y) {
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL && out_x != NULL && out_y != NULL, RET_BAD_PARAMS);
  *out_x = 0;
  *out_y = dynamic_listview->yoffset;

  return RET_OK;
}

TK_DECL_VTABLE(dynamic_listview) = {.size = sizeof(dynamic_listview_t),
                                    .type = WIDGET_TYPE_DYNAMIC_LISTVIEW,
                                    .scrollable = TRUE,
                                    .clone_properties = s_dynamic_listview_properties,
                                    .persistent_properties = s_dynamic_listview_properties,
                                    .parent = TK_PARENT_VTABLE(widget),
                                    .create = dynamic_listview_create,
                                    .on_paint_self = dynamic_listview_on_paint_self,
                                    .get_offset = dynamic_listview_get_offset,
                                    .set_prop = dynamic_listview_set_prop,
                                    .get_prop = dynamic_listview_get_prop,
                                    .on_event = dynamic_listview_on_event,
                                    .on_paint_children = dynamic_listview_on_paint_children,
                                    .find_target = dynamic_listview_find_target,
                                    .invalidate = dynamic_listview_invalidate,
                                    .on_destroy = dynamic_listview_on_destroy};

widget_t* dynamic_listview_create(widget_t* parent, xy_t x, xy_t y, wh_t w, wh_t h) {
  widget_t* widget = widget_create(parent, TK_REF_VTABLE(dynamic_listview), x, y, w, h);
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL, NULL);

  dynamic_listview->rows = 0;
  dynamic_listview->width = w;
  dynamic_listview->row_height = 40;
  dynamic_listview->yslidable = TRUE;
  dynamic_listview->yspeed_scale = 1;

  return widget;
}

widget_t* dynamic_listview_cast(widget_t* widget) {
  return_value_if_fail(WIDGET_IS_INSTANCE_OF(widget, dynamic_listview), NULL);

  return widget;
}

int64_t dynamic_listview_get_virtual_h(widget_t* widget) {
  int64_t virtual_h = 0;
  dynamic_listview_t* dynamic_listview = DYNAMIC_LISTVIEW(widget);
  return_value_if_fail(dynamic_listview != NULL, 1);
//计算出要加上几个item，计算列表内容的高度时，要用columns的倍数来算，否则高度会偏小，最后一行可能显示不全
  int64_t surplus = dynamic_listview->rows % dynamic_listview->columns;
  if(surplus > 0) {
    surplus = dynamic_listview->columns - surplus;
  }
  virtual_h = (dynamic_listview->rows+surplus) * dynamic_listview->row_height / dynamic_listview->columns;
  log_warn("dynamic_listview_get_virtual_h  surplus=%d rows=%d columns=%d row_height=%d virtual_h=%d max=%d\n\n\n",surplus, dynamic_listview->rows, dynamic_listview->columns, dynamic_listview->row_height, virtual_h,tk_max(virtual_h, widget->h));
  return tk_max(virtual_h, widget->h);
}
