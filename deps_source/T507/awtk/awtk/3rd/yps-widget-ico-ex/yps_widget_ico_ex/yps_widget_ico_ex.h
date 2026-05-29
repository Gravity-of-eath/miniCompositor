/**
 * File:   yps_widget_ico_ex.h
 * Author: 云片松
 * Brief:  这是一个图标控件的升级版，支持一次性设置多个图片，提供接口可以通过id进行显示指定图片
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
 * 2023-4-19 wangdongpo created
 *
 */


#ifndef TK_YPS_WIDGET_ICO_EX_H
#define TK_YPS_WIDGET_ICO_EX_H
#include <stdlib.h>
#include <stdbool.h>
#include "base/widget.h"
#include <tkc/mutex.h>

BEGIN_C_DECLS
/**
 * @class yps_widget_ico_ex_t
 * @parent widget_t
 * @annotation ["scriptable","design","widget"]
 * 这是一个图标控件的升级版，支持一次性设置多个图片，提供接口可以通过id进行显示指定图片
 * 在xml中使用"yps\_widget\_ico\_ex"标签创建控件。如：
 *
 * ```xml
 * <!-- ui -->
 * <yps_widget_ico_ex x="c" y="50" w="100" h="100"/>
 * ```
 *
 * 可用通过style来设置控件的显示风格，如字体的大小和颜色等等。如：
 * 
 * ```xml
 * <!-- style -->
 * <yps_widget_ico_ex>
 *   <style name="default" font_size="32">
 *     <normal text_color="black" />
 *   </style>
 * </yps_widget_ico_ex>
 * ```
 */
typedef struct _yps_widget_ico_ex_t {
  widget_t widget;
  widget_t* widget_ico;

  /**
   * @property {str_t} image_list
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 图片名称前缀。
   */
  str_t image_list;

  /**
   * @property {int} image_id
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 图片id,设置之后通过索引显示指定图片。
   */
  int image_id;

} yps_widget_ico_ex_t;

/**
 * @method yps_widget_ico_ex_create
 * @annotation ["constructor", "scriptable"]
 * 创建yps_widget_ico_ex对象
 * @param {widget_t*} parent 父控件
 * @param {xy_t} x x坐标
 * @param {xy_t} y y坐标
 * @param {wh_t} w 宽度
 * @param {wh_t} h 高度
 *
 * @return {widget_t*} yps_widget_ico_ex对象。
 */
widget_t* yps_widget_ico_ex_create(widget_t* parent, xy_t x, xy_t y, wh_t w, wh_t h);

/**
 * @method yps_widget_ico_ex_cast
 * 转换为yps_widget_ico_ex对象(供脚本语言使用)。
 * @annotation ["cast", "scriptable"]
 * @param {widget_t*} widget yps_widget_ico_ex对象。
 *
 * @return {widget_t*} yps_widget_ico_ex对象。
 */
widget_t* yps_widget_ico_ex_cast(widget_t* widget);


/**
 * @method yps_widget_ico_ex_set_image_list
 * 设置 图片名称，多张图片使用逗号分割。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {const char*} image_list 图片名称，多张图片使用逗号分割。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_ico_ex_set_image_list(widget_t* widget, const char* image_list);

/**
 * @method yps_widget_ico_ex_set_image_id
 * 设置 图片显示id,设置之后通过索引显示指定图片。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int} image_id 图片显示id,从0开始索引。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_ico_ex_set_image_id(widget_t* widget, int image_id);

/**
 * @method yps_widget_ico_ex_get_image_id
 * 获取当前图片的ID。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 *
 * @return {int} 返回图片id。
 */
int yps_widget_ico_ex_get_image_id(widget_t* widget);


#define YPS_WIDGET_ICO_EX_PROP_IMAGE_LIST "image_list"
#define YPS_WIDGET_ICO_EX_PROP_IMAGE_ID "image_id"

#define WIDGET_TYPE_YPS_WIDGET_ICO_EX "yps_widget_ico_ex"

#define YPS_WIDGET_ICO_EX(widget) ((yps_widget_ico_ex_t*)(yps_widget_ico_ex_cast(WIDGET(widget))))

/*public for subclass and runtime type check*/
TK_EXTERN_VTABLE(yps_widget_ico_ex);

END_C_DECLS

#endif /*TK_YPS_WIDGET_ICO_EX_H*/
