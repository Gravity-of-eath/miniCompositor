 

#ifndef TK_LIST_ROW_H
#define TK_LIST_ROW_H

#include "base/widget.h"

BEGIN_C_DECLS
/**
 * @class list_row_t
 * @parent widget_t
 * @annotation ["scriptable","design","widget"]
 * table\_row。表示表格的一行。
 *
 * 它本身不提供布局功能，仅提供具有语义的标签，让xml更具有可读性。
 * 子控件的布局可用layout\_children属性指定。
 * 请参考[布局参数](https://github.com/zlgopen/awtk/blob/master/docs/layout.md)。
 *
 * table\_row\_t是[widget\_t](widget_t.md)的子类控件，widget\_t的函数均适用于table\_row\_t控件。
 *
 * 在xml中使用"table\_row"标签创建table\_row。
 * 
 * table\_row一般放在table\_client对象中，创建一个对象即可，table\_client以此为模版，根据需要创建table\_row对象。
 * 
 * 如：
 *
 * ```xml
 * <!-- ui -->
 * <list_row x="0" y="0" w="200" h="30"/>
 * ```
 *
 * 可用通过style来设置控件的显示风格，如背景颜色等。如：
 *
 * ```xml
 * <!-- style -->
 * <list_row>
 *   <style name="default" border_color="#d8d8d8" border="bottom">
 *     <normal bg_color="#fcfcfc"/>
 *   </style>
 * </list_row>
 * ```
 */
typedef struct _list_row_t {
  widget_t widget;

  /**
   * @property {uint32_t} index
   * @annotation ["set_prop","get_prop","readable","persitent","design","scriptable"]
   * 行的编号。
   */
  uint32_t index;
  uint32_t view_type;

} list_row_t;

/**
 * @method list_row_create
 * @annotation ["constructor", "scriptable"]
 * 创建list_row对象
 * @param {widget_t*} parent 父控件
 * @param {xy_t} x x坐标
 * @param {xy_t} y y坐标
 * @param {wh_t} w 宽度
 * @param {wh_t} h 高度
 *
 * @return {widget_t*} list_row对象。
 */
widget_t* list_row_create(widget_t* parent, xy_t x, xy_t y, wh_t w, wh_t h);

/**
 * @method list_row_cast
 * 转换为list_row对象(供脚本语言使用)。
 * @annotation ["cast", "scriptable"]
 * @param {widget_t*} widget list_row对象。
 *
 * @return {widget_t*} list_row对象。
 */
widget_t* list_row_cast(widget_t* widget);

/**
 * @method list_row_set_index
 * 设置 行的编号。
 * @annotation ["scriptable"]
 * @param {widget_t*} widget widget对象。
 * @param {uint32_t} index 行的编号。
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t list_row_set_index(widget_t* widget, uint32_t index);

ret_t list_row_set_view_type(widget_t* widget, uint32_t view_type);


uint32_t list_row_get_index(widget_t* widget);

uint32_t list_row_get_view_type(widget_t* widget);

#define LIST_ROW_PROP_INDEX "index"
#define LIST_ROW_PROP_TYPE "view_type"

#define WIDGET_TYPE_LIST_ROW "list_row"

#define LIST_ROW(widget) ((list_row_t*)(list_row_cast(WIDGET(widget))))

/*public for subclass and runtime type check*/
TK_EXTERN_VTABLE(list_row);

END_C_DECLS

#endif /*TK_LIST_ROW_H*/
