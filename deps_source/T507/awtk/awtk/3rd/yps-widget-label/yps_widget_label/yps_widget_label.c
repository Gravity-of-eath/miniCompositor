/**
 * File:   yps_widget_label.c
 * Author: 云片松
 * Brief:  组合标签控件扩展，同一标签内支持不同字体字号颜色设置
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

#include "yps_widget_label.h"

#include "../common_tool/yps_awtk_str_tool.h"
#include "tkc/mem.h"
#include "tkc/utils.h"

static color_t* color_from_value(color_t* color_value, const value_t* v) {
  str_t str_color;
  str_init(&str_color, 32);
  str_from_value(&str_color, v);
  color_from_str(color_value, str_color.str);
  str_reset(&str_color);
  return color_value;
}

static ret_t get_draw_text_size(canvas_t* c, str_t* text, int user_width,
                                int user_height, rect_t* text_rect) {
  int width = 0;
  int height = 0;

  if (user_width == 0) {
    width = canvas_measure_utf8(c, text->str);
  } else {
    width = user_width;
  }

  if (user_height == 0) {
    height = canvas_get_font_height(c);
  } else {
    height = user_height;
  }

  text_rect->w = width;
  text_rect->h = height;
  return RET_OK;
}

static ret_t set_text_align(canvas_t* c, str_t* align) {
  if (str_eq(align, "left")) {
    canvas_set_text_align(c, ALIGN_H_LEFT, ALIGN_V_MIDDLE);
  } else if (str_eq(align, "right")) {
    canvas_set_text_align(c, ALIGN_H_RIGHT, ALIGN_V_MIDDLE);
  } else {
    canvas_set_text_align(c, ALIGN_H_CENTER, ALIGN_V_MIDDLE);
  }
  return RET_OK;
}

static rect_t get_custom_rect(rect_t* text_rect, int32_t margin_left,
                              int margin_right, int margin_top,
                              int margin_bottom) {
  rect_t custom_rect;
  custom_rect.x = text_rect->x + margin_left;
  custom_rect.y = text_rect->y + margin_top;
  custom_rect.w = text_rect->w - margin_left - margin_right;
  custom_rect.h = text_rect->h - margin_top - margin_bottom;
  return custom_rect;
}

static ret_t reload_style(widget_t* widget) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);

  style_t* style = widget->astyle;

  // 获取字体
  // if(yps_widget_label->font_name.size == 0){
  //   str_set(&yps_widget_label->font_name,
  //                 style_get_str(style, STYLE_ID_FONT_NAME, "default"));
  // }
  // if(yps_widget_label->prefix_font_name.size == 0){
  //   str_set(&yps_widget_label->prefix_font_name,
  //                 style_get_str(style, STYLE_ID_PREFIX_FONT_NAME,
  //                 "default"));
  // }
  // if(yps_widget_label->suffix_font_name.size == 0){
  //   str_set(&yps_widget_label->suffix_font_name,
  //                 style_get_str(style, STYLE_ID_SUFFIX_FONT_NAME,
  //                 "default"));
  // }

  //   // 获取字体大小
  //   if(yps_widget_label->font_size == 0){
  //     yps_widget_label->font_size =
  //         style_get_int(style, STYLE_ID_FONT_SIZE, TK_DEFAULT_FONT_SIZE);
  //   }
  //   if(yps_widget_label->prefix_font_size == 0){
  //     yps_widget_label->prefix_font_size =
  //         style_get_int(style, STYLE_ID_PREFIX_FONT_SIZE,
  //         TK_DEFAULT_FONT_SIZE);
  //   }
  //   if(yps_widget_label->suffix_font_size == 0){
  //     yps_widget_label->suffix_font_size =
  //         style_get_int(style, STYLE_ID_SUFFIX_FONT_SIZE,
  //         TK_DEFAULT_FONT_SIZE);
  //   }

  //   // 获取左间距
  //   if (yps_widget_label->prefix_text_margin_left < 0) {
  //     yps_widget_label->prefix_text_margin_left =
  //         style_get_int(style, STYLE_ID_PREFIX_MARGIN_LEFT, 0);
  //   }
  //   if (yps_widget_label->suffix_text_margin_left < 0) {
  //     yps_widget_label->suffix_text_margin_left =
  //         style_get_int(style, STYLE_ID_SUFFIX_MARGIN_LEFT, 0);
  //   }

  //   // 获取右间距
  //   if (yps_widget_label->prefix_text_margin_right < 0) {
  //     yps_widget_label->prefix_text_margin_right =
  //         style_get_int(style, STYLE_ID_PREFIX_MARGIN_RIGHT, 0);
  //   }
  //   if(yps_widget_label->suffix_text_margin_right < 0){
  //   yps_widget_label->suffix_text_margin_right =
  //       style_get_int(style, STYLE_ID_SUFFIX_MARGIN_RIGHT, 0);
  //   }
  //   // 获取下间距
  //   if(yps_widget_label->prefix_text_margin_bottom < 0){
  //   yps_widget_label->prefix_text_margin_bottom =
  //       style_get_int(style, STYLE_ID_PREFIX_MARGIN_BOTTOM, 0);
  //   }
  //   if(yps_widget_label->suffix_text_margin_bottom < 0){
  //   yps_widget_label->suffix_text_margin_bottom =
  //       style_get_int(style, STYLE_ID_SUFFIX_MARGIN_BOTTOM, 0);
  //   }

  //   if(yps_widget_label->prefix_text_width < 0){
  //   yps_widget_label->prefix_text_width =
  //       style_get_int(style, STYLE_ID_PREFIX_WIDTH, 0);
  //   }
  //   if(yps_widget_label->suffix_text_width < 0){
  //   yps_widget_label->suffix_text_width =
  //       style_get_int(style, STYLE_ID_SUFFIX_WIDTH, 0);
  //   }

  //   color_t default_color = color_init(0, 0, 0, 255);
  //   if(yps_widget_label->font_color.rgba.a == 0){
  //   yps_widget_label->font_color =
  //       style_get_color(style, STYLE_ID_TEXT_COLOR, default_color);
  //   }
  //   if(yps_widget_label->prefix_font_color.rgba.a == 0){
  //   yps_widget_label->prefix_font_color =
  //       style_get_color(style, STYLE_ID_PREFIX_FONT_COLOR, default_color);
  //   }
  //   if(yps_widget_label->suffix_font_color.rgba.a == 0){
  //   yps_widget_label->suffix_font_color =
  //       style_get_color(style, STYLE_ID_SUFFIX_FONT_COLOR, default_color);
  //   }
  return RET_OK;
}

ret_t yps_widget_label_set_text_font_size(widget_t* widget, int32_t font_size) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->font_size = font_size;
  return RET_OK;
}

ret_t yps_widget_label_set_prefix_text_font_size(widget_t* widget,
                                                 int32_t font_size) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->prefix_font_size = font_size;
  return RET_OK;
}

ret_t yps_widget_label_set_suffix_text_font_size(widget_t* widget,
                                                 int32_t font_size) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->suffix_font_size = font_size;
  return RET_OK;
}

ret_t yps_widget_label_set_text_font_color(widget_t* widget, char* font_color) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  color_from_str(&(yps_widget_label->font_color), font_color);
  return RET_OK;
}

ret_t yps_widget_label_set_prefix_text_font_color(widget_t* widget,
                                                  char* font_color) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  color_from_str(&(yps_widget_label->prefix_font_color), font_color);
  return RET_OK;
}

ret_t yps_widget_label_set_suffix_text_font_color(widget_t* widget,
                                                  char* font_color) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  color_from_str(&(yps_widget_label->suffix_font_color), font_color);
  return RET_OK;
}

ret_t yps_widget_label_set_text_width(widget_t* widget, int32_t text_width) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->text_width = text_width;
  return RET_OK;
}

ret_t yps_widget_label_set_prefix_text_width(widget_t* widget,
                                             int32_t text_width) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->prefix_text_width = text_width;
  return RET_OK;
}

ret_t yps_widget_label_set_suffix_text_width(widget_t* widget,
                                             int32_t text_width) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->suffix_text_width = text_width;
  return RET_OK;
}

ret_t yps_widget_label_set_text_height(widget_t* widget, int32_t text_height) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->text_height = text_height;
  return RET_OK;
}

ret_t yps_widget_label_set_prefix_text_height(widget_t* widget,
                                              int32_t text_height) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->prefix_text_height = text_height;
  return RET_OK;
}

ret_t yps_widget_label_set_suffix_text_height(widget_t* widget,
                                              int32_t text_height) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->suffix_text_height = text_height;
  return RET_OK;
}

ret_t yps_widget_label_set_text_align(widget_t* widget,
                                      const char* text_align) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  str_set(&(yps_widget_label->text_align), text_align);
  return RET_OK;
}

ret_t yps_widget_label_set_prefix_text_align(widget_t* widget,
                                             const char* text_align) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  str_set(&(yps_widget_label->prefix_text_align), text_align);
  return RET_OK;
}

ret_t yps_widget_label_set_suffix_text_align(widget_t* widget,
                                             const char* text_align) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  str_set(&(yps_widget_label->suffix_text_align), text_align);
  return RET_OK;
}

ret_t yps_widget_label_set_text_margin_left(widget_t* widget,
                                            int32_t text_margin_left) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->text_margin_left = text_margin_left;
  return RET_OK;
}

ret_t yps_widget_label_set_text_margin_right(widget_t* widget,
                                             int32_t text_margin_right) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->text_margin_right = text_margin_right;
  return RET_OK;
}

ret_t yps_widget_label_set_text_margin_top(widget_t* widget,
                                           int32_t text_margin_top) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->text_margin_top = text_margin_top;
  return RET_OK;
}

ret_t yps_widget_label_set_text_margin_bottom(widget_t* widget,
                                              int32_t text_margin_bottom) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->text_margin_bottom = text_margin_bottom;
  return RET_OK;
}

ret_t yps_widget_label_set_prefix_text_margin_left(
    widget_t* widget, int32_t prefix_text_margin_left) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->prefix_text_margin_left = prefix_text_margin_left;
  return RET_OK;
}

ret_t yps_widget_label_set_prefix_text_margin_right(
    widget_t* widget, int32_t prefix_text_margin_right) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->prefix_text_margin_right = prefix_text_margin_right;
  return RET_OK;
}

ret_t yps_widget_label_set_prefix_text_margin_top(
    widget_t* widget, int32_t prefix_text_margin_top) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->prefix_text_margin_top = prefix_text_margin_top;
  return RET_OK;
}

ret_t yps_widget_label_set_prefix_text_margin_bottom(
    widget_t* widget, int32_t prefix_text_margin_bottom) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->prefix_text_margin_bottom = prefix_text_margin_bottom;
  return RET_OK;
}

ret_t yps_widget_label_set_suffix_text_margin_left(
    widget_t* widget, int32_t suffix_text_margin_left) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->suffix_text_margin_left = suffix_text_margin_left;
  return RET_OK;
}

ret_t yps_widget_label_set_suffix_text_margin_right(
    widget_t* widget, int32_t suffix_text_margin_right) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->suffix_text_margin_right = suffix_text_margin_right;
  return RET_OK;
}

ret_t yps_widget_label_set_suffix_text_margin_top(
    widget_t* widget, int32_t suffix_text_margin_top) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->suffix_text_margin_top = suffix_text_margin_top;
  return RET_OK;
}

ret_t yps_widget_label_set_suffix_text_margin_bottom(
    widget_t* widget, int32_t suffix_text_margin_bottom) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->suffix_text_margin_bottom = suffix_text_margin_bottom;
  return RET_OK;
}

ret_t yps_widget_label_set_font_name(widget_t* widget, const char* font_name) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  str_set(&(yps_widget_label->font_name), font_name);
  return RET_OK;
}

ret_t yps_widget_label_set_prefix_font_name(widget_t* widget,
                                            const char* prefix_font_name) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  str_set(&(yps_widget_label->prefix_font_name), prefix_font_name);
  return RET_OK;
}

ret_t yps_widget_label_set_suffix_font_name(widget_t* widget,
                                            const char* suffix_font_name) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  str_set(&(yps_widget_label->suffix_font_name), suffix_font_name);
  return RET_OK;
}

ret_t yps_widget_label_set_center_text(widget_t* widget, const char* center_text) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  str_set(&(yps_widget_label->center_text), center_text);
  return RET_OK;
}

ret_t yps_widget_label_set_prefix_text(widget_t* widget,
                                       const char* prefix_text) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  str_set(&(yps_widget_label->prefix_text), prefix_text);
  return RET_OK;
}

ret_t yps_widget_label_set_suffix_text(widget_t* widget,
                                       const char* suffix_text) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  str_set(&(yps_widget_label->suffix_text), suffix_text);
  return RET_OK;
}

ret_t yps_widget_label_bind_text(widget_t* widget, str_t* text){
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->bind_text = text;
}

ret_t yps_widget_label_bind_prefix_text(widget_t* widget, str_t* prefix_text){
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
  yps_widget_label->bind_prefix_text = prefix_text;
}

ret_t yps_widget_label_bind_suffix_text(widget_t* widget, str_t* suffix_text){
    yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
    return_value_if_fail(yps_widget_label != NULL, RET_BAD_PARAMS);
    yps_widget_label->bind_suffix_text = suffix_text;
}

static ret_t yps_widget_label_get_prop(widget_t* widget, const char* name,
                                       value_t* v) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL && name != NULL && v != NULL,
                       RET_BAD_PARAMS);

  if (tk_str_eq(YPS_WIDGET_LABEL_PROP_FONT_NAME, name)) {
    value_set_str(v, yps_widget_label->font_name.str);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_PREFIX_FONT_NAME, name)) {
    value_set_str(v, yps_widget_label->prefix_font_name.str);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_SUFFIX_FONT_NAME, name)) {
    value_set_str(v, yps_widget_label->suffix_font_name.str);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_FONT_SIZE, name)) {
    value_set_int(v, yps_widget_label->font_size);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_PREFIX_FONT_SIZE, name)) {
    value_set_int(v, yps_widget_label->prefix_font_size);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_SUFFIX_FONT_SIZE, name)) {
    value_set_int(v, yps_widget_label->suffix_font_size);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_FONT_COLOR, name)) {
    color_hex_str(yps_widget_label->font_color, yps_widget_label->font_color_str);
    value_set_str(v, yps_widget_label->font_color_str);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_PREFIX_FONT_COLOR, name)) {
    color_hex_str(yps_widget_label->prefix_font_color, yps_widget_label->prefix_font_color_str);
    value_set_str(v, yps_widget_label->prefix_font_color_str);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_SUFFIX_FONT_COLOR, name)) {
    color_hex_str(yps_widget_label->suffix_font_color, yps_widget_label->suffix_font_color_str);
    value_set_str(v, yps_widget_label->suffix_font_color_str);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_TEXT_WIDTH, name)) {
    value_set_int(v, yps_widget_label->text_width);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_PREFIX_TEXT_WIDTH, name)) {
    value_set_int(v, yps_widget_label->prefix_text_width);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_SUFFIX_TEXT_WIDTH, name)) {
    value_set_int(v, yps_widget_label->suffix_text_width);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_TEXT_HEIGHT, name)) {
    value_set_int(v, yps_widget_label->text_height);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_PREFIX_TEXT_HEIGHT, name)) {
    value_set_int(v, yps_widget_label->prefix_text_height);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_SUFFIX_TEXT_HEIGHT, name)) {
    value_set_int(v, yps_widget_label->suffix_text_height);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_TEXT_ALIGN, name)) {
    value_set_str(v, yps_widget_label->text_align.str);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_PREFIX_TEXT_ALIGN, name)) {
    value_set_str(v, yps_widget_label->prefix_text_align.str);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_SUFFIX_TEXT_ALIGN, name)) {
    value_set_str(v, yps_widget_label->suffix_text_align.str);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_MARGIN_LEFT, name)) {
    value_set_int(v, yps_widget_label->text_margin_left);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_MARGIN_RIGHT, name)) {
    value_set_int(v, yps_widget_label->text_margin_right);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_MARGIN_TOP, name)) {
    value_set_int(v, yps_widget_label->text_margin_top);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_MARGIN_BOTTOM, name)) {
    value_set_int(v, yps_widget_label->text_margin_bottom);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_PREFIX_MARGIN_LEFT, name)) {
    value_set_int(v, yps_widget_label->prefix_text_margin_left);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_PREFIX_MARGIN_RIGHT, name)) {
    value_set_int(v, yps_widget_label->prefix_text_margin_right);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_PREFIX_MARGIN_TOP, name)) {
    value_set_int(v, yps_widget_label->prefix_text_margin_top);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_PREFIX_MARGIN_BOTTOM, name)) {
    value_set_int(v, yps_widget_label->prefix_text_margin_bottom);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_SUFFIX_MARGIN_LEFT, name)) {
    value_set_int(v, yps_widget_label->suffix_text_margin_left);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_SUFFIX_MARGIN_RIGHT, name)) {
    value_set_int(v, yps_widget_label->suffix_text_margin_right);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_SUFFIX_MARGIN_TOP, name)) {
    value_set_int(v, yps_widget_label->suffix_text_margin_top);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_SUFFIX_MARGIN_BOTTOM, name)) {
    value_set_int(v, yps_widget_label->suffix_text_margin_bottom);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_TEXT, name)) {
    value_set_str(v, yps_widget_label->center_text.str);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_PREFIX_TEXT, name)) {
    value_set_str(v, yps_widget_label->prefix_text.str);
    return RET_OK;
  } else if (tk_str_eq(YPS_WIDGET_LABEL_PROP_SUFFIX_TEXT, name)) {
    value_set_str(v, yps_widget_label->suffix_text.str);
    return RET_OK;
  };

  return RET_NOT_FOUND;
}

static ret_t yps_widget_label_set_prop(widget_t* widget, const char* name,
                                       const value_t* v) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(widget != NULL && name != NULL && v != NULL,
                       RET_BAD_PARAMS);

  if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_FONT_NAME)) {
    str_from_value(&(yps_widget_label->font_name), v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_PREFIX_FONT_NAME)) {
    str_from_value(&(yps_widget_label->prefix_font_name), v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_SUFFIX_FONT_NAME)) {
    str_from_value(&(yps_widget_label->suffix_font_name), v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_FONT_SIZE)) {
    yps_widget_label->font_size = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_PREFIX_FONT_SIZE)) {
    yps_widget_label->prefix_font_size = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_SUFFIX_FONT_SIZE)) {
    yps_widget_label->suffix_font_size = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_FONT_COLOR)) {
    // str_set(&(yps_widget_label->font_color), value_str(v));
    color_from_value(&(yps_widget_label->font_color), v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_PREFIX_FONT_COLOR)) {
    color_from_value(&(yps_widget_label->prefix_font_color), v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_SUFFIX_FONT_COLOR)) {
    color_from_value(&(yps_widget_label->suffix_font_color), v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_TEXT_WIDTH)) {
    yps_widget_label->text_width = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_PREFIX_TEXT_WIDTH)) {
    yps_widget_label->prefix_text_width = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_SUFFIX_TEXT_WIDTH)) {
    yps_widget_label->suffix_text_width = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_TEXT_HEIGHT)) {
    yps_widget_label->text_height = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_PREFIX_TEXT_HEIGHT)) {
    yps_widget_label->prefix_text_height = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_SUFFIX_TEXT_HEIGHT)) {
    yps_widget_label->suffix_text_height = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_TEXT_ALIGN)) {
    str_from_value(&(yps_widget_label->text_align), v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_PREFIX_TEXT_ALIGN)) {
    str_from_value(&(yps_widget_label->prefix_text_align), v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_SUFFIX_TEXT_ALIGN)) {
    str_from_value(&(yps_widget_label->suffix_text_align), v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_MARGIN_LEFT)) {
    yps_widget_label->text_margin_left = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_MARGIN_RIGHT)) {
    yps_widget_label->text_margin_right = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_MARGIN_TOP)) {
    yps_widget_label->text_margin_top = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_MARGIN_BOTTOM)) {
    yps_widget_label->text_margin_bottom = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_PREFIX_MARGIN_LEFT)) {
    yps_widget_label->prefix_text_margin_left = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_PREFIX_MARGIN_RIGHT)) {
    yps_widget_label->prefix_text_margin_right = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_PREFIX_MARGIN_TOP)) {
    yps_widget_label->prefix_text_margin_top = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_PREFIX_MARGIN_BOTTOM)) {
    yps_widget_label->prefix_text_margin_bottom = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_SUFFIX_MARGIN_LEFT)) {
    yps_widget_label->suffix_text_margin_left = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_SUFFIX_MARGIN_RIGHT)) {
    yps_widget_label->suffix_text_margin_right = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_SUFFIX_MARGIN_TOP)) {
    yps_widget_label->suffix_text_margin_top = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_SUFFIX_MARGIN_BOTTOM)) {
    yps_widget_label->suffix_text_margin_bottom = value_int32(v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_TEXT)) {
    str_from_value(&(yps_widget_label->center_text), v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_PREFIX_TEXT)) {
    str_from_value(&(yps_widget_label->prefix_text), v);
    return RET_OK;
  } else if (tk_str_eq(name, YPS_WIDGET_LABEL_PROP_SUFFIX_TEXT)) {
    str_from_value(&(yps_widget_label->suffix_text), v);
    return RET_OK;
  }
  return RET_NOT_FOUND;
}

static ret_t yps_widget_label_on_destroy(widget_t* widget) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(widget != NULL && yps_widget_label != NULL,
                       RET_BAD_PARAMS);

  str_reset(&yps_widget_label->font_name);
  str_reset(&yps_widget_label->prefix_font_name);
  str_reset(&yps_widget_label->suffix_font_name);
  str_reset(&yps_widget_label->center_text);
  str_reset(&yps_widget_label->prefix_text);
  str_reset(&yps_widget_label->suffix_text);

  str_reset(&yps_widget_label->text_align);
  str_reset(&yps_widget_label->prefix_text_align);
  str_reset(&yps_widget_label->suffix_text_align);

  return RET_OK;
}

static ret_t yps_widget_label_on_paint_self(widget_t* widget, canvas_t* c) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);

  //   (void)yps_widget_label;

  if(yps_widget_label->bind_text != NULL) {
    str_set_with_len(&(yps_widget_label->center_text), yps_widget_label->bind_text->str,yps_widget_label->bind_text->size);
  }

  if(yps_widget_label->bind_prefix_text != NULL) {
    str_set_with_len(&(yps_widget_label->prefix_text), yps_widget_label->bind_prefix_text->str,yps_widget_label->bind_prefix_text->size);
  }

  if(yps_widget_label->bind_suffix_text != NULL) {
      str_set_with_len(&(yps_widget_label->suffix_text), yps_widget_label->bind_suffix_text->str,yps_widget_label->bind_suffix_text->size);
  }

  rect_t r = widget_get_content_area(widget);
  rect_t text_rect = rect_init(r.x, r.y, 0, 0);
  rect_t prefix_text_rect = rect_init(r.x, r.y, 0, 0);
  rect_t suffix_text_rect = rect_init(r.x, r.y, 0, 0);
  int x_offset = 0;
  int y_offset = 0;
//   canvas_set_fill_color(c, color_init(0x00, 0x00, 0x00, 0xff));
//   canvas_fill_rect(c, r.x, r.y, r.w, r.h);

  // 绘制前缀文本
  canvas_set_font(c, yps_widget_label->prefix_font_name.str,
                  yps_widget_label->prefix_font_size);
  get_draw_text_size(c, &(yps_widget_label->prefix_text),
                     yps_widget_label->prefix_text_width,
                     yps_widget_label->prefix_text_height, &prefix_text_rect);
  if (prefix_text_rect.w > 0 && prefix_text_rect.h > 0) {
    prefix_text_rect.x = x_offset;
    prefix_text_rect.w += yps_widget_label->prefix_text_margin_left +
                          yps_widget_label->prefix_text_margin_right;
    prefix_text_rect.h += yps_widget_label->prefix_text_margin_top +
                          yps_widget_label->prefix_text_margin_bottom;

    set_text_align(c, &(yps_widget_label->prefix_text_align));
    canvas_set_text_color(c, yps_widget_label->prefix_font_color);
    canvas_draw_utf8_in_rect(c, yps_widget_label->prefix_text.str,
                             &prefix_text_rect);
  }

  // 绘制文本
  x_offset = prefix_text_rect.x + prefix_text_rect.w;
  canvas_set_font(c, yps_widget_label->font_name.str,
                  yps_widget_label->font_size);
  get_draw_text_size(c, &(yps_widget_label->center_text), yps_widget_label->text_width,
                     yps_widget_label->text_height, &text_rect);
  if (text_rect.w > 0 && text_rect.h > 0) {
    text_rect.x = x_offset;
    text_rect.w += yps_widget_label->text_margin_left +
                   yps_widget_label->text_margin_right;
    text_rect.h += yps_widget_label->text_margin_top +
                   yps_widget_label->text_margin_bottom;
    canvas_set_text_color(c, yps_widget_label->font_color);
    set_text_align(c, &(yps_widget_label->text_align));
    canvas_draw_utf8_in_rect(c, yps_widget_label->center_text.str, &text_rect);
  }
  // 绘制后缀文本
  x_offset += text_rect.w;
  canvas_set_font(c, yps_widget_label->prefix_font_name.str,
                  yps_widget_label->suffix_font_size);
  get_draw_text_size(c, &(yps_widget_label->suffix_text),
                     yps_widget_label->suffix_text_width,
                     yps_widget_label->suffix_text_height, &suffix_text_rect);

  if (suffix_text_rect.w > 0 && suffix_text_rect.h > 0) {
    suffix_text_rect.x = x_offset;
    suffix_text_rect.y += y_offset;
    suffix_text_rect.w += yps_widget_label->suffix_text_margin_left +
                          yps_widget_label->suffix_text_margin_right;
    suffix_text_rect.h += yps_widget_label->suffix_text_margin_top +
                          yps_widget_label->suffix_text_margin_bottom;
    canvas_set_text_color(c, yps_widget_label->suffix_font_color);
    set_text_align(c, &(yps_widget_label->suffix_text_align));
    canvas_draw_utf8_in_rect(c, yps_widget_label->suffix_text.str,
                             &suffix_text_rect);
  }

  // rect_t prefix_fill_rect = get_custom_rect(&prefix_text_rect,
  // yps_widget_label->prefix_text_margin_left,
  //                                           yps_widget_label->prefix_text_margin_right,
  //                                           yps_widget_label->prefix_text_margin_top,
  //                                           yps_widget_label->prefix_text_margin_bottom);
  // rect_t text_fill_rect = get_custom_rect(&text_rect,
  // yps_widget_label->text_margin_left, yps_widget_label->text_margin_right,
  //                                         yps_widget_label->text_margin_top,
  //                                         yps_widget_label->text_margin_bottom);
  // rect_t suffix_fill_rect = get_custom_rect(&suffix_text_rect,
  // yps_widget_label->suffix_text_margin_left,
  //                                           yps_widget_label->suffix_text_margin_right,
  //                                           yps_widget_label->suffix_text_margin_top,
  //                                           yps_widget_label->suffix_text_margin_bottom);
  // log_debug("suff_file_rect: %d, %d, %d, %d \n", suffix_fill_rect.x,
  // suffix_fill_rect.y, suffix_fill_rect.w, suffix_fill_rect.h);
  // canvas_set_fill_color(c, color_init(0x00, 0x00,0x00,0xff));
  // canvas_fill_rect(c, r.x, r.y, r.w, r.h);

  // canvas_set_fill_color(c, color_init(0xff, 0xff,0x00,0xee));
  // canvas_fill_rect(c, prefix_fill_rect.x, prefix_fill_rect.y,
  // prefix_fill_rect.w, prefix_fill_rect.h); canvas_set_fill_color(c,
  // color_init(0xff, 0x00,0x00,0xee)); canvas_fill_rect(c, text_fill_rect.x,
  // text_fill_rect.y, text_fill_rect.w, text_fill_rect.h);
  // canvas_set_fill_color(c, color_init(0x00, 0xff,0x00,0xee));
  // canvas_fill_rect(c, suffix_fill_rect.x, suffix_fill_rect.y,
  // suffix_fill_rect.w, suffix_fill_rect.h);

  return RET_OK;
}

static ret_t yps_widget_label_on_event(widget_t* widget, event_t* e) {
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(widget != NULL && yps_widget_label != NULL,
                       RET_BAD_PARAMS);

  //   (void)yps_widget_label;
  switch (e->type) {
    case EVT_WIDGET_LOAD:
      break;
    case EVT_WINDOW_OPEN:
      break;
    case EVT_WINDOW_CLOSE:
      break;
    default:
      break;
  }

  return RET_OK;
}

const char* s_yps_widget_label_properties[] = {
    YPS_WIDGET_LABEL_PROP_FONT_NAME,
    YPS_WIDGET_LABEL_PROP_PREFIX_FONT_NAME,
    YPS_WIDGET_LABEL_PROP_SUFFIX_FONT_NAME,
    YPS_WIDGET_LABEL_PROP_FONT_SIZE,
    YPS_WIDGET_LABEL_PROP_PREFIX_FONT_SIZE,
    YPS_WIDGET_LABEL_PROP_SUFFIX_FONT_SIZE,
    YPS_WIDGET_LABEL_PROP_FONT_COLOR,
    YPS_WIDGET_LABEL_PROP_PREFIX_FONT_COLOR,
    YPS_WIDGET_LABEL_PROP_SUFFIX_FONT_COLOR,
    YPS_WIDGET_LABEL_PROP_TEXT_WIDTH,
    YPS_WIDGET_LABEL_PROP_PREFIX_TEXT_WIDTH,
    YPS_WIDGET_LABEL_PROP_SUFFIX_TEXT_WIDTH,
    YPS_WIDGET_LABEL_PROP_TEXT_HEIGHT,
    YPS_WIDGET_LABEL_PROP_PREFIX_TEXT_HEIGHT,
    YPS_WIDGET_LABEL_PROP_SUFFIX_TEXT_HEIGHT,
    YPS_WIDGET_LABEL_PROP_TEXT_ALIGN,
    YPS_WIDGET_LABEL_PROP_PREFIX_TEXT_ALIGN,
    YPS_WIDGET_LABEL_PROP_SUFFIX_TEXT_ALIGN,
    YPS_WIDGET_LABEL_PROP_MARGIN_LEFT,
    YPS_WIDGET_LABEL_PROP_MARGIN_RIGHT,
    YPS_WIDGET_LABEL_PROP_MARGIN_TOP,
    YPS_WIDGET_LABEL_PROP_MARGIN_BOTTOM,
    YPS_WIDGET_LABEL_PROP_PREFIX_MARGIN_LEFT,
    YPS_WIDGET_LABEL_PROP_PREFIX_MARGIN_RIGHT,
    YPS_WIDGET_LABEL_PROP_PREFIX_MARGIN_TOP,
    YPS_WIDGET_LABEL_PROP_PREFIX_MARGIN_BOTTOM,
    YPS_WIDGET_LABEL_PROP_SUFFIX_MARGIN_LEFT,
    YPS_WIDGET_LABEL_PROP_SUFFIX_MARGIN_RIGHT,
    YPS_WIDGET_LABEL_PROP_SUFFIX_MARGIN_TOP,
    YPS_WIDGET_LABEL_PROP_SUFFIX_MARGIN_BOTTOM,
    YPS_WIDGET_LABEL_PROP_TEXT,
    YPS_WIDGET_LABEL_PROP_PREFIX_TEXT,
    YPS_WIDGET_LABEL_PROP_SUFFIX_TEXT,
    NULL};

TK_DECL_VTABLE(yps_widget_label) = {
    .size = sizeof(yps_widget_label_t),
    .type = WIDGET_TYPE_YPS_WIDGET_LABEL,
    .clone_properties = s_yps_widget_label_properties,
    .persistent_properties = s_yps_widget_label_properties,
    .parent = TK_PARENT_VTABLE(widget),
    .create = yps_widget_label_create,
    .on_paint_self = yps_widget_label_on_paint_self,
    .set_prop = yps_widget_label_set_prop,
    .get_prop = yps_widget_label_get_prop,
    .on_event = yps_widget_label_on_event,
    .on_destroy = yps_widget_label_on_destroy};

widget_t* yps_widget_label_create(widget_t* parent, xy_t x, xy_t y, wh_t w,
                                  wh_t h) {
  widget_t* widget =
      widget_create(parent, TK_REF_VTABLE(yps_widget_label), x, y, w, h);
  yps_widget_label_t* yps_widget_label = YPS_WIDGET_LABEL(widget);
  return_value_if_fail(yps_widget_label != NULL, NULL);

  // 初始化默认值
  yps_widget_label->parent_widget = parent;
  yps_widget_label->flag_reload_style = TRUE;

  str_set(&(yps_widget_label->font_name), "default");
  str_set(&(yps_widget_label->prefix_font_name), "default");
  str_set(&(yps_widget_label->suffix_font_name), "default");

  str_set(&(yps_widget_label->text_align), "center");
  str_set(&(yps_widget_label->prefix_text_align), "center");
  str_set(&(yps_widget_label->suffix_text_align), "center");

  yps_widget_label->text_width = 0;
  yps_widget_label->prefix_text_width = 0;
  yps_widget_label->suffix_text_width = 0;

  yps_widget_label->font_size = 18;
  yps_widget_label->prefix_font_size = 18;
  yps_widget_label->suffix_font_size = 18;

  yps_widget_label->text_margin_left = 0;
  yps_widget_label->text_margin_right = 0;
  yps_widget_label->text_margin_top = 0;
  yps_widget_label->text_margin_bottom = 0;

  yps_widget_label->prefix_text_margin_left = 0;
  yps_widget_label->prefix_text_margin_right = 0;
  yps_widget_label->prefix_text_margin_top = 0;
  yps_widget_label->prefix_text_margin_bottom = 0;

  yps_widget_label->suffix_text_margin_left = 0;
  yps_widget_label->suffix_text_margin_right = 0;
  yps_widget_label->suffix_text_margin_top = 0;
  yps_widget_label->suffix_text_margin_bottom = 0;

  yps_widget_label->font_color = color_init(0, 0, 0, 0xff);
  yps_widget_label->prefix_font_color = color_init(0, 0, 0, 0xff);
  yps_widget_label->suffix_font_color = color_init(0, 0, 0, 0xff);

  yps_widget_label->bind_text = NULL;
  yps_widget_label->bind_prefix_text = NULL;
  yps_widget_label->bind_suffix_text = NULL;

  return widget;
}

widget_t* yps_widget_label_cast(widget_t* widget) {
  return_value_if_fail(WIDGET_IS_INSTANCE_OF(widget, yps_widget_label), NULL);
  return widget;
}

ret_t yps_widget_label_repaint(yps_widget_label_t* widget) {
  return_value_if_fail(widget != NULL, RET_BAD_PARAMS);

  rect_t r = widget_get_content_area(widget->parent_widget);
  widget_invalidate((widget_t*)widget, &r);
  return RET_OK;
}