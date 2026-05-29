/**
 * File:   spine2d.c
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

#include "tkc/mem.h"
#include "tkc/utils.h"
#include "tkc/tokenizer.h"
#include "tkc/time_now.h"

#include "spine2d.h"
#include "spine_gl.h"
#include "stdio.h"

using namespace spine;

typedef struct _skeleton_info_t {
  Atlas* atlas;
  SkeletonData* skeletonData;
  Skeleton* skeleton;
  AnimationStateData* animationStateData;
  AnimationState* animationState;
  renderer_t* renderer;
  double last_time;
} skeleton_info_t;

static ret_t spine2d_disptach_event(widget_t* widget, uint32_t type, TrackEntry* entry) {
  widget_animator_event_t e;
  widget_animator_t animator;
  const char* name = entry->getAnimation()->getName().buffer();

  memset(&animator, 0x00, sizeof(animator));
  animator.widget = widget;
  widget_animator_set_name(&animator, name);
  widget_animator_event_init(&e, type, widget, &animator);
  widget_dispatch(widget, (event_t*)&e);
  TKMEM_FREE(animator.name);

  return RET_OK;
}

class MyAnimationStateListenerObject : public AnimationStateListenerObject {
 public:
  MyAnimationStateListenerObject(widget_t* widget) {
    this->widget = widget;
  }
  void callback(AnimationState* state, EventType type, TrackEntry* entry, Event* event) {
    spine2d_t* spine2d = SPINE2D(widget);

    if (type == EventType_Complete) {
      if (spine2d->loop) {
        spine2d_disptach_event(widget, EVT_ANIM_ONCE, entry);
      } else {
        spine2d_disptach_event(widget, EVT_ANIM_END, entry);
      }
    } else if (type == EventType_Start) {
      spine2d_disptach_event(widget, EVT_ANIM_START, entry);
    } else {
      // log_debug("type=%d\n", type);
    }
  }

  widget_t* widget;
};

static ret_t animation_state_set_names(AnimationState* animationState, const char* names,
                                       bool_t loop) {
  tokenizer_t t;
  return_value_if_fail(animationState != NULL && names != NULL, RET_BAD_PARAMS);

  if (strchr(names, ',') != NULL) {
    uint32_t i = 0;
    tokenizer_init(&t, names, tk_strlen(names), ",");
    while (tokenizer_has_more(&t)) {
      const char* name = tokenizer_next(&t);
      if (i == 0) {
        animationState->setAnimation(0, name, loop);
      } else {
        animationState->addAnimation(0, name, loop, 0);
      }
      i++;
    }
    tokenizer_deinit(&t);
  } else {
    animationState->setAnimation(0, names, loop);
  }

  return RET_OK;
}

static ret_t skeleton_update_position_size(widget_t* widget, Skeleton* skeleton) {
  point_t p = {0, 0};
  spine2d_t* spine2d = SPINE2D(widget);
  widget_t* wm = widget_get_window_manager(widget);
  return_value_if_fail(spine2d != NULL, RET_BAD_PARAMS);

  widget_to_global(widget, &p);
  float x = p.x + widget->w / 2;
  float y = p.y + widget->h;
  float scale_x = (float)(spine2d->scale_x * widget->w) / (float)(wm->w);
  float scale_y = (float)(spine2d->scale_y * widget->h) / (float)(wm->h);

  skeleton->setPosition(x, y);
  skeleton->setScaleX(scale_x);
  skeleton->setScaleY(scale_y);
  printf("skeleton_update_position_size scaX=%f scaY=%f",scale_x,scale_y);
  return RET_OK;
}
#include "tkc/fs.h"

static asset_info_t* load_asset_from_file(uint16_t type, uint16_t subtype, const char* path,
                                          const char* name) {
  asset_info_t* info = NULL;
  if (file_exist(path)) {
    int32_t size = file_get_size(path);
    info = asset_info_create(type, subtype, name, size);
    return_value_if_fail(info != NULL, NULL);

    ENSURE(file_read_part(path, info->data, size, 0) == size);
  }

  return info;
}


static char* get_dir_from_path(const char* path) {
    if (!path) return NULL;
    
    const char* slash = strrchr(path, '/');
    if (!slash) return strdup(".");
    
    size_t len = slash - path;
    if (len == 0) return strdup("/");
    
    char* dir = (char*)TKMEM_ALLOC(len + 1);
    if (dir) {
        strncpy(dir, path, len);
        dir[len] = '\0';
    }
    return dir;
}

static skeleton_info_t* skeleton_info_create(widget_t* widget) {
  spine2d_t* spine2d = SPINE2D(widget);
  asset_info_t* asset_atlas = NULL;
  asset_info_t* asset_skel = NULL;
  widget_t* wm = widget_get_window_manager(widget);
  return_value_if_fail(spine2d != NULL, NULL);

  const char* atlas_file = spine2d->atlas;
  const char* skel_file = spine2d->skeleton;
  return_value_if_fail(atlas_file != NULL && skel_file != NULL, NULL);

  skeleton_info_t* info = NULL;
  info = TKMEM_ZALLOC(skeleton_info_t);
  return_value_if_fail(info != NULL, NULL);

  char flag = '/';
  GlTextureLoader textureLoader;
   Atlas* atlas;
  if (strchr(atlas_file, flag) != NULL) {
    asset_atlas = load_asset_from_file(ASSET_TYPE_DATA, 0, atlas_file, atlas_file);
    char *lastForwardSlash = get_dir_from_path(atlas_file);
    printf("atlas_file use outter data <%s> out path=%s\n", atlas_file,lastForwardSlash);
    atlas= new Atlas((const char*)asset_atlas->data, asset_atlas->size, "", &textureLoader);
  } else {
    printf("atlas_file use assets data <%s>\n", atlas_file);
    asset_atlas = assets_manager_load(assets_manager(), ASSET_TYPE_DATA, atlas_file);
    atlas= new Atlas((const char*)asset_atlas->data, asset_atlas->size, "", &textureLoader);
  }

  asset_info_unref(asset_atlas);

  SkeletonBinary binary(atlas);
  if (strchr(atlas_file, flag) != NULL) {
    printf("skel_file use outter data <%s>\n", skel_file);
    asset_skel = load_asset_from_file(ASSET_TYPE_DATA, 0, skel_file, skel_file);
    printf("skel_file outter asset_skel=%p\n", asset_skel);
  } else {
    printf("skel_file use assets data <%s>\n", skel_file);
    asset_skel = assets_manager_load(assets_manager(), ASSET_TYPE_DATA, skel_file);
    printf("skel_file assets asset_skel=%p\n", asset_skel);
  }

  printf("skel_file name=%s  size=%d asset_skel->data=%d\n", asset_skel->name, asset_skel->size,
         asset_skel->data[0]);
  SkeletonData* skeletonData = binary.readSkeletonData(asset_skel->data, asset_skel->size);
  if (skeletonData == NULL) {
    printf("spine2d: readSkeletonData failed: %s\n", binary.getError().buffer());
    asset_info_unref(asset_skel);
    delete atlas;
    TKMEM_FREE(info);
    return NULL;
  }
  asset_info_unref(asset_skel);

  Skeleton* skeleton = new Skeleton(skeletonData);

  skeleton_update_position_size(widget, skeleton);
  AnimationStateData* animationStateData = new AnimationStateData(skeletonData);
  animationStateData->setDefaultMix(0.2f);
  AnimationState* animationState = new AnimationState(animationStateData);
  animationState->setListener(new MyAnimationStateListenerObject(widget));

  if (TK_STR_IS_NOT_EMPTY(spine2d->action)) {
    spine2d->skeleton_info = info;
    animation_state_set_names(animationState, spine2d->action, spine2d->loop);
  }
  animationState->setTimeScale(spine2d->scale_time);

  info->atlas = atlas;
  info->skeleton = skeleton;
  info->skeletonData = skeletonData;
  info->animationState = animationState;
  info->animationStateData = animationStateData;
  info->renderer = renderer_create();
  renderer_set_viewport_size(info->renderer, wm->w, wm->h);
  info->last_time = time_now_ms() / 1000.0f;

  return info;
}
static ret_t skeleton_info_update(skeleton_info_t* info) {
  double now = time_now_ms() / 1000.0f;
  return_value_if_fail(info != NULL, RET_BAD_PARAMS);
  float delta = now - info->last_time;
  info->last_time = now;
  // Update and apply the animation state to the skeleton
  info->animationState->update(delta);
  info->animationState->apply(*(info->skeleton));

  // Update the skeleton time (used for physics)
  info->skeleton->update(delta);

  // Calculate the new pose
  info->skeleton->updateWorldTransform();

  return RET_OK;
}

static ret_t skeleton_info_draw(skeleton_info_t* info) {
  renderer_draw(info->renderer, info->skeleton, true);
  return RET_OK;
}

static ret_t skeleton_info_destroy(skeleton_info_t* info) {
  return_value_if_fail(info != NULL, RET_BAD_PARAMS);

  delete info->animationState;
  delete info->animationStateData;
  delete info->skeleton;
  delete info->skeletonData;
  renderer_dispose(info->renderer);

  TKMEM_FREE(info);

  return RET_OK;
}

static ret_t spine2d_on_update_timer(const timer_info_t* timer) {
  widget_t* widget = WIDGET(timer->ctx);
  spine2d_t* spine2d = SPINE2D(widget);
  return_value_if_fail(spine2d != NULL, RET_BAD_PARAMS);

  skeleton_info_update((skeleton_info_t*)spine2d->skeleton_info);

  widget_invalidate(widget, NULL);

  return RET_REPEAT;
}

static ret_t spine2d_create_skeleton(widget_t* widget) {
  spine2d_t* spine2d = SPINE2D(widget);
  return_value_if_fail(spine2d != NULL, RET_BAD_PARAMS);

  Bone::setYDown(true);
  try {
    spine2d->skeleton_info = skeleton_info_create(widget);
  } catch (const std::exception& e) {
    log_error("%s\n", e.what());
  }

  skeleton_info_update((skeleton_info_t*)spine2d->skeleton_info);
  spine2d->timer_id = timer_add(spine2d_on_update_timer, widget, 16);

  return RET_OK;
}

ret_t spine2d_set_atlas(widget_t* widget, const char* atlas) {
  spine2d_t* spine2d = SPINE2D(widget);
  return_value_if_fail(spine2d != NULL, RET_BAD_PARAMS);

  spine2d->atlas = tk_str_copy(spine2d->atlas, atlas);
  /*不支持运行时修改*/
  assert(spine2d->skeleton_info == NULL);

  return RET_OK;
}

