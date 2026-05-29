/**
 * File:   dynamic_listview.h
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

#ifndef TK_DYNAMIC_LISTVIEW_H
#define TK_DYNAMIC_LISTVIEW_H

#include "base/widget.h"
#include "base/velocity.h"
#include "base/widget_animator.h"

BEGIN_C_DECLS

bool_t dynamic_listview_get_visible();
void dynamic_listview_set_visible(bool_t visible);

typedef ret_t (*dynamic_listview_on_bind_view_holder)(void* ctx, uint32_t view_type,
                                                      uint32_t row_index, widget_t* row);
typedef ret_t (*dynamic_listview_on_create_view_holder)(void* ctx, uint32_t row_index,
                                                        uint32_t view_type, widget_t* row);
typedef uint32_t (*dynamic_listview_on_get_item_view_type)(void* ctx, uint32_t row_index);
typedef ret_t (*dynamic_listview_on_prepare_row_t)(void* ctx, widget_t* client,
                                                   uint32_t prepare_cnt);

typedef struct _dynamic_listview_t {
  widget_t widget;

  /**
   * @property {uint32_t} row_height
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 行高。
   */
  uint32_t row_height;

  /**
   * @property {uint32_t} rows
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 最大行数。
   */
  uint32_t rows;


  /**
   * @property {uint32_t} columns
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 列数。
   */
  uint32_t columns;

  uint32_t width;

  /**
   * @property {int32_t} yoffset
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 偏移量。
   */
  int32_t yoffset;

  /**
   * @property {bool_t} yslidable
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 是否允许y方向滑动。
   */
  bool_t yslidable;

  /**
   * @property {float_t} yspeed_scale
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * y偏移速度比例。
   */
  float_t yspeed_scale;

  /*private*/
  uint32_t start_index;

  point_t down;
  bool_t pressed;
  bool_t dragged;
  velocity_t velocity;
  int32_t yoffset_end;
  int32_t yoffset_save;
  widget_animator_t* wa;
  bool_t first_move_after_down;

  /*用于加载数据的回调函数*/
  void* on_bind_view_holder_ctx;
  dynamic_listview_on_bind_view_holder on_bind_view_holder;

  /*创建行时的回调函数，可以注册事件处理函数*/
  void* on_create_view_holder_ctx;
  dynamic_listview_on_create_view_holder on_create_view_holder;

  void* on_get_item_view_type_ctx;
  dynamic_listview_on_get_item_view_type on_get_item_view_type;

  /*预处理行（创建行）的回调函数，可以注册事件处理函数*/
  void* on_prepare_row_ctx;
  dynamic_listview_on_prepare_row_t on_prepare_row;
} dynamic_listview_t;

/**
 * @event {event_t} EVT_SCROLL_START
 * 开始滚动事件。
 */

/**
 * @event {event_t} EVT_SCROLL_END
 * 结束滚动事件。
 */

/**
 * @event {event_t} EVT_SCROLL
 * 滚动事件。
 */

/**
 * @method dynamic_listview_create
 * @annotation ["constructor", "scriptable"]
 * 创建dynamic_listview对象
 * @param {widget_t*} parent 父控件
 * @param {xy_t} x x坐标
 * @param {xy_t} y y坐标
 * @param {wh_t} w 宽度
 * @param {wh_t} h 高度
 *
 * @return {widget_t*} dynamic_listview对象。
 */
widget_t* dynamic_listview_create(widget_t* parent, xy_t x, xy_t y, wh_t w, wh_t h);

/**
 * @method dynamic_listview_cast
 * 转换为dynamic_listview对象(供脚本语言使用)。
 * @annotation ["cast", "scriptable"]
 * @param {widget_t*} widget dynamic_listview对象。
 *
 * @return {widget_t*} dynamic_listview对象。
 */
widget_t* dynamic_listview_cast(widget_t* widget);

