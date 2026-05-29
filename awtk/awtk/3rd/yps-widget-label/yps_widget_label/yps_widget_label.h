/**
 * File:   yps_widget_label.h
 * Author: 云片松
 * Brief:  标签控件扩展，支持不同字体
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

#ifndef TK_YPS_WIDGET_LABEL_H
#define TK_YPS_WIDGET_LABEL_H
#include <stdbool.h>
#include "base/widget.h"
#include "../common_tool/yps_vector.h"
/////////////////////////////
// 自定义控件属性
/////////////////////////////
#define YPS_WIDGET_LABEL_PROP_FONT_NAME "font_name"
#define YPS_WIDGET_LABEL_PROP_PREFIX_FONT_NAME "prefix_font_name"
#define YPS_WIDGET_LABEL_PROP_SUFFIX_FONT_NAME "suffix_font_name"
#define YPS_WIDGET_LABEL_PROP_FONT_SIZE "font_size"
#define YPS_WIDGET_LABEL_PROP_PREFIX_FONT_SIZE "prefix_font_size"
#define YPS_WIDGET_LABEL_PROP_SUFFIX_FONT_SIZE "suffix_font_size"
#define YPS_WIDGET_LABEL_PROP_FONT_COLOR "font_color"
#define YPS_WIDGET_LABEL_PROP_PREFIX_FONT_COLOR "prefix_font_color"
#define YPS_WIDGET_LABEL_PROP_SUFFIX_FONT_COLOR "suffix_font_color"
#define YPS_WIDGET_LABEL_PROP_TEXT_WIDTH "text_width"
#define YPS_WIDGET_LABEL_PROP_PREFIX_TEXT_WIDTH "prefix_text_width"
#define YPS_WIDGET_LABEL_PROP_SUFFIX_TEXT_WIDTH "suffix_text_width"
#define YPS_WIDGET_LABEL_PROP_TEXT_HEIGHT "text_height"
#define YPS_WIDGET_LABEL_PROP_PREFIX_TEXT_HEIGHT "prefix_text_height"
#define YPS_WIDGET_LABEL_PROP_SUFFIX_TEXT_HEIGHT "suffix_text_height"
#define YPS_WIDGET_LABEL_PROP_TEXT_ALIGN "text_align"
#define YPS_WIDGET_LABEL_PROP_PREFIX_TEXT_ALIGN "prefix_text_align"
#define YPS_WIDGET_LABEL_PROP_SUFFIX_TEXT_ALIGN "suffix_text_align"
#define YPS_WIDGET_LABEL_PROP_MARGIN_LEFT "text_margin_left"
#define YPS_WIDGET_LABEL_PROP_MARGIN_RIGHT "text_margin_right"
#define YPS_WIDGET_LABEL_PROP_MARGIN_TOP "text_margin_top"
#define YPS_WIDGET_LABEL_PROP_MARGIN_BOTTOM "margin_text_bottom"
#define YPS_WIDGET_LABEL_PROP_PREFIX_MARGIN_LEFT "prefix_text_margin_left"
#define YPS_WIDGET_LABEL_PROP_PREFIX_MARGIN_RIGHT "prefix_text_margin_right"
#define YPS_WIDGET_LABEL_PROP_PREFIX_MARGIN_TOP "prefix_text_margin_top"
#define YPS_WIDGET_LABEL_PROP_PREFIX_MARGIN_BOTTOM "prefix_text_margin_bottom"
#define YPS_WIDGET_LABEL_PROP_SUFFIX_MARGIN_LEFT "suffix_text_margin_left"
#define YPS_WIDGET_LABEL_PROP_SUFFIX_MARGIN_RIGHT "suffix_text_margin_right"
#define YPS_WIDGET_LABEL_PROP_SUFFIX_MARGIN_TOP "suffix_text_margin_top"
#define YPS_WIDGET_LABEL_PROP_SUFFIX_MARGIN_BOTTOM "suffix_text_margin_bottom"
#define YPS_WIDGET_LABEL_PROP_TEXT "center_text"
#define YPS_WIDGET_LABEL_PROP_PREFIX_TEXT "prefix_text"
#define YPS_WIDGET_LABEL_PROP_SUFFIX_TEXT "suffix_text"

BEGIN_C_DECLS
/**
 * @class yps_widget_label_t
 * @parent widget_t
 * @annotation ["scriptable","design","widget"]
 * 标签控件扩展，支持不同字体
 * 在xml中使用"yps\_widget\_label"标签创建控件。如：
 *
 * ```xml
 * <!-- ui -->
 * <yps_widget_label x="c" y="50" w="100" h="100"/>
 * ```
 *
 * 可用通过style来设置控件的显示风格，如字体的大小和颜色等等。如：
 *
 * ```xml
 * <!-- style -->
 * <yps_widget_label>
 *   <style name="default" font_size="32">
 *     <normal text_color="black" />
 *   </style>
 * </yps_widget_label>
 * ```
 */