ret_t spine2d_set_skeleton(widget_t* widget, const char* skeleton) {
  spine2d_t* spine2d = SPINE2D(widget);
  return_value_if_fail(spine2d != NULL, RET_BAD_PARAMS);

  spine2d->skeleton = tk_str_copy(spine2d->skeleton, skeleton);
  /*不支持运行时修改*/
  assert(spine2d->skeleton_info == NULL);

  return RET_OK;
}

ret_t spine2d_set_action(widget_t* widget, const char* action) {
  spine2d_t* spine2d = SPINE2D(widget);
  return_value_if_fail(spine2d != NULL, RET_BAD_PARAMS);

  spine2d->action = tk_str_copy(spine2d->action, action);
  if (spine2d->skeleton_info != NULL) {
    skeleton_info_t* info = (skeleton_info_t*)spine2d->skeleton_info;
    animation_state_set_names(info->animationState, spine2d->action, spine2d->loop);
  }

  return RET_OK;
}

ret_t spine2d_set_scale_x(widget_t* widget, float scale_x) {
  spine2d_t* spine2d = SPINE2D(widget);
  return_value_if_fail(spine2d != NULL, RET_BAD_PARAMS);

  spine2d->scale_x = scale_x;
  if (spine2d->skeleton_info != NULL) {
    skeleton_info_t* info = (skeleton_info_t*)spine2d->skeleton_info;
    skeleton_update_position_size(widget, info->skeleton);
  }

  return RET_OK;
}

