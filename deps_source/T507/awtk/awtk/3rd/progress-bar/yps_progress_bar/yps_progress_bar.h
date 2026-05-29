/**
 * File:   yps_progress_bar.h
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


#ifndef TK_YPS_PROGRESS_BAR_H
#define TK_YPS_PROGRESS_BAR_H

#include "base/widget.h"

BEGIN_C_DECLS
/**
 * @class yps_progress_bar_t
 * @parent widget_t
 * @annotation ["scriptable","design","widget"]
 * yps_progress_bar
 * 在xml中使用"yps\_progress\_bar"标签创建控件。如：
 *
 * ```xml
 * <!-- ui -->
 * <yps_progress_bar x="c" y="50" w="100" h="100"/>
 * ```
 *
 * 可用通过style来设置控件的显示风格，如字体的大小和颜色等等。如：
 * 
 * ```xml
 * <!-- style -->
 * <yps_progress_bar>
 *   <style name="default" font_size="32">
 *     <normal text_color="black" />
 *   </style>
 * </yps_progress_bar>
 * ```
 */
typedef struct _yps_progress_bar_t {
  widget_t widget;


  /**
   * @property {int32_t} max
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 。
   */
  int32_t max;

  /**
   * @property {int32_t} current
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 。
   */
  int32_t current;

} yps_progress_bar_t;

/**
 * @method yps_progress_bar_create
 * @annotation ["constructor", "scriptable"]
 * 创建yps_progress_bar对象
 * @param {widget_t*} parent 父控件
 * @param {xy_t} x x坐标
 * @param {xy_t} y y坐标
 * @param {wh_t} w 宽度
 * @param {wh_t} h 高度
 *
 * @return {widget_t*} yps_progress_bar对象。
 */
widget_t* yps_progress_bar_create(widget_t* parent, xy_t x, xy_t y, wh_t w, wh_t h);

/**
 * @method yps_progress_bar_cast
 * 转换为yps_progress_bar对象(供脚本语言使用)。
 * @annotation ["cast", "scriptable"]
 * @param {widget_t*} widget yps_progress_bar对象。
 *
 * @return {widget_t*} yps_progress_bar对象。
 */
widget_t* yps_progress_bar_cast(widget_t* widget);


/**
 * @method yps_progress_bar_set_max
 * 设置 。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} max 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_progress_bar_set_max(widget_t* widget, int32_t max);

/**
 * @method yps_progress_bar_set_current
 * 设置 。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} current 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_progress_bar_set_current(widget_t* widget, int32_t current);


#define YPS_PROGRESS_BAR_PROP_MAX "max"
#define YPS_PROGRESS_BAR_PROP_CURRENT "current"

#define WIDGET_TYPE_YPS_PROGRESS_BAR "yps_progress_bar"

#define YPS_PROGRESS_BAR(widget) ((yps_progress_bar_t*)(yps_progress_bar_cast(WIDGET(widget))))

/*public for subclass and runtime type check*/
TK_EXTERN_VTABLE(yps_progress_bar);

END_C_DECLS

#endif /*TK_YPS_PROGRESS_BAR_H*/