typedef struct _yps_widget_label_t {
  widget_t widget;

  bool flag_reload_style;

char font_color_str[12];
char prefix_font_color_str[12];
char suffix_font_color_str[12];

str_t* bind_text;
str_t* bind_prefix_text;
str_t* bind_suffix_text;

/* 父级容器指针 */
  widget_t* parent_widget;

  /**
   * @property {int32_t} font_size
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 中间文本字体大小
   */
  int32_t font_size;

    /**
   * @property {int32_t} prefix_font_size
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 前缀文本字体大小
   */
  int32_t prefix_font_size;

  /**
   * @property {int32_t} suffix_font_size
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 后缀文本字体大小
   */
  int32_t suffix_font_size;

    /**
     * @property {color_t} font_color
     * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
     * 中间文本字体颜色
     */
  color_t font_color;

    /**
     * @property {color_t} prefix_font_color
     * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
     * 前缀文本字体颜色
     */
  color_t prefix_font_color;

    /**
     * @property {color_t} suffix_font_color
     * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
     * 后缀文本字体颜色
     */
  color_t suffix_font_color;

  /**
   * @property {int32_t} text_width
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 中间文本宽度
   */
  int32_t text_width;

    /**
     * @property {int32_t} prefix_text_width
     * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
     * 前缀文本宽度
     */
  int32_t prefix_text_width;

    /**
     * @property {int32_t} suffix_text_width
     * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
     * 前缀文本宽度
     */
  int32_t suffix_text_width;


  /**
   * @property {int32_t} text_height
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 中间文本绘制区域高度
   */
  int32_t text_height;

    /**
     * @property {int32_t} prefix_text_height
     * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
     * 前缀文本绘制区域高度
     */
  int32_t prefix_text_height;

    /**
     * @property {int32_t} suffix_text_height
     * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
     * 后缀文本绘制区域高度
     */
  int32_t suffix_text_height;

  /**
   * @property {str_t} text_align
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 文中间本对齐方式 支持左对齐（left），右对齐（right），居中对齐（center）
   */
  str_t text_align;

  /**
   * @property {str_t} prefix_text_align
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 前缀文本对齐方式 支持左对齐（left），右对齐（right），居中对齐（center）
   */
  str_t prefix_text_align;

  /**
   * @property {str_t} suffix_text_align
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 后缀文本对齐方式 支持左对齐（left），右对齐（right），居中对齐（center）
   */
  str_t suffix_text_align;

  /**
   * @property {int32_t} text_margin_left
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 文本左间距
   */
    int32_t text_margin_left;

    /**
     * @property {int32_t} text_margin_right
     * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
     * 文本右间距
     */
    int32_t text_margin_right;

  /**
     * @property {int32_t} text_margin_top
     * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
     * 文本上间距
     */
    int32_t text_margin_top;

    /**
     * @property {int32_t} text_margin_bottom
     * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
     * 文本下间距
     */
    int32_t text_margin_bottom;

    /**
     * @property {int32_t} prefix_text_margin_left
     * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
     * 前缀文本左间距
     */
    int32_t prefix_text_margin_left;

    /**
     * @property {int32_t} prefix_text_margin_right
     * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
     * 前缀文本右间距
     */
    int32_t prefix_text_margin_right;

    /**
     * @property {int32_t} prefix_text_margin_top
     * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
     * 前缀文本上间距
     */
    int32_t prefix_text_margin_top;

    /**
     * @property {int32_t} prefix_text_margin_bottom
     * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
     * 前缀文本下间距
     */
    int32_t prefix_text_margin_bottom;

    /**
     * @property {int32_t} suffix_text_margin_left
     * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
     * 后缀文本左间距
     */
    int32_t suffix_text_margin_left;

    /**
     * @property {int32_t} suffix_text_margin_right
     * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
     * 后缀文本右间距
     */
    int32_t suffix_text_margin_right;

    /**
     * @property {int32_t} suffix_text_margin_top
     * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
     * 后缀文本上间距
     */
    int32_t suffix_text_margin_top;

    /**
     * @property {int32_t} suffix_text_margin_bottom
     * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
     * 后缀文本下间距
     */
    int32_t suffix_text_margin_bottom;


  /**
   * @property {str_t} font_name
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 文本字体名称
   */
  str_t font_name;

 /**
  * @property {str_t} prefix_font_name
  * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
  * 前缀文本字体名称
  */
  str_t prefix_font_name;

   /**
  * @property {str_t} suffix_font_name
  * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
  * 后缀文本字体名称
  */
  str_t suffix_font_name;

   /**
  * @property {str_t} center_text
  * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
  * 中间部分显示的文本
  */
  str_t center_text;

 /**
  * @property {str_t} prefix_text
  * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
  * 前缀文本值
  */
  str_t prefix_text;

   /**
  * @property {str_t} suffix_text
  * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
  * 后缀文本值
  */
  str_t suffix_text;


} yps_widget_label_t;