ret_t spine2d_set_scale_y(widget_t* widget, float scale_y) {
  spine2d_t* spine2d = SPINE2D(widget);
  return_value_if_fail(spine2d != NULL, RET_BAD_PARAMS);

  spine2d->scale_y = scale_y;
  if (spine2d->skeleton_info != NULL) {
    skeleton_info_t* info = (skeleton_info_t*)spine2d->skeleton_info;
    skeleton_update_position_size(widget, info->skeleton);
  }

  return RET_OK;
}

ret_t spine2d_set_scale_time(widget_t* widget, float scale_time) {
  spine2d_t* spine2d = SPINE2D(widget);
  return_value_if_fail(spine2d != NULL, RET_BAD_PARAMS);

  spine2d->scale_time = scale_time;
  if (spine2d->skeleton_info != NULL) {
    skeleton_info_t* info = (skeleton_info_t*)spine2d->skeleton_info;
    info->animationState->setTimeScale(scale_time);
  }

  return RET_OK;
}

ret_t spine2d_set_loop(widget_t* widget, bool_t loop) {
  spine2d_t* spine2d = SPINE2D(widget);
  return_value_if_fail(spine2d != NULL, RET_BAD_PARAMS);

  spine2d->loop = loop;

  return RET_OK;
}

static ret_t spine2d_get_prop(widget_t* widget, const char* name, value_t* v) {
  spine2d_t* spine2d = SPINE2D(widget);
  return_value_if_fail(spine2d != NULL && name != NULL && v != NULL, RET_BAD_PARAMS);

  if (tk_str_eq(SPINE2D_PROP_ATLAS, name)) {
    value_set_str(v, spine2d->atlas);
    return RET_OK;
  } else if (tk_str_eq(SPINE2D_PROP_SKELETON, name)) {
    value_set_str(v, spine2d->skeleton);
    return RET_OK;
  } else if (tk_str_eq(SPINE2D_PROP_ACTION, name)) {
    value_set_str(v, spine2d->action);
    return RET_OK;
  } else if (tk_str_eq(SPINE2D_PROP_SCALE_X, name)) {
    value_set_float(v, spine2d->scale_x);
    return RET_OK;
  } else if (tk_str_eq(SPINE2D_PROP_SCALE_Y, name)) {
    value_set_float(v, spine2d->scale_y);
    return RET_OK;
  } else if (tk_str_eq(SPINE2D_PROP_SCALE_TIME, name)) {
    value_set_float(v, spine2d->scale_time);
    return RET_OK;
  } else if (tk_str_eq(SPINE2D_PROP_LOOP, name)) {
    value_set_bool(v, spine2d->loop);
    return RET_OK;
  }

  return RET_NOT_FOUND;
}

