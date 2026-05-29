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
#include "yps_progress_bar.h"

ret_t yps_progress_bar_set_max(widget_t* widget, int32_t max) {
  yps_progress_bar_t* yps_progress_bar = YPS_PROGRESS_BAR(widget);
  return_value_if_fail(yps_progress_bar != NULL, RET_BAD_PARAMS);
  if (max > 0) {
    yps_progress_bar->max = max;
    if(yps_progress_bar->max<yps_progress_bar->current){
      yps_progress_bar->current=yps_progress_bar->max;
    }
    widget_invalidate(yps_progress_bar, NULL);
  }
  return RET_OK;
}

ret_t yps_progress_bar_set_current(widget_t* widget, int32_t current) {
  yps_progress_bar_t* yps_progress_bar = YPS_PROGRESS_BAR(widget);
  return_value_if_fail(yps_progress_bar != NULL, RET_BAD_PARAMS);
  if (current <= yps_progress_bar->max) {
    yps_progress_bar->current = current;
    widget_invalidate(yps_progress_bar, NULL);
  }else{
    yps_progress_bar->current = yps_progress_bar->max;
    widget_invalidate(yps_progress_bar, NULL);
  }

  return RET_OK;
}

static ret_t yps_progress_bar_get_prop(widget_t* widget, const char* name, value_t* v) {
  yps_progress_bar_t* yps_progress_bar = YPS_PROGRESS_BAR(widget);
  return_value_if_fail(yps_progress_bar != NULL && name != NULL && v != NULL, RET_BAD_PARAMS);

  if (tk_str_eq(YPS_PROGRESS_BAR_PROP_MAX, name)) {
    value_set_int32(v, yps_progress_bar->max);
    return RET_OK;
  } else if (tk_str_eq(YPS_PROGRESS_BAR_PROP_CURRENT, name)) {
    value_set_int32(v, yps_progress_bar->current);
    return RET_OK;
  }

  return RET_NOT_FOUND;
}

static ret_t yps_progress_bar_set_prop(widget_t* widget, const char* name, const value_t* v) {
  yps_progress_bar_t* yps_progress_bar = YPS_PROGRESS_BAR(widget);
  return_value_if_fail(widget != NULL && name != NULL && v != NULL, RET_BAD_PARAMS);

  if (tk_str_eq(YPS_PROGRESS_BAR_PROP_MAX, name)) {
    yps_progress_bar_set_max(widget, value_int32(v));
    return RET_OK;
  } else if (tk_str_eq(YPS_PROGRESS_BAR_PROP_CURRENT, name)) {
    yps_progress_bar_set_current(widget, value_int32(v));
    return RET_OK;
  }

  return RET_NOT_FOUND;
}

static ret_t yps_progress_bar_on_destroy(widget_t* widget) {
  yps_progress_bar_t* yps_progress_bar = YPS_PROGRESS_BAR(widget);
  return_value_if_fail(widget != NULL && yps_progress_bar != NULL, RET_BAD_PARAMS);

  return RET_OK;
}

static rect_t* get_current_rect(widget_t* widget) {
  yps_progress_bar_t* yps_progress_bar = YPS_PROGRESS_BAR(widget);
  if (widget->w > widget->h) {  //--
    int32_t x = (1.0f * yps_progress_bar->current / yps_progress_bar->max) * widget->w / 10 * 9;
    // log_warn("get_current_rect x=%d   ", x);
    return rect_create(x, 1, widget->w / 10, widget->h-2);
  } else {  //|
    int32_t y = (1.0f * yps_progress_bar->current / yps_progress_bar->max) * widget->h / 10 * 9;
    // log_warn("get_current_rect y=%d   ", y);
    return rect_create(1, y, widget->w-2, widget->h / 10);
  }
}

static ret_t yps_progress_bar_on_paint_self(widget_t* widget, canvas_t* c) {
  yps_progress_bar_t* yps_progress_bar = YPS_PROGRESS_BAR(widget);

  style_t* style = widget->astyle;
  color_t fc = style_get_color(style, STYLE_ID_FG_COLOR, color_init(0xF, 0, 0, 0));
  int32_t round = style_get_int(style, STYLE_ID_ROUND_RADIUS, 0);
  // int32_t margin = style_get_int(style, STYLE_ID_MARGIN, 0);
  // log_warn("yps_progress_bar_on_paint_self fColor=%d  round=%d", fc.color, round);
  canvas_save(c);
  canvas_set_fill_color(c, fc);
  canvas_fill_rounded_rect(c, get_current_rect(widget), NULL, &fc, round);
  canvas_restore(c);

  (void)yps_progress_bar;

  return RET_OK;
}

static ret_t yps_progress_bar_on_event(widget_t* widget, event_t* e) {
  yps_progress_bar_t* yps_progress_bar = YPS_PROGRESS_BAR(widget);
  return_value_if_fail(widget != NULL && yps_progress_bar != NULL, RET_BAD_PARAMS);

  (void)yps_progress_bar;

  return RET_OK;
}

const char* s_yps_progress_bar_properties[] = {YPS_PROGRESS_BAR_PROP_MAX,
                                               YPS_PROGRESS_BAR_PROP_CURRENT, NULL};

TK_DECL_VTABLE(yps_progress_bar) = {.size = sizeof(yps_progress_bar_t),
                                    .type = WIDGET_TYPE_YPS_PROGRESS_BAR,
                                    .clone_properties = s_yps_progress_bar_properties,
                                    .persistent_properties = s_yps_progress_bar_properties,
                                    .parent = TK_PARENT_VTABLE(widget),
                                    .create = yps_progress_bar_create,
                                    .on_paint_self = yps_progress_bar_on_paint_self,
                                    .set_prop = yps_progress_bar_set_prop,
                                    .get_prop = yps_progress_bar_get_prop,
                                    .on_event = yps_progress_bar_on_event,
                                    .on_destroy = yps_progress_bar_on_destroy};

widget_t* yps_progress_bar_create(widget_t* parent, xy_t x, xy_t y, wh_t w, wh_t h) {
  widget_t* widget = widget_create(parent, TK_REF_VTABLE(yps_progress_bar), x, y, w, h);
  yps_progress_bar_t* yps_progress_bar = YPS_PROGRESS_BAR(widget);
  return_value_if_fail(yps_progress_bar != NULL, NULL);

  yps_progress_bar->max = 100;
  yps_progress_bar->current = 0;

  return widget;
}

widget_t* yps_progress_bar_cast(widget_t* widget) {
  return_value_if_fail(WIDGET_IS_INSTANCE_OF(widget, yps_progress_bar), NULL);

  return widget;
}
