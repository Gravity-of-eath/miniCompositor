# 上下文：Spine 降级 4.2 → 3.8

## 背景
- Spine 4.x Editor 采用年费订阅（用户反馈太贵），3.8 Editor 免费
- awtk-widget-spine2d 当前依赖 spine-cpp 4.2，包含校验 `startsWith("4.2")`，非 4.2 数据会被拒绝加载
- `3rd/spine-cpp/spine-cpp_3.8/` 目录已存在并是**混合版**：3.8 基础 + 从 4.2 backport 的 SkeletonRenderer/Physics.h

## 关键文件位置
- 4.2 runtime（待移除）：`3rd/awtk-widget-spine2d/3rd/spine-cpp/spine-cpp/`
- 4.2 C 封装层（需删除依赖）：`3rd/awtk-widget-spine2d/3rd/spine-cpp/spine-cpp-lite/`
- 目标 3.8 runtime：`3rd/awtk-widget-spine2d/3rd/spine-cpp/spine-cpp_3.8/`
- widget 适配层：`3rd/awtk-widget-spine2d/src/spine2d/spine_gl.{h,cpp}`、`spine2d.cpp`
- 构建脚本：`3rd/awtk-widget-spine2d/{SConstruct, src/SConscript, 3rd/SConscript}`

## API 兼容性结论
- 3.8 已 backport 的 `SkeletonRenderer::render()` 返回的 `RenderCommand` 结构与 4.2 字段完全一致
- `AnimationStateListenerObject::callback(state, EventType, TrackEntry, Event)` 3.8/4.2 签名一致
- `Bone::setYDown`、`Skeleton::setPosition/update(delta)`、`AnimationState::*` 全部兼容
- **唯一差异**：3.8 的 `Skeleton::updateWorldTransform()` 无参（4.2 需传 Physics_Update）
- **唯一不兼容**：3.8 无 `spine-cpp-lite`，widget 里 `renderer_draw_lite` 需删除

## 后续用户动作
- 代码改完后，资源必须用 Spine 3.8 Editor 重新导出：`design/default/data/spineboy-pro.{json,skel,atlas}` 和 `res/assets/default/raw/data/spineboy-pro.*`
- 二进制/JSON 格式不兼容，4.2 导出的资源无法被 3.8 runtime 读取