static ret_t spine2d_set_prop(widget_t* widget, const char* name, const value_t* v) {
  log_debug("spine2d_set_prop*************************name=%s**************************\n",name);
  return_value_if_fail(widget != NULL && name != NULL && v != NULL, RET_BAD_PARAMS);

  if (tk_str_eq(SPINE2D_PROP_ATLAS, name)) {
    spine2d_set_atlas(widget, value_str(v));
    return RET_OK;
  } else if (tk_str_eq(SPINE2D_PROP_SKELETON, name)) {
    spine2d_set_skeleton(widget, value_str(v));
    return RET_OK;
  } else if (tk_str_eq(SPINE2D_PROP_ACTION, name)) {
    spine2d_set_action(widget, value_str(v));
    return RET_OK;
  } else if (tk_str_eq(SPINE2D_PROP_SCALE_X, name)) {
    spine2d_set_scale_x(widget, value_float(v));
    return RET_OK;
  } else if (tk_str_eq(SPINE2D_PROP_SCALE_Y, name)) {
    spine2d_set_scale_y(widget, value_float(v));
    return RET_OK;
  } else if (tk_str_eq(SPINE2D_PROP_SCALE_TIME, name)) {
    spine2d_set_scale_time(widget, value_float(v));
    return RET_OK;
  } else if (tk_str_eq(SPINE2D_PROP_LOOP, name)) {
    spine2d_set_loop(widget, value_bool(v));
    return RET_OK;
  }

  return RET_NOT_FOUND;
}

