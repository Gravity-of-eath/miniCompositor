/**
 * File:   yps_widget_ico_ex.c
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


#include "tkc/mem.h"
#include "tkc/utils.h"
#include "base/enums.h"
#include "yps_widget_ico_ex.h"
#include <tkc/str.h>
#include <widgets/image.h>
#include <stdio.h>

ret_t yps_widget_ico_ex_set_image_list(widget_t* widget, const char* image_list) {
  yps_widget_ico_ex_t* yps_widget_ico_ex = YPS_WIDGET_ICO_EX(widget);
  return_value_if_fail(yps_widget_ico_ex != NULL, RET_BAD_PARAMS);
  str_set(&(yps_widget_ico_ex->image_list), image_list);
  return RET_OK;
}

ret_t yps_widget_ico_ex_set_image_id(widget_t* widget, int image_id){
  yps_widget_ico_ex_t* yps_widget_ico_ex = YPS_WIDGET_ICO_EX(widget);
  return_value_if_fail(yps_widget_ico_ex != NULL, RET_BAD_PARAMS);
  if (yps_widget_ico_ex->image_id != image_id) {
      yps_widget_ico_ex->image_id = image_id;
      widget_invalidate(widget, NULL);
  }
  return RET_OK;
}

int yps_widget_ico_ex_get_image_id(widget_t* widget) {
    yps_widget_ico_ex_t* yps_widget_ico_ex = YPS_WIDGET_ICO_EX(widget);
    return_value_if_fail(yps_widget_ico_ex != NULL, RET_BAD_PARAMS);
    return  yps_widget_ico_ex->image_id;
}


static ret_t yps_widget_ico_ex_get_prop(widget_t* widget, const char* name, value_t* v) {
  yps_widget_ico_ex_t* yps_widget_ico_ex = YPS_WIDGET_ICO_EX(widget);
  return_value_if_fail(yps_widget_ico_ex != NULL && name != NULL && v != NULL, RET_BAD_PARAMS);

  if (tk_str_eq(YPS_WIDGET_ICO_EX_PROP_IMAGE_LIST, name)) {
     value_set_str(v, yps_widget_ico_ex->image_list.str);
     return RET_OK;
  }else if(tk_str_eq(YPS_WIDGET_ICO_EX_PROP_IMAGE_ID, name)){
    value_set_int(v, yps_widget_ico_ex->image_id);
    return RET_OK;
  }

  return RET_NOT_FOUND;
}

static ret_t yps_widget_ico_ex_set_prop(widget_t* widget, const char* name, const value_t* v) {
//   yps_widget_ico_ex_t* yps_widget_ico_ex = YPS_WIDGET_ICO_EX(widget);
  return_value_if_fail(widget != NULL && name != NULL && v != NULL, RET_BAD_PARAMS);

 
  if (tk_str_eq(YPS_WIDGET_ICO_EX_PROP_IMAGE_LIST, name)) {
     yps_widget_ico_ex_set_image_list(widget, value_str(v));
     return RET_OK;
  }
  else if(tk_str_eq(YPS_WIDGET_ICO_EX_PROP_IMAGE_ID, name)){
    yps_widget_ico_ex_set_image_id(widget, value_int(v));
    return RET_OK;
  }

  return RET_NOT_FOUND;
}

static ret_t yps_widget_ico_ex_on_destroy(widget_t* widget) {
  yps_widget_ico_ex_t* yps_widget_ico_ex = YPS_WIDGET_ICO_EX(widget);
  return_value_if_fail(widget != NULL && yps_widget_ico_ex != NULL, RET_BAD_PARAMS);

  str_reset(&(yps_widget_ico_ex->image_list));
  return RET_OK;
}

//绘制背景图
static ret_t draw_background_image(widget_t* widget, canvas_t* c, char* image_name){
  yps_widget_ico_ex_t* yps_widget_ico_ex = YPS_WIDGET_ICO_EX(widget);
  bitmap_t bitmap;
  rect_t dst;
  image_manager_t* imm = widget_get_image_manager(widget);
  return_value_if_fail(imm != NULL, RET_BAD_PARAMS);
  return_value_if_fail(widget != NULL && image_name != NULL , RET_BAD_PARAMS);
  if(image_manager_get_bitmap(imm, image_name, &bitmap) == RET_OK) {
      dst = rect_init(0, 0, widget->w, widget->h);
      return canvas_draw_image_ex(c, &bitmap, IMAGE_DRAW_SCALE_AUTO, &dst);
  }
  else {
        log_debug("image %s not found\n", image_name);
  }

    return RET_FAIL;
}

static ret_t yps_widget_ico_ex_on_paint_self(widget_t* widget, canvas_t* c) {
  yps_widget_ico_ex_t* yps_widget_ico_ex = YPS_WIDGET_ICO_EX(widget);
  char image_name[128] = {0};
  sprintf(image_name, "%s%d", yps_widget_ico_ex->image_list.str, yps_widget_ico_ex->image_id);
  draw_background_image(widget, c, image_name);
  return RET_OK;
}

static ret_t yps_widget_ico_ex_on_event(widget_t* widget, event_t* e) {
  yps_widget_ico_ex_t* yps_widget_ico_ex = YPS_WIDGET_ICO_EX(widget);
  return_value_if_fail(widget != NULL && yps_widget_ico_ex != NULL, RET_BAD_PARAMS);

  return RET_OK;
}

const char* s_yps_widget_ico_ex_properties[] = {
  YPS_WIDGET_ICO_EX_PROP_IMAGE_LIST,
  YPS_WIDGET_ICO_EX_PROP_IMAGE_ID,
  NULL
};

TK_DECL_VTABLE(yps_widget_ico_ex) = {.size = sizeof(yps_widget_ico_ex_t),
                          .type = WIDGET_TYPE_YPS_WIDGET_ICO_EX,
                          .clone_properties = s_yps_widget_ico_ex_properties,
                          .persistent_properties = s_yps_widget_ico_ex_properties,
                          .parent = TK_PARENT_VTABLE(widget),
                          .create = yps_widget_ico_ex_create,
                          .on_paint_self = yps_widget_ico_ex_on_paint_self,
                          .set_prop = yps_widget_ico_ex_set_prop,
                          .get_prop = yps_widget_ico_ex_get_prop,
                          .on_event = yps_widget_ico_ex_on_event,
                          .on_destroy = yps_widget_ico_ex_on_destroy};

widget_t* yps_widget_ico_ex_create(widget_t* parent, xy_t x, xy_t y, wh_t w, wh_t h) {
  widget_t* widget = widget_create(parent, TK_REF_VTABLE(yps_widget_ico_ex), x, y, w, h);
  yps_widget_ico_ex_t* yps_widget_ico_ex = YPS_WIDGET_ICO_EX(widget);
  return_value_if_fail(yps_widget_ico_ex != NULL, NULL);

  rect_t widget_rect = widget_get_content_area(widget);

  yps_widget_ico_ex->widget_ico = image_create(widget, 0, 0, widget_rect.w, widget_rect.h);

  str_set(&(yps_widget_ico_ex->image_list), "");
  return widget;
}

widget_t* yps_widget_ico_ex_cast(widget_t* widget) {
    bool ret = WIDGET_IS_INSTANCE_OF(widget, yps_widget_ico_ex);
    if (!ret) {
        printf("控件 %s 不是yps_widget_ico_ex类型 \n", widget->name);
    }
    return_value_if_fail(ret, NULL);
  return widget;
}