/**
 * @method yps_widget_label_create
 * @annotation ["constructor", "scriptable"]
 * 创建yps_widget_label对象
 * @param {widget_t*} parent 父控件
 * @param {xy_t} x x坐标
 * @param {xy_t} y y坐标
 * @param {wh_t} w 宽度
 * @param {wh_t} h 高度
 *
 * @return {widget_t*} yps_widget_label对象。
 */
widget_t* yps_widget_label_create(widget_t* parent, xy_t x, xy_t y, wh_t w,
                                  wh_t h);

/**
 * @method yps_widget_label_cast
 * 转换为yps_widget_label对象(供脚本语言使用)。
 * @annotation ["cast", "scriptable"]
 * @param {widget_t*} widget yps_widget_label对象。
 *
 * @return {widget_t*} yps_widget_label对象。
 */
widget_t* yps_widget_label_cast(widget_t* widget);


/**
 * @method yps_widget_label_set_text_font_size
 * 设置 。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} font_size 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_text_font_size(widget_t* widget,
                                           int32_t font_size);

/**
 * @method yps_widget_label_set_prefix_text_font_size
 * 设置 。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} font_size 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_prefix_text_font_size(widget_t* widget,
                                           int32_t font_size);

/**
 * @method yps_widget_label_set_suffix_text_font_size
 * 设置 。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} font_size 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_suffix_text_font_size(widget_t* widget,
                                           int32_t font_size);

/**
 * @method yps_widget_label_set_text_font_color
 * 设置文本字体颜色 。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} font_color 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_text_font_color(widget_t* widget,
                                           char* font_color);

/**
 * @method yps_widget_label_set_prefix_text_font_color
 * 设置前缀文本字体颜色 。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} font_color 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_prefix_text_font_color(widget_t* widget,
                                           char* font_color);

/**
 * @method yps_widget_label_set_suffix_text_font_color
 * 设置前缀文本字体颜色 。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} font_color 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_suffix_text_font_color(widget_t* widget,
                                           char* font_color);

/**
 * @method yps_widget_label_set_text_width
 * 设置文本宽度 。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} text_width 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_text_width(widget_t* widget,
                                           int32_t text_width);

/**
 * @method yps_widget_label_set_prefix_text_width
 * 设置前缀文本宽度 。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} text_width 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_prefix_text_width(widget_t* widget,
                                           int32_t text_width);

/**
 * @method yps_widget_label_set_suffix_text_width
 * 设置后缀文本宽度 。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} text_width 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_suffix_text_width(widget_t* widget,
                                           int32_t text_width);

/**
 * @method yps_widget_label_set_text_height
 * 设置文本高度 。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} text_height 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_text_height(widget_t* widget,
                                           int32_t text_height);

/**
 * @method yps_widget_label_set_prefix_text_height
 * 设置前缀文本高度  。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} text_height 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_prefix_text_height(widget_t* widget,
                                           int32_t text_height);

/**
 * @method yps_widget_label_set_suffix_text_height
 * 设置后缀文本高度 。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} text_height 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_suffix_text_height(widget_t* widget,
                                           int32_t text_height);

/**
 * @method yps_widget_label_set_text_align
 * 设置文本对齐方式 支持左对齐（left），右对齐（right），居中对齐（center）
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {const char*} text_align 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_text_align(widget_t* widget,
                                           const char* text_align);

/**
 * @method yps_widget_label_set_prefix_text_align
 * 设置前缀文本对齐方式 支持左对齐（left），右对齐（right），居中对齐（center）
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {const char*} text_align 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_prefix_text_align(widget_t* widget,
                                           const char* text_align);

/**
 * @method yps_widget_label_set_suffix_text_align
 * 设置后缀文本对齐方式 支持左对齐（left），右对齐（right），居中对齐（center）
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {const char*} text_align 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_suffix_text_align(widget_t* widget,
                                           const char* text_align);

/**
 * @method yps_widget_label_set_text_margin_left
 * 设置中间文本左边距
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} text_margin_left 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_text_margin_left(widget_t* widget,
                                           int32_t text_margin_left);

/**
 * @method yps_widget_label_set_text_margin_right
 * 设置中间文本右边距
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} text_margin_right 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_text_margin_right(widget_t* widget,
                                           int32_t text_margin_right);

/**
 * @method yps_widget_label_set_text_margin_top
 * 设置中间jian文本上边距
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} text_margin_top 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_text_margin_top(widget_t* widget,
                                           int32_t text_margin_top);

/**
 * @method yps_widget_label_set_text_margin_bottom
 * 设置中间文本下边距
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} text_margin_bottom 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_text_margin_bottom(widget_t* widget,
                                           int32_t text_margin_bottom);


/**
 * @method yps_widget_label_set_prefix_text_margin_left
 * 设置前缀文本左边距
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} prefix_text_margin_left 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_prefix_text_margin_left(widget_t* widget,
                                           int32_t prefix_text_margin_left);

/**
 * @method yps_widget_label_set_prefix_text_margin_right
 * 设置前缀文本右边距
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} prefix_text_margin_right 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_prefix_text_margin_right(widget_t* widget,
                                           int32_t prefix_text_margin_right);

/**
 * @method yps_widget_label_set_prefix_text_margin_top
 * 设置前缀文本上边距
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} prefix_text_margin_top 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_prefix_text_margin_top(widget_t* widget,
                                           int32_t prefix_text_margin_top);

/**
 * @method yps_widget_label_set_prefix_text_margin_bottom
 * 设置设置前缀文本下边距
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} text_margin_bottom 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_prefix_text_margin_bottom(widget_t* widget,
                                           int32_t prefix_text_margin_bottom);

/**
 * @method yps_widget_label_set_suffix_text_margin_left
 * 设置后缀文本左边距
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} suffix_text_margin_left 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_suffix_text_margin_left(widget_t* widget,
                                           int32_t suffix_text_margin_left);

/**
 * @method yps_widget_label_set_suffix_text_margin_right
 * 设置后缀文本右边距
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} suffix_text_margin_right 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_suffix_text_margin_right(widget_t* widget,
                                           int32_t suffix_text_margin_right);

/**
 * @method yps_widget_label_set_suffix_text_margin_top
 * 设置后缀文本上边距
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} suffix_text_margin_top 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_suffix_text_margin_top(widget_t* widget,
                                           int32_t suffix_text_margin_top);

/**
 * @method yps_widget_label_set_suffix_text_margin_bottom
 * 设置
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {int32_t} suffix_text_margin_bottom 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_suffix_text_margin_bottom(widget_t* widget,
                                           int32_t suffix_text_margin_bottom);


/**
 * @method yps_widget_label_set_font_name
 * 设置
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {const char*} font_name 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_font_name(widget_t* widget,
                                           const char* font_name);

/**
 * @method yps_widget_label_set_prefix_font_name
 * 设置
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {const char*} prefix_font_name 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_prefix_font_name(widget_t* widget,
                                           const char* prefix_font_name);

/**
 * @method yps_widget_label_set_suffix_font_name
 * 设置
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {const char*} suffix_font_name 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_suffix_font_name(widget_t* widget,
                                           const char* suffix_font_name);

/**
 * @method yps_widget_label_set_center_text
 * 设置中间部分文本
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {const char*} center_text 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_center_text(widget_t* widget,
                                           const char* center_text);          

/**
 * @method yps_widget_label_set_prefix_text
 * 设置
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {const char*} prefix_text 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_prefix_text(widget_t* widget,
                                           const char* prefix_text);                            

/**
 * @method yps_widget_label_set_suffix_text
 * 设置
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {const char*} suffix_text 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_set_suffix_text(widget_t* widget,
                                           const char* suffix_text);

/**
 * @method yps_widget_label_bind_text
 * 设置中间文本绑定的值
 * @annotation []
 * @param {widget_t*} widget widget对象。
 * @param {str_t*} text，需要绑定的文本变量 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_bind_text(widget_t* widget,
                                           str_t* text);

/**
 * @method yps_widget_label_bind_prefix_text
 * 设置前缀文本绑定的值
 * @annotation []
 * @param {widget_t*} widget widget对象。
 * @param {str_t*} prefix_text，需要绑定的文本变量 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_bind_prefix_text(widget_t* widget,
                                           str_t* prefix_text);

/**
 * @method yps_widget_label_bind_suffix_text
 * 设置后缀文本绑定的值
 * @annotation []
 * @param {widget_t*} widget widget对象。
 * @param {str_t*} suffix_text，需要绑定的文本变量 。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_bind_suffix_text(widget_t* widget,
                                           str_t* suffix_text);


#define WIDGET_TYPE_YPS_WIDGET_LABEL "yps_widget_label"

#define YPS_WIDGET_LABEL(widget) \
  ((yps_widget_label_t*)(yps_widget_label_cast(WIDGET(widget))))

    /*public for subclass and runtime type check*/
    TK_EXTERN_VTABLE(yps_widget_label);

END_C_DECLS

#endif /*TK_YPS_WIDGET_LABEL_H*/