static ret_t spine2d_on_destroy(widget_t* widget) {
  spine2d_t* spine2d = SPINE2D(widget);
  return_value_if_fail(widget != NULL && spine2d != NULL, RET_BAD_PARAMS);

  TKMEM_FREE(spine2d->atlas);
  TKMEM_FREE(spine2d->skeleton);
  TKMEM_FREE(spine2d->action);
  timer_remove(spine2d->timer_id);

  if (spine2d->skeleton_info != NULL) {
    skeleton_info_destroy((skeleton_info_t*)spine2d->skeleton_info);
  }
  return RET_OK;
}

static ret_t spine2d_on_paint_self(widget_t* widget, canvas_t* c) {
  spine2d_t* spine2d = SPINE2D(widget);
  // log_debug("spine2d_on_paint_self atlas=%s  skeleton=%s\n", spine2d->atlas, spine2d->skeleton);
  if (spine2d == NULL || spine2d->atlas == NULL || tk_str_eq(spine2d->atlas, "") ||
      spine2d->skeleton == NULL || tk_str_eq(spine2d->skeleton, "")) {
    log_debug(
        "spine2d_on_paint_self***************spine2d->skeleton or spine2d->atlas is "
        "null************************************\n");
    return RET_OK;
  }
  vgcanvas_t* vg = canvas_get_vgcanvas(c);
  return_value_if_fail(widget != NULL && spine2d != NULL, RET_BAD_PARAMS);
  return_value_if_fail(vg != NULL, RET_BAD_PARAMS);

  if (spine2d->skeleton_info == NULL) {
    if (spine2d->atlas != NULL && spine2d->skeleton != NULL) {
      spine2d_create_skeleton(widget);
    }
  }

  if (spine2d->skeleton_info != NULL) {
    vgcanvas_flush(vg);
    skeleton_info_draw((skeleton_info_t*)spine2d->skeleton_info);
  }

  return RET_OK;
}

static ret_t spine2d_on_event(widget_t* widget, event_t* e) {
  spine2d_t* spine2d = SPINE2D(widget);
  return_value_if_fail(widget != NULL && spine2d != NULL, RET_BAD_PARAMS);
  if (spine2d == NULL || spine2d->atlas == NULL || tk_str_eq(spine2d->atlas, "") ||
      spine2d->skeleton == NULL || tk_str_eq(spine2d->skeleton, "")) {
    return RET_OK;
  }
  switch (e->type) {
    case EVT_MOVE:
    case EVT_MOVE_RESIZE:
    case EVT_RESIZE: {
      skeleton_info_t* info = (skeleton_info_t*)spine2d->skeleton_info;
      if (info != NULL) {
        skeleton_update_position_size(widget, info->skeleton);
      }
      break;
    }
    default:
      break;
  }
  (void)spine2d;

  return RET_OK;
}

const char* s_spine2d_properties[] = {
    SPINE2D_PROP_ATLAS,   SPINE2D_PROP_SKELETON,   SPINE2D_PROP_ACTION, SPINE2D_PROP_SCALE_X,
    SPINE2D_PROP_SCALE_Y, SPINE2D_PROP_SCALE_TIME, SPINE2D_PROP_LOOP,   NULL};

// TK_DECL_VTABLE(spine2d) = {.size = sizeof(spine2d_t),
//                            .type = WIDGET_TYPE_SPINE2D,
//                            .clone_properties = s_spine2d_properties,
//                            .persistent_properties = s_spine2d_properties,
//                            .parent = TK_PARENT_VTABLE(widget),
//                            .create = spine2d_create,
//                            .get_prop = spine2d_get_prop,
//                            .set_prop = spine2d_set_prop,
//                            .on_paint_self = spine2d_on_paint_self,
//                            .on_event = spine2d_on_event,
//                            .on_destroy = spine2d_on_destroy};

