## AWTK 骨骼动画控件用法

### 创建骨骼动画控件

atlas 指定纹理图集文件，skeleton 指定骨骼动画数据文件。数据文件和图片文件放在design/assets/default/raw/data目录下。

scale_x 和 scale_y 指定缩放比例，根据实际情况调整。

scale_time 指定播放速度，1 表示正常速度，小于 1 表示减速，大于 1 表示加速。

action 指定动画名称，多个动画用逗号分隔。

示例：

```xml
  <spine2d name="spine2d" x="c" y="m" w="240" h="200" atlas="spineboy-pma.atlas"
    skeleton="spineboy-pro.skel" scale_x="0.5" scale_y="0.5" 
    action="portal,run" />
```

### 播放动画

通过 spine2d_set_action 函数设置当前播放的动画名称。

```c
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
```

### 注册动画事件

通过 widget_on 函数注册动画事件。目前支持的事件有：

* EVT_ANIM_START 动画开始事件
* EVT_ANIM_ONCE 动画播放一次事件
* EVT_ANIM_END 动画结束事件

```c
static ret_t on_anim_start(void* ctx, event_t* e) {
  widget_animator_event_t* evt = widget_animator_event_cast(e);
  widget_animator_t* animator = (widget_animator_t*)evt->animator;
  log_debug("on_anim_start: %s\n", animator->name);

  return RET_OK;
}

static ret_t on_anim_once(void* ctx, event_t* e) {
  widget_animator_event_t* evt = widget_animator_event_cast(e);
  widget_animator_t* animator = (widget_animator_t*)evt->animator;
  log_debug("on_anim_once: %s\n", animator->name);

  return RET_OK;
}

static ret_t on_anim_end(void* ctx, event_t* e) {
  widget_animator_event_t* evt = widget_animator_event_cast(e);
  widget_animator_t* animator = (widget_animator_t*)evt->animator;
  log_debug("on_anim_end: %s\n", animator->name);

  return RET_OK;
}

  widget_child_on(win, "spine2d", EVT_ANIM_START, on_anim_start, win); 
  widget_child_on(win, "spine2d", EVT_ANIM_END, on_anim_end, win); 
  widget_child_on(win, "spine2d", EVT_ANIM_ONCE, on_anim_once, win); 
```