/**
 * @method dynamic_listview_set_row_height
 * 设置 行高。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {uint32_t} row_height 行高。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t dynamic_listview_set_row_height(widget_t* widget, uint32_t row_height);

/**
 * @method dynamic_listview_set_rows
 * 设置 最大行数。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {uint32_t} rows 最大行数。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t dynamic_listview_set_rows(widget_t* widget, uint32_t rows);

ret_t dynamic_listview_set_columns(widget_t* widget, uint32_t columns);

/**
 * @method dynamic_listview_set_yoffset
 * 设置 偏移量。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} yoffset 偏移量。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t dynamic_listview_set_yoffset(widget_t* widget, int32_t yoffset);

/**
 * @method dynamic_listview_set_yslidable
 * 设置 是否允许y方向滑动。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {bool_t} yslidable 是否允许y方向滑动。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t dynamic_listview_set_yslidable(widget_t* widget, bool_t yslidable);

/**
 * @method dynamic_listview_set_yspeed_scale
 * 设置 y偏移速度比例。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {float_t} yspeed_scale y偏移速度比例。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t dynamic_listview_set_yspeed_scale(widget_t* widget, float_t yspeed_scale);

/**
 * @method dynamic_listview_set_on_load_data
 * 设置 加载数据的回调函数。
 * @param {widget_t*} widget widget对象。
 * @param {dynamic_listview_on_load_data_t} on_bind_view_holder 回调函数。
 * @param {void*} ctx 回调函数的上下文。 
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t dynamic_listview_set_on_bind_view_holder(
    widget_t* widget, dynamic_listview_on_bind_view_holder on_bind_view_holder, void* ctx);

ret_t dynamic_listview_set_on_get_item_view_type(
    widget_t* widget, dynamic_listview_on_get_item_view_type on_get_item_view_type, void* ctx);

/**
 * @method dynamic_listview_set_on_create_row
 * 设置 创建行时的回调函数，在回调函数中可以注册控件的事件。
 * @param {widget_t*} widget widget对象。
 * @param {dynamic_listview_on_create_view_holder} on_create_view_holder 回调函数。
 * @param {void*} ctx 回调函数的上下文。 
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t dynamic_listview_set_on_create_view_holder(
    widget_t* widget, dynamic_listview_on_create_view_holder on_create_view_holder, void* ctx);

/**
 * @method dynamic_listview_set_on_prepare_row
 * 设置 预处理行（创建行）的回调函数，在回调函数中可以创建行控件。
 * @param {widget_t*} widget widget对象。
 * @param {dynamic_listview_on_prepare_row_t} on_prepare_row 回调函数。
 * @param {void*} ctx 回调函数的上下文。 
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t dynamic_listview_set_on_prepare_row(widget_t* widget,
                                          dynamic_listview_on_prepare_row_t on_prepare_row,
                                          void* ctx);

/**
 * @method dynamic_listview_get_virtual_h
 * 获取虚拟高度。
 * @param {widget_t*} widget widget对象。
 *
 * @return {int64_t} 返回虚拟高度。
 */
int64_t dynamic_listview_get_virtual_h(widget_t* widget);

/**
 * @method dynamic_listview_scroll_to_row
 * 滚动到指定行。
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} row 行号。
 * @param {int32_t} duration 动画时间
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t dynamic_listview_scroll_to_row(widget_t* widget, int32_t row, int32_t duration);

ret_t dynamic_listview_scroll_to_item(widget_t* widget, int32_t item);
ret_t dynamic_listview_scroll_to_next_page(widget_t* widget);
ret_t dynamic_listview_scroll_to_per_page(widget_t* widget);

/**
 * @method dynamic_listview_scroll_to_yoffset
 * 滚动到指定偏移位置。
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} yoffset 偏移量。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t dynamic_listview_scroll_to_yoffset(widget_t* widget, int32_t yoffset);

/**
 * @method dynamic_listview_ensure_children 
 * 确保子控件已经创建。
 * @param {widget_t*} widget widget对象。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t dynamic_listview_ensure_children(widget_t* widget);

/**
 * @method dynamic_listview_reload
 * 数据变化时让table client重新加载数据。
 * @param {widget_t*} widget widget对象。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t dynamic_listview_reload(widget_t* widget);

#define DYNAMIC_LISTVIEW_PROP_ROWS "rows"
#define DYNAMIC_LISTVIEW_PROP_COLUMNS "columns"
#define DYNAMIC_LISTVIEW_PROP_YOFFSET "yoffset"
#define DYNAMIC_LISTVIEW_PROP_YSLIDABLE "yslidable"
#define DYNAMIC_LISTVIEW_PROP_YSPEED_SCALE "yspeed_scale"
#define DYNAMIC_LISTVIEW_PROP_ROW_HEIGHT "row_height"

#define WIDGET_TYPE_DYNAMIC_LISTVIEW "dynamic_listview"

#define DYNAMIC_LISTVIEW(widget) ((dynamic_listview_t*)(dynamic_listview_cast(WIDGET(widget))))

/*public for subclass and runtime type check*/
TK_EXTERN_VTABLE(dynamic_listview);

END_C_DECLS

#endif /*TK_DYNAMIC_LISTVIEW_H*/