TK_DECL_VTABLE(spine2d) = {
    /* 基本属性 */
    sizeof(spine2d_t),     // size
    WIDGET_TYPE_SPINE2D,   // type
    s_spine2d_properties,  // clone_properties
    s_spine2d_properties,  // persistent_properties

    /* 布尔标志 */
    FALSE,  // scrollable
    FALSE,  // inputable
    FALSE,  // focusable
    FALSE,  // disallow_children_focusable
    FALSE,  // space_key_to_activate
    FALSE,  // return_key_to_activate
    FALSE,  // is_window
    FALSE,  // is_window_manager
    FALSE,  // is_designing_window
    FALSE,  // is_keyboard
    FALSE,  // allow_draw_outside

    /* 父类vtable相关 */
    NULL,                      // parent (动态继承使用)
    widget_get_widget_vtable,  // get_parent_vt (静态继承使用)
    NULL,                      // pointer_cursor

    /* 函数指针 - 必须严格按照结构体定义顺序 */
    spine2d_create,         // create
    spine2d_get_prop,       // get_prop
    spine2d_set_prop,       // set_prop
    NULL,                   // invalidate
    NULL,                   // find_target
    NULL,                   // is_point_in
    NULL,                   // get_offset
    NULL,                   // auto_adjust_size
    NULL,                   // get_prop_default_value
    NULL,                   // get_only_active_children
    NULL,                   // on_copy
    NULL,                   // on_keyup
    NULL,                   // on_keydown
    NULL,                   // on_wheel
    NULL,                   // on_multi_gesture
    NULL,                   // on_re_translate
    NULL,                   // on_paint_background
    spine2d_on_paint_self,  // on_paint_self
    NULL,                   // on_paint_children
    NULL,                   // on_paint_border
    NULL,                   // on_paint_begin
    NULL,                   // on_paint_end
    NULL,                   // on_pointer_down
    NULL,                   // on_pointer_move
    NULL,                   // on_pointer_up
    NULL,                   // on_context_menu
    NULL,                   // on_layout_children
    NULL,                   // on_add_child
    NULL,                   // on_remove_child
    NULL,                   // on_attach_parent
    NULL,                   // on_detach_parent
    spine2d_on_event,       // on_event
    NULL,                   // on_event_before_children
    spine2d_on_destroy,     // on_destroy
    NULL                    // init
};

static ret_t spine2d_on_prop_change(void* ctx, event_t* e){
  prop_change_event_t *pc_e = (prop_change_event_t*)e;
  // printf("spine2d_on_prop_change**************************%s\n",pc_e->name);
  widget_t* widget = WIDGET(e->target);
  spine2d_t* spine2d = SPINE2D(ctx);
  return_value_if_fail(spine2d != NULL, RET_BAD_PARAMS);

  if (tk_str_eq(pc_e->name, WIDGET_PROP_X) ||
      tk_str_eq(pc_e->name, WIDGET_PROP_Y)) {
    skeleton_info_t* info = (skeleton_info_t*)spine2d->skeleton_info;
    skeleton_update_position_size(widget, info->skeleton);
  }

  return RET_OK;
}

widget_t* spine2d_create(widget_t* parent, xy_t x, xy_t y, wh_t w, wh_t h) {
  log_debug("spine2d_create***************************************************\n");
  widget_t* widget = widget_create(parent, TK_REF_VTABLE(spine2d), x, y, w, h);
  spine2d_t* spine2d = SPINE2D(widget);
  return_value_if_fail(spine2d != NULL, NULL);

  spine2d->scale_x = 1;
  spine2d->scale_y = 1;
  spine2d->scale_time = 1;
  spine2d->loop = TRUE;
  widget_on(widget, EVT_PROP_CHANGED, spine2d_on_prop_change, widget);

  return widget;
}

widget_t* spine2d_cast(widget_t* widget) {
  return_value_if_fail(WIDGET_IS_INSTANCE_OF(widget, spine2d), NULL);

  return widget;
}
