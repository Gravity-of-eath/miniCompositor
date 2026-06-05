# Toast 提示设计（独立守护进程 + 替换语义）

日期：2026-06-05
状态：已通过设计评审，待写实现计划

## 1. 目标与约束

应用可以直接给出一段 Toast 提示文本，需要：

- **无条件展示在最上层**（压在所有全屏应用和 popup 之上）
- **固定时间后自动消失**
- **不占焦点**
- **不可触摸**（输入穿透到下层应用）

并在此基础上提供可选参数：**显示位置** 与 **显示时长**。

## 2. 现状（与 Toast 相关的 mc 合成器事实）

- 角色仅 3 种：`MC_ROLE_FULLSCREEN=1`(z=50)、`MC_ROLE_POPUP=2`(z=100)、`MC_ROLE_BG=3`(z=0)。
  `z_order` 决定合成叠放（低→高）与触摸命中顺序（高→低）。
- `mc_input_hit_test()`：把可见 surface 按 z 降序，取第一个命中点的 surface —— 最上层抢触摸。
- 已有 bus 发布订阅（`mc_bus_publish/subscribe`，topic 形如 `ui/...`，载荷 ≤4KB，发布者不收自己的消息）。
- surface flag 机制存在，目前仅 `MC_SURF_FLAG_FLIP_Y (1<<0)`；**无**"不接收输入"标志，**无**高于 POPUP 的层级。
- LVGL 已作为 mc 客户端跑通（`ports/lvgl` / `lv_port_mc`），**自带字体渲染**；compositor 本身没有字体栈。

## 3. 总体方案

**独立 Toast 守护进程**渲染并管理生命周期；应用只发一条 bus 消息。
并发策略为**替换语义**：新 Toast 立即取代当前显示并重置计时。

```
应用 ──mc_toast()──> bus "ui/toast" ──> Toast 守护进程
                                          │ LVGL 渲染文本到 TOAST 层 surface
                                          │ 计时(替换=重启计时)
                                          └ 到点 destroy surface
compositor: TOAST 角色 z=200 最上层 + 命中测试跳过 → 不可触摸/不抢焦点
```

## 4. 应用侧 API（加在 `libmc/include/mc.h` / 实现在 `libmc/src`）

```c
typedef enum {
    MC_TOAST_POS_BOTTOM = 0,  /* 默认：底部居中 */
    MC_TOAST_POS_CENTER = 1,
    MC_TOAST_POS_TOP    = 2,
} mc_toast_pos_t;

/* text: UTF-8 文本。
 * duration_ms: <=0 用默认时长 (2000ms)。
 * pos: 位置；越界回落 BOTTOM。
 * 本质就是一次 bus publish，不创建 surface、不阻塞、可随处调用。
 * 返回 0 成功，负值为 MC_E_*。*/
int mc_toast(mc_ctx_t *ctx, const char *text, int duration_ms, mc_toast_pos_t pos);
```

- 实现：打包定长头 + 文本，`mc_bus_publish(ctx, "ui/toast", payload, len)`。
- 线协议（建议）：`{ uint16 version=1; uint8 pos; uint8 _pad; uint32 duration_ms; char text[] }`，
  小端，`text` 不含 NUL 也可（按 len 推断）。文本超过载荷上限按字节截断（注意不截断到 UTF-8 字符中间，留待实现处理）。

## 5. Toast 守护进程（新增常驻 mc 客户端，复用 LVGL）

- 启动即 `mc_connect` + `mc_bus_subscribe("ui/toast")`，进入 `mc_dispatch` + 超时的单线程事件循环。
- **单 surface + 替换语义**：只维护一个 TOAST 层 surface。每收到一条消息：
  1. 用 LVGL label 渲染（半透明圆角底 + 文字，最大宽度 = 屏宽 80%，超宽自动换行）；
  2. 按 `pos` 摆放（BOTTOM/CENTER/TOP，留边距）；
  3. commit；
  4. **重启计时器**（无论当前是否有 Toast 在显示，新消息直接覆盖并重新计时）。
- **到点消失**：计时器到期 → destroy surface（或提交空内容）→ compositor 重合成自动移除。
- 计时实现：事件循环用 `poll`/`mc_dispatch` 的超时来驱动到期，避免独立线程。
- 由 `start.sh` 与其它 mc 客户端一起拉起；进程挂掉不影响应用（`mc_toast` 仍只是发消息），交上层 supervisor 重启。

## 6. compositor 改动（两处小改 + 一处核实）

1. **新角色 `MC_ROLE_TOAST = 4`**：`surface.c` 的 `default_z_order()` 返回 **200**（高于 POPUP 的 100）→ 无条件最上层。
2. **TOAST 层输入透明**：`mc_input_hit_test()` 跳过 `role==MC_ROLE_TOAST` 的 surface → 不可触摸、不抢焦点。
   不新增 NO_INPUT flag —— "不可触摸"是 Toast 固有属性，绑在角色上更省代码（YAGNI）。
3. **核实**：`compose.c` 需把 role 4 当普通 alpha 叠加层处理（与 popup 一样混合），
   不能被 `role==2` 的硬编码漏掉。实现时验证合成与"全屏不透明剔除"逻辑对 role 4 正确。

## 7. 默认值

- 默认时长：**2000ms**
- 默认位置：**底部居中**
- 最大宽度：**屏宽 80%**，超宽自动换行
- 位置档位：仅 **上 / 中 / 下** 三档（不开放任意像素坐标）

## 8. 测试要点（真机）

- 应用发 Toast → 出现在 popup 之上、到时自动消失。
- Toast 显示期间触摸**穿透**到下层应用（命中测试跳过验证）。
- 连发两条 → 后一条**立即替换**并**重新计时**。
- 默认参数（时长/位置）与显式参数都生效；位置越界回落 BOTTOM。
- 守护进程未启动时 `mc_toast` 不报错（仅无显示）。

## 9. 明确不做（YAGNI）

- 不做 Toast 队列/堆叠（已选替换语义）。
- 不开放任意像素坐标定位。
- 不在 compositor 内做字体渲染。
- 不新增通用 NO_INPUT surface flag（绑在 TOAST 角色上）。
