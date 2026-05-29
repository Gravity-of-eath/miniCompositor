/**
 * File:   spine2d.h
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


#ifndef TK_SPINE2D_H
#define TK_SPINE2D_H

#include "base/widget.h"

BEGIN_C_DECLS
/**
 * @class spine2d_t
 * @parent widget_t
 * @annotation ["scriptable","design","widget"]
 * 基于spine的骨骼动画。
 * 在xml中使用"spine2d"标签创建控件。如：
 *
 * ```xml
 * <!-- ui -->
 * <spine2d x="c" y="50" w="100" h="100"/>
 * ```
 *
 * 可用通过style来设置控件的显示风格，如字体的大小和颜色等等。如：
 * 
 * ```xml
 * <!-- style -->
 * <spine2d>
 *   <style name="default" font_size="32">
 *     <normal text_color="black" />
 *   </style>
 * </spine2d>
 * ```
 */
typedef struct _spine2d_t {
  widget_t widget;

  /**
   * @property {char*} atlas
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * atlas文件名。
   */
  char* atlas;

  /**
   * @property {char*} skeleton
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * skeleton文件名。
   */
  char* skeleton;

  /**
   * @property {char*} action
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 当前动画名。
   */
  char* action;

  /**
   * @property {float} scale_x
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * x缩放。
   */
  float scale_x;

  /**
   * @property {float} scale_y
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * y缩放。
   */
  float scale_y;
  
  /**
   * @property {float} scale_time
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * time缩放。指定播放速度，1 表示正常速度，小于 1 表示减速，大于 1 表示加速。
   */
  float scale_time;
  
  /**
   * @property {bool_t} loop
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 是否循环当前动作。
   */
  bool_t loop;

  /*private*/
  void* skeleton_info;
  uint32_t timer_id;
} spine2d_t;

/**
 * @event {event_t} EVT_ANIM_START
 * 动画开始事件。
 */

/**
 * @event {event_t} EVT_ANIM_ONCE
 * 动画完成一次事件。
 */

/**
 * @event {event_t} EVT_ANIM_END
 * 动画结束事件。
 */

/**
 * @method spine2d_create
 * @annotation ["constructor", "scriptable"]
 * 创建spine2d对象
 * @param {widget_t*} parent 父控件
 * @param {xy_t} x x坐标
 * @param {xy_t} y y坐标
 * @param {wh_t} w 宽度
 * @param {wh_t} h 高度
 *
 * @return {widget_t*} spine2d对象。
 */
widget_t* spine2d_create(widget_t* parent, xy_t x, xy_t y, wh_t w, wh_t h);

/**
 * @method spine2d_cast
 * 转换为spine2d对象(供脚本语言使用)。
 * @annotation ["cast", "scriptable"]
 * @param {widget_t*} widget spine2d对象。
 *
 * @return {widget_t*} spine2d对象。
 */
widget_t* spine2d_cast(widget_t* widget);


/**
 * @method spine2d_set_atlas
 * 设置 atlas文件名。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {const char*} atlas atlas文件名。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t spine2d_set_atlas(widget_t* widget, const char* atlas);

/**
 * @method spine2d_set_skeleton
 * 设置 skeleton文件名。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {const char*} skeleton skeleton文件名。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t spine2d_set_skeleton(widget_t* widget, const char* skeleton);

/**
 * @method spine2d_set_action
 * 设置 当前动画名。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {const char*} action 当前动画名。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t spine2d_set_action(widget_t* widget, const char* action);

/**
 * @method spine2d_set_scale_x
 * 设置 x缩放。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {float} scale_x x缩放。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t spine2d_set_scale_x(widget_t* widget, float scale_x);

/**
 * @method spine2d_set_scale_y
 * 设置 y缩放。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {float} scale_y y缩放。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t spine2d_set_scale_y(widget_t* widget, float scale_y);

/**
 * @method spine2d_set_scale_time
 * 设置 time缩放。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {float} scale_time time缩放。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t spine2d_set_scale_time(widget_t* widget, float scale_time);

/**
 * @method spine2d_set_loop
 * 设置 是否循环当前动作(下次设置动作后生效)。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {bool_t} loop 是否循环当前动作。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t spine2d_set_loop(widget_t* widget, bool_t loop);

#define SPINE2D_PROP_ATLAS "atlas"
#define SPINE2D_PROP_SKELETON "skeleton"
#define SPINE2D_PROP_ACTION "action"
#define SPINE2D_PROP_SCALE_X "scale_x"
#define SPINE2D_PROP_SCALE_Y "scale_y"
#define SPINE2D_PROP_SCALE_TIME "scale_time"
#define SPINE2D_PROP_LOOP "loop"

#define WIDGET_TYPE_SPINE2D "spine2d"

#define SPINE2D(widget) ((spine2d_t*)(spine2d_cast(WIDGET(widget))))

/*public for subclass and runtime type check*/
TK_EXTERN_VTABLE(spine2d);

END_C_DECLS

#endif /*TK_SPINE2D_H*/
