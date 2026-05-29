# Mini-Compositor 完整设计方案 v1.1

**项目代号**：`mc` (mini-compositor)
**目标平台**：Allwinner T113 / T507，Rockchip RV1126 / RV1106 / RK3036 —— Linux + Framebuffer
**适配框架**：LVGL（CPU 渲染）、AWTK（CPU 或 GPU 渲染）、原生 C 客户端
**文档版本**：v1.1（2026-05-27）
**主要变更**：相对 v1.0 修订了"GPU 渲染"决策，新增双轨 Buffer 模型（shm + dma-buf）以支持有 GPU 平台的 GL 渲染客户端。

---

## 目录

- 第 0 章 设计目标与约束
- 第 1 章 系统架构
- 第 2 章 协议设计
- 第 3 章 共享内存与缓冲管理
- 第 4 章 Compositor 核心
- 第 5 章 客户端 SDK (libmc)
- 第 6 章 LVGL 适配
- 第 7 章 AWTK 适配
- 第 8 章 硬件加速与平台后端
- 第 9 章 启动器与配置
- 第 10 章 测试方案
- 第 11 章 安全考虑
- 第 12 章 实施路线图
- 第 13 章 风险与决策记录
- 第 14 章 交付物清单
- 附录 A 协议消息完整参考
- 附录 B 平台特性矩阵
- 附录 C 术语表

---

## 第 0 章 设计目标与约束

### 0.1 功能目标

| 编号 | 需求 | 优先级 |
|---|---|---|
| F1 | 多 APP 并发显示，独立进程 | P0 |
| F2 | 前后台切换（整 APP 级） | P0 |
| F3 | 弹窗叠加（半透明、modal） | P0 |
| F4 | 触摸事件正确分发到目标 APP | P0 |
| F5 | 支持 LVGL / AWTK 客户端共存 | P0 |
| F6 | APP 间消息通讯（Bus） | P0 |
| F7 | 共享大块数据（shm/dma-buf） | P1 |
| F8 | 硬件加速合成（G2D/RGA） | P1 |
| F9 | 支持客户端 GPU 渲染（dma-buf 路径） | P1 |
| F10 | 横竖屏切换 | P2 |
| F11 | 动画转场（淡入淡出、滑动） | P2 |

### 0.2 非功能约束

- **内存预算**：Compositor 常驻 ≤ 4MB（含合成缓冲）
- **启动时间**：mc 守护进程 < 200ms 完成 fb / input 初始化
- **延迟**：
  - 触摸事件 evdev → APP ≤ 8ms
  - commit → 上屏 ≤ 16.6ms（一帧 @60fps）
- **稳定性**：任一 APP 崩溃不影响 Compositor 和其他 APP
- **依赖**：仅依赖 glibc/musl + Linux 内核接口；不引入 D-Bus、systemd、Wayland、Qt
- **License**：自有，可商用闭源
- **代码规模**：核心 ≤ 3200 行 C；总目标 ≤ 5500 行（v1.1 因新增 dma-buf 略增）

### 0.3 显式不做（Non-goals）

- **Compositor 自身不用 GPU 做合成**：合成是简单的 blit + alpha，2D 加速器（G2D/RGA）足够；引入 EGL/GL 会增加启动时间、依赖体积与崩溃面
- **Compositor 不提供绘图原语**：字体、矢量、控件是 LVGL/AWTK 的职责
- 不做 X11 风格的网络透明
- 不做多显示器（目标平台基本单屏）
- 不做 IME（工业 HMI 用屏幕键盘即可）

> 注意：**"Compositor 不做 GPU" 不等于 "客户端不能用 GPU"**。客户端可以用 OpenGL ES 渲染并通过 dma-buf 把结果交给 Compositor，详见第 3 章和第 8 章。

---

## 第 1 章 系统架构

### 1.1 总览

```
┌────────────────────────────────────────────────────────────────┐
│                            用户态                                │
│                                                                  │
│   ┌────────────────────────────────────────────────┐            │
│   │              mc-compositor (root or video组)    │            │
│   │  ┌──────────┐  ┌──────────┐  ┌──────────────┐  │            │
│   │  │ socket   │  │ surface  │  │  bus broker  │  │            │
│   │  │ server   │  │ manager  │  │              │  │            │
│   │  └──────────┘  └──────────┘  └──────────────┘  │            │
│   │  ┌──────────┐  ┌──────────┐  ┌──────────────┐  │            │
│   │  │  input   │  │compositor│  │   damage     │  │            │
│   │  │ pump     │  │  core    │  │   tracker    │  │            │
│   │  └──────────┘  └──────────┘  └──────────────┘  │            │
│   │  ┌──────────┐  ┌──────────┐  ┌──────────────┐  │            │
│   │  │   GAL    │  │   IAL    │  │   ACCEL      │  │            │
│   │  │ fbdev    │  │  evdev   │  │ g2d / rga    │  │            │
│   │  └──────────┘  └──────────┘  └──────────────┘  │            │
│   └─────────┬──────────────────┬──────────────────┬─────────────┘
│             │ unix socket      │ shm fd / dmabuf  │ /dev/fb0    │
│             │ + fd-passing     │ + eventfd        │ /dev/g2d    │
│             ▼                  ▼                  │ /dev/rga    │
│   ┌──────────────┐  ┌──────────────┐  ┌──────────│/dev/input/* │
│   │  libmc.so    │  │  libmc.so    │  │  libmc.so│             │
│   ├──────────────┤  ├──────────────┤  ├──────────┤             │
│   │ LVGL port    │  │ AWTK soft    │  │ AWTK GL  │             │
│   │ (shm)        │  │ (shm)        │  │ (dmabuf) │             │
│   ├──────────────┤  ├──────────────┤  ├──────────┤             │
│   │ APP-A        │  │ APP-B        │  │ APP-C    │             │
│   │ (Dashboard)  │  │ (Settings)   │  │ (Alert)  │             │
│   └──────────────┘  └──────────────┘  └──────────┘             │
│                              用户态                              │
└──────────────────────────────────────────────────────────────────┘
                                  │
        ┌─────────────────────────┼─────────────────────────┐
        │                         内核                       │
        │ fbdev | evdev | dma-heap/ION | g2d | rga | DRM/GBM│
        └────────────────────────────────────────────────────┘
```

### 1.2 模块划分

**Compositor 端（mc-compositor）**

| 模块 | 职责 | 行数估计 |
|---|---|---|
| `gal/` | Framebuffer 抽象、page flip、pan display | 300 |
| `ial/` | evdev 抽象、multi-touch slot 协议 | 250 |
| `accel/` | G2D/RGA 抽象、blit/alpha/scale，dma-buf 互操作 | 500 |
| `proto/` | TLV 序列化、fd-passing | 200 |
| `transport/` | socket server、epoll loop | 250 |
| `surface/` | surface 表、buffer 管理（shm + dmabuf）、z-order | 500 |
| `compose/` | 合成调度、damage 合并、fence 等待 | 400 |
| `input/` | 输入路由（hit-test、focus、grab） | 300 |
| `bus/` | topic 订阅/发布 | 200 |
| `lifecycle/` | 客户端状态机、前后台切换 | 200 |
| `main.c` | 配置加载、初始化、主循环 | 150 |
| **小计** | | **~3200** |

**客户端 SDK（libmc.so）**

| 模块 | 职责 | 行数估计 |
|---|---|---|
| `connect.c` | socket 连接、握手 | 80 |
| `surface_shm.c` | shm 路径 surface 管理 | 200 |
| `surface_dmabuf.c` | dma-buf 路径 surface 管理 | 180 |
| `event.c` | 事件解析、回调分发 | 150 |
| `bus.c` | 订阅/发布 | 80 |
| `util.c` | 帧序列化、错误处理 | 90 |
| **小计** | | **~780** |

**Framework Ports**

| 模块 | 职责 | 行数 |
|---|---|---|
| `lvgl-port/` | disp_drv + indev_drv 适配（shm） | 120 |
| `awtk-port-soft/` | lcd_mem + input source（shm） | 180 |
| `awtk-port-gl/` | EGL + GBM + dma-buf 导出 | 320 |
| **小计** | | **~620** |

### 1.3 进程模型与启动顺序

```
启动顺序（由 init / systemd / mc-launcher 拉起）:

  1. mc-compositor                    [必须最先]
       ├─ 打开 /dev/fb0
       ├─ 打开 /dev/input/event*    + EVIOCGRAB
       ├─ 打开 /dev/g2d 或 /dev/rga
       ├─ 打开 /dev/dma_heap/* (可选, 用于 dma-buf)
       └─ 创建 /var/run/mc.sock 并监听

  2. mc-launcher (可选)
       └─ 按 launcher.conf 启动 APP, 监控崩溃重启

  3. APP-A, APP-B, ...                [并行启动]
       └─ connect("/var/run/mc.sock")
```

崩溃恢复：
- APP 崩溃 → mc-launcher 按策略重启（指数退避，3 次失败停止）
- Compositor 崩溃 → init/systemd 拉起 → 各 APP 通过 SDK 自动重连

---

## 第 2 章 协议设计

### 2.1 传输层

- **类型**：`AF_UNIX, SOCK_STREAM`
- **路径**：`/var/run/mc.sock`，权限 `0660`，属主 `root:video`
- **认证**：通过 `SO_PEERCRED` 拿到客户端 uid/pid（生产可加 ACL）

### 2.2 帧格式

固定 12 字节头 + 变长 TLV 载荷：

```c
struct mc_msg_hdr {
    uint16_t magic;       // 'M' << 8 | 'C' = 0x4D43
    uint16_t type;        // 消息类型, 见 §2.3
    uint32_t payload_len; // payload 字节数
    uint32_t serial;      // 请求/响应配对
} __attribute__((packed));
```

**TLV 字段**：

```c
struct mc_tlv {
    uint16_t tag;
    uint16_t len;
    uint8_t  value[];
} __attribute__((packed));
```

**Tag 命名空间**（高字节模块号，低字节字段号）：

| 范围 | 模块 |
|---|---|
| 0x01xx | 通用（id、name、version） |
| 0x02xx | Surface（w、h、format、role、buf_type） |
| 0x03xx | Buffer（index、stride、damage、dmabuf） |
| 0x04xx | Input（type、x、y、slot） |
| 0x05xx | Lifecycle |
| 0x06xx | Bus（topic、payload） |

完整 Tag 表见附录 A.2。

### 2.3 消息类型总表

#### Client → Compositor

```
0x01  CL_HELLO              握手, 携带 NAME/VERSION/PID/CAPS
0x02  CL_CREATE_SURFACE     申请 surface (含 BUF_TYPE: SHM 或 DMABUF)
0x03  CL_DESTROY_SURFACE    销毁 surface
0x04  CL_COMMIT             提交一帧 (含 damage; dmabuf 路径附 fd+fence)
0x05  CL_SET_ROLE           改变角色 FULLSCREEN/POPUP/BG
0x06  CL_REQUEST_FOCUS      申请前台/焦点
0x07  CL_ACK_LIFECYCLE      确认生命周期切换
0x10  CL_BUS_SUB            订阅 topic (支持通配)
0x11  CL_BUS_PUB            发布消息
0x12  CL_BUS_UNSUB          退订
0xFF  CL_BYE                优雅断开
```

#### Compositor → Client

```
0x81  SV_WELCOME            回复 HELLO, 含 client_id/screen 信息
0x82  SV_SURFACE_OK         回复 CREATE_SURFACE
                             - SHM 路径附 SCM_RIGHTS: [shm_fd × N, efd]
                             - DMABUF 路径仅返回 meta
0x83  SV_FRAME_DONE         上帧已上屏 (通常用 eventfd 替代)
0x84  SV_LIFECYCLE          VISIBLE / HIDDEN / SUSPEND / RESUME
0x85  SV_INPUT              触摸事件
0x86  SV_FOCUS              焦点变化
0x90  SV_BUS_MSG            收到订阅消息
0xEE  SV_ERROR              错误响应, code + msg
```

详细字段定义见附录 A.1。

### 2.4 序列号与错误处理

- `serial`：客户端发请求时单调递增，回复带相同 serial
- 异步事件（SV_INPUT/SV_LIFECYCLE/SV_BUS_MSG）`serial = 0`
- 错误：`SV_ERROR` 带原 serial，CODE 见 §2.5

### 2.5 错误码

```
0x00  OK
0x01  EPROTO       协议错误
0x02  EINVAL       参数非法
0x03  ENOMEM       资源不足
0x04  ENOENT       资源不存在 (sid 无效等)
0x05  EBUSY        操作冲突
0x06  EPERM        权限不足
0x07  ETOOLARGE    超出限制
0x08  ENOTSUP      不支持的特性 (如 dma-buf 在无 GPU 平台)
0xFE  EINTERNAL    内部错误
```

### 2.6 协议演进

- `CL_HELLO.VERSION` 携带语义化版本（major.minor）
- major 不同直接拒绝
- minor 高方加新 tag，老方忽略未知 tag（向后兼容）
- 永不删除已分配的 tag/type
- 新增能力通过 `CAPS` 位图协商：`CAP_DMABUF`、`CAP_FENCE`、`CAP_BUS`、`CAP_MULTI_SURFACE` 等

---

## 第 3 章 共享内存与缓冲管理

### 3.1 双轨 Buffer 模型

v1.1 关键升级：支持两种 buffer 类型，客户端按渲染方式选择。

```c
typedef enum {
    MC_BUF_TYPE_SHM    = 1,  // 软渲染: memfd / shm
    MC_BUF_TYPE_DMABUF = 2,  // GPU 渲染: dma-buf fd
} mc_buf_type_t;
```

| 路径 | 适用场景 | 后端 | 性能 |
|---|---|---|---|
| SHM | LVGL CPU 渲染、AWTK AGGE/nanovg-soft | memfd_create / dma-heap | 中 |
| DMABUF | AWTK nanovg-gl、客户端自渲染 GL | GBM / dma-heap + EGL | 高（零拷贝） |

### 3.2 SHM 路径详细

**后端选择**：

| 后端 | 优点 | 缺点 | 默认 |
|---|---|---|---|
| `memfd_create` | 匿名、可 seal | 内核 ≥ 3.17 | **是** |
| `shm_open` | 兼容性好 | 路径污染 /dev/shm | 回退 |
| `dma-heap` | 可硬件 DMA 零拷贝 | 4.12+ 内核 | 加速时启用 |

**Buffer 布局**：

```c
// 每个 buffer = 64B 头 + 像素区
struct mc_buf_hdr {
    uint32_t magic;            // 0x4D434246 'MCBF'
    uint32_t seq;              // 帧序号
    uint32_t state;            // FREE/CLIENT_OWN/READY/SCANOUT
    uint32_t damage_count;
    struct { int16_t x,y,w,h; } damage[6];
    uint32_t reserved[3];
};

// 状态
#define MC_BUF_FREE        0
#define MC_BUF_CLIENT_OWN  1
#define MC_BUF_READY       2
#define MC_BUF_SCANOUT     3
```

**对齐**：
- `stride = align(width * bpp, 64)`（cacheline 友好，覆盖所有平台对齐要求）
- buffer 总大小 = `align(sizeof(hdr), 64) + stride * height`

**同步**：每 surface 一个 eventfd，Compositor 释放 buffer 时 `write(efd, 1)`，client `epoll` 等待。

### 3.3 DMABUF 路径详细

**生命周期**：
- `CREATE_SURFACE(buf_type=DMABUF)` 时 Compositor **不分配 buffer**
- 客户端用 GBM / dma-heap 自行分配 BO
- 每帧 `COMMIT` 时通过 SCM_RIGHTS 携带 dmabuf_fd 和 fence_fd
- Compositor 通过 `mmap` 或交给 G2D/RGA 消费 dmabuf_fd

**协议字段**：

```
CL_COMMIT (DMABUF 路径):
  Tags:
    SID
    DMABUF_FORMAT     (DRM_FORMAT_ARGB8888 / etc)
    DMABUF_MODIFIER   (uint64, 默认 LINEAR=0)
    DMABUF_STRIDE
    DMABUF_OFFSET
    DAMAGE_RECTS
  SCM_RIGHTS:
    [dmabuf_fd, fence_fd?]    // fence_fd 可选
```

**Fence 同步**：
- 客户端在 GL 渲染完成后用 `EGL_ANDROID_native_fence_sync` 拿到 sync fd
- Compositor 在合成前 `poll(fence_fd, POLLIN, timeout)` 等 GPU 完成
- 无 fence 时回退 `glFinish()` 在客户端侧（性能差，仅调试）

**Modifier 协商**：
- 默认只支持 `DRM_FORMAT_MOD_LINEAR`（线性布局）
- 平台特有的 tiled 格式（如 Mali AFBC）暂不支持，需 G2D/RGA 能处理才打开
- 协商方式：`SV_WELCOME` 中带 `SUPPORTED_MODIFIERS` 列表

### 3.4 Buffer 数量与轮换

| 模式 | n_buf | 适用 |
|---|---|---|
| 单 buffer | 1 | 不推荐，会撕裂 |
| 双 buffer | 2 | 默认 |
| 三 buffer | 3 | GPU 路径推荐（GL pipeline 较深） |

GPU 路径建议 3 buffer：GL 渲染异步特性使得 client 可能在 fence 完成前就要画下一帧。

### 3.5 内存回收

- Client 断开：Compositor 关闭所有 fd，ref-count 归零自动释放
- 显式 `CL_DESTROY_SURFACE`：立即释放
- 异常崩溃：socket EPOLLHUP 触发清理
- DMABUF：每帧 commit 后 Compositor 在合成完成后 close fd，client 也应 close（dma-buf 引用计数管理）

---

## 第 4 章 Compositor 核心

### 4.1 主循环

单线程 epoll 模型：

```
epoll fd set:
  - listen_sock (LT)            新客户端连接
  - client_socks[N] (LT)        客户端消息
  - input_fd[]                  触摸/按键事件
  - vsync_timer_fd 或 fb_vsync  合成时机
  - signalfd                    SIGTERM / SIGINT
  - fence_fds[]                 GPU 完成事件 (动态)
```

**时间片预算（60fps，16.6ms）**：

| 阶段 | 预算 |
|---|---|
| 处理 socket 消息 | 2ms |
| 处理 input 事件 | 1ms |
| 等 fence (GPU 路径) | 0~3ms |
| 合成（damage 合并 + blit） | 6ms |
| 上屏（FBIOPAN_DISPLAY） | <1ms |
| 余量 | 3ms |

### 4.2 Surface 数据结构

```c
#define MAX_CLIENTS  16
#define MAX_SURFACES 64

struct mc_surface {
    uint32_t sid;
    uint32_t cid;            // 所属客户端
    uint16_t w, h;
    uint32_t stride;
    uint8_t  format;         // ARGB8888 / RGB565
    uint8_t  role;           // FULLSCREEN / POPUP / BG
    uint8_t  modal;
    uint8_t  buf_type;       // SHM / DMABUF
    int16_t  x, y;           // POPUP 位置
    int      z_order;
    uint8_t  visible;

    // buffers
    int      n_buf;
    struct mc_buf bufs[3];
    int      event_fd;

    int      scanout_idx;    // 当前合成中的 buffer
    int      pending_idx;    // 已 commit 待合成

    struct mc_surface *next; // z-order 链表
};

struct mc_buf {
    mc_buf_type_t type;
    union {
        struct {
            int    fd;
            void  *map;
            size_t size;
            struct mc_buf_hdr *hdr;
        } shm;
        struct {
            int      fd;
            int      fence_fd;
            uint64_t modifier;
            uint32_t drm_format;
            uint32_t stride;
            uint32_t offset;
        } dmabuf;
    };
};
```

### 4.3 Z-order 与可见性

- **z_order**：整数，越大越靠上
- 默认值：`BG=0`，`FULLSCREEN=10`，`POPUP=100`
- `CL_REQUEST_FOCUS`：把目标 FULLSCREEN 提到当前最高 FULLSCREEN 的 z+1
- 同 z 按创建顺序

**可见性计算**：从顶到底遍历，FULLSCREEN 完全遮挡时下面的标记为 `HIDDEN`（推送 SV_LIFECYCLE）。

### 4.4 合成算法

```
for each frame tick (vsync 或 timer):
    # 1. 收集待合成 surfaces
    for each surface:
        if surface.pending_idx >= 0:
            CAS buf[pending_idx].state: READY -> SCANOUT
            surface.scanout_idx = pending_idx

    # 2. DMABUF 路径等 fence
    for each dmabuf surface in scanout:
        if buf.fence_fd >= 0:
            poll(fence_fd, POLLIN, 8ms)   # 超时则跳过这一帧
            close(fence_fd)

    # 3. 计算屏幕级 damage
    screen_damage = union(map_to_screen(s.damage) for s in scanout)
    if screen_damage is empty:
        return  # 跳过此帧

    # 4. 快路径: 单 FULLSCREEN 不透明全屏 -> 直接 fb pan
    if eligible_fast_path():
        fb_pan_display(top_surface.buf)
        goto done

    # 5. 慢路径: 硬件合成
    accel_begin()
    for s in surfaces (z 升序, 与 screen_damage 有交集):
        if s.role == FULLSCREEN and s.opaque:
            accel_blit(fb, s.buf, src, dst)
        else:
            accel_blend(fb, s.buf, src, dst, global_alpha)
    accel_sync()

    # 6. 上屏
    fb_pan_display(fb_back_buffer)

    # 7. 释放 buffer
done:
    for each surface in scanout:
        CAS buf[scanout_idx].state: SCANOUT -> FREE
        write(surface.event_fd, 1)
```

**快路径条件**（zero-copy）：
- 只有一个可见 FULLSCREEN 且无 POPUP
- 该 surface 的 buffer 是物理连续（dma-heap 分配）
- 格式与 fb 一致
- fb 支持 FBIOPAN_DISPLAY 双 buffer

### 4.5 输入路由

**Hit-test 流程**：

```
on evdev event (raw_x, raw_y):
    # 校准
    x, y = calibrate(raw_x, raw_y)

    if global_grab.active:
        target = grab.owner
    elif active_modal_popup:
        target = modal_popup
    else:
        for s in surfaces (z desc, visible only):
            if point_in_rect(x, y, s.x, s.y, s.w, s.h):
                target = s
                break

    if target:
        local_x = x - target.x
        local_y = y - target.y
        send SV_INPUT(target.cid, ...)
```

**Multi-touch slot 协议**：
- 每个 slot 独立 tracking
- DOWN 时确定 target，整个 touch 序列（DOWN→MOVE…→UP）锁定到该 target
- UP/CANCEL 时解锁
- 同时多 slot 落在不同 surface：各自路由（多指多 APP 协同少见，但协议支持）

### 4.6 生命周期状态机

```
                    ┌─────────┐
                    │ CREATED │  CL_CREATE_SURFACE
                    └────┬────┘
                         │ 加入 z-order
                         ▼
                  ┌─────────────┐
                  │   VISIBLE   │◄────┐
                  └──┬──────┬───┘     │ REQUEST_FOCUS / 用户操作
                     │      │         │
        被遮挡       │      │ 切换到后台
                     ▼      ▼         │
              ┌──────────┐ ┌─────────┐│
              │  HIDDEN  │ │SUSPENDED││
              └────┬─────┘ └────┬────┘│
                   │            │     │
                   └────────────┴─────┘
```

策略：
- **HIDDEN**：仍接收 commit（保留预渲染能力），不接收 input
- **SUSPENDED**：停止接收 commit，APP 应释放重资源（大纹理、动画 timer）
- POPUP 出现，底层 FULLSCREEN 仍 VISIBLE（被部分遮挡，但仍可见）
- 内存压力下 mc-launcher 可强制 SUSPEND 后台 APP

### 4.7 错误处理与降级

| 错误 | 行为 |
|---|---|
| Client socket 错误 | 关闭 client，释放其全部 surface |
| DMABUF fence 超时 | 跳过该 surface 这一帧，下帧重试 |
| G2D/RGA ioctl 失败 | 该帧降级 CPU blit，记录 metric |
| FBIOPAN 失败 | 降级到 memcpy 到 fb |
| OOM | 拒绝新 surface，记录日志 |

---

## 第 5 章 客户端 SDK（libmc）

### 5.1 公共 API

```c
// libmc.h --------------------------------------------------

typedef struct mc_ctx mc_ctx_t;
typedef struct mc_surface mc_surface_t;

typedef enum {
    MC_FMT_ARGB8888 = 1,
    MC_FMT_BGRA8888 = 2,   // 与 AWTK 默认匹配
    MC_FMT_RGB565   = 3,
} mc_format_t;

typedef enum {
    MC_ROLE_FULLSCREEN = 1,
    MC_ROLE_POPUP      = 2,
    MC_ROLE_BG         = 3,
} mc_role_t;

typedef enum {
    MC_BUF_SHM    = 1,
    MC_BUF_DMABUF = 2,
} mc_buf_type_t;

typedef struct { int16_t x, y, w, h; } mc_rect_t;

typedef enum {
    MC_EV_NONE = 0,
    MC_EV_FRAME_DONE,
    MC_EV_TOUCH,
    MC_EV_LIFECYCLE,
    MC_EV_FOCUS,
    MC_EV_BUS,
    MC_EV_RECONNECTED,
    MC_EV_FATAL,
} mc_event_kind_t;

typedef struct {
    mc_event_kind_t kind;
    union {
        struct { int16_t x, y; uint8_t slot, state; uint32_t t; } touch;
        struct { uint32_t sid; int state; } lc;
        struct { uint32_t sid; int has_focus; } focus;
        struct {
            const char *topic;
            const char *sender;
            const void *data;
            uint32_t    len;
        } bus;
    };
} mc_event_t;

// 连接管理
mc_ctx_t *mc_connect(const char *app_name);
void      mc_disconnect(mc_ctx_t *);
int       mc_fd(mc_ctx_t *);  // for epoll

// 配置
void mc_set_reconnect_policy(mc_ctx_t *, int auto_reconnect);
void mc_set_log_level(int level);

// Surface (SHM 路径)
mc_surface_t *mc_surface_create_shm(mc_ctx_t *,
                                     int w, int h,
                                     mc_format_t fmt,
                                     mc_role_t role,
                                     int n_buf);
void *mc_surface_acquire(mc_surface_t *, int *out_stride);
void  mc_surface_commit (mc_surface_t *, const mc_rect_t *damage, int n_rect);

// Surface (DMABUF 路径)
mc_surface_t *mc_surface_create_dmabuf(mc_ctx_t *,
                                        int w, int h,
                                        mc_format_t fmt,
                                        mc_role_t role);
int mc_surface_commit_dmabuf(mc_surface_t *,
                              int dmabuf_fd,
                              int fence_fd,        // -1 表示无 fence
                              uint32_t stride,
                              uint64_t modifier,
                              const mc_rect_t *damage, int n_rect);

void mc_surface_destroy(mc_surface_t *);

// 角色与焦点
int mc_surface_set_role(mc_surface_t *, mc_role_t, int modal);
int mc_surface_set_popup_pos(mc_surface_t *, int x, int y);
int mc_surface_request_focus(mc_surface_t *);
int mc_surface_ack_lifecycle(mc_surface_t *, int state);

// 事件
int mc_dispatch(mc_ctx_t *, mc_event_t *out, int timeout_ms);

// Bus
int mc_bus_subscribe  (mc_ctx_t *, const char *topic);
int mc_bus_unsubscribe(mc_ctx_t *, const char *topic);
int mc_bus_publish    (mc_ctx_t *, const char *topic,
                       const void *payload, uint32_t len);

// 工具
const char *mc_strerror(int err);
```

### 5.2 线程模型

- **默认单线程**：所有 libmc 调用必须在 owner 线程；LVGL/AWTK 本来就是单线程，天然契合
- **可选多线程**（v2）：内部加 mutex，按需启用

### 5.3 重连策略

- **MC_RECONNECT_NEVER**：API 返回错误，APP 自行决定
- **MC_RECONNECT_AUTO**（默认）：内部重连，重建 surface，触发 `MC_EV_RECONNECTED`，APP 应全屏重绘

### 5.4 错误处理

- 所有 API 返回 `int`：0 成功，负数 = `-errno_like`
- `mc_strerror(int)` 转可读字符串
- 致命错误触发 `MC_EV_FATAL`，APP 应退出

---

## 第 6 章 LVGL 适配

### 6.1 编译配置

```c
// lv_conf.h
#define LV_COLOR_DEPTH       32         // ARGB8888, 便于合成
#define LV_COLOR_16_SWAP     0
#define LV_DISP_DEF_REFR_PERIOD 16      // 60fps
#define LV_USE_GPU_*         0          // CPU 渲染, 合成在 mc 端
```

**LVGL 与 mc 像素格式约定**：
- LVGL `LV_COLOR_DEPTH=32` 内存布局是 `B, G, R, A`（小端机器上）
- 这与 `MC_FMT_BGRA8888` 一致
- 合成器内部统一 BGRA8888，故 LVGL 直接对接，无需 swizzle

### 6.2 显示驱动

```c
typedef struct {
    mc_ctx_t     *ctx;
    mc_surface_t *surf;
    int           stride;
} lvgl_mc_priv_t;

static void mc_flush_cb(lv_disp_drv_t *drv,
                        const lv_area_t *area,
                        lv_color_t *color_p)
{
    lvgl_mc_priv_t *p = drv->user_data;
    void *dst = mc_surface_acquire(p->surf, &p->stride);

    int aw = area->x2 - area->x1 + 1;
    int ah = area->y2 - area->y1 + 1;
    uint8_t *d = (uint8_t*)dst + area->y1 * p->stride + area->x1 * 4;
    uint8_t *s = (uint8_t*)color_p;

    for (int y = 0; y < ah; y++) {
        memcpy(d + y * p->stride, s + y * aw * 4, aw * 4);
    }

    mc_rect_t damage = { area->x1, area->y1, aw, ah };
    mc_surface_commit(p->surf, &damage, 1);
    lv_disp_flush_ready(drv);
}
```

**Buffer 策略**：
- LVGL 用 `LV_DISP_BUF_DOUBLE`，绘制 buffer 是 LVGL 自己的
- flush_cb 拷贝到 mc shm，partial render 限定范围
- 进阶版：让 LVGL 直接渲染到 mc shm（`lv_disp_buf_init(buf, mc_buf_ptr)`），省一次 memcpy，但要处理与合成器读取的同步 → **推荐先用拷贝版**

### 6.3 输入驱动

```c
static void mc_indev_read_cb(lv_indev_drv_t *drv,
                             lv_indev_data_t *data)
{
    lvgl_mc_priv_t *p = drv->user_data;
    static int16_t last_x, last_y;
    static lv_indev_state_t last_state = LV_INDEV_STATE_REL;

    mc_event_t ev;
    while (mc_dispatch(p->ctx, &ev, 0) > 0) {
        switch (ev.kind) {
        case MC_EV_TOUCH:
            last_x = ev.touch.x;
            last_y = ev.touch.y;
            last_state = (ev.touch.state == 1 || ev.touch.state == 2)
                       ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
            break;
        case MC_EV_LIFECYCLE:
            handle_lifecycle(p, ev.lc.state);
            break;
        case MC_EV_BUS:
            handle_bus_msg(&ev.bus);
            break;
        default: break;
        }
    }

    data->point.x = last_x;
    data->point.y = last_y;
    data->state = last_state;
}
```

### 6.4 生命周期处理

```c
static void handle_lifecycle(lvgl_mc_priv_t *p, int state) {
    extern int g_lvgl_paused;
    switch (state) {
    case MC_LC_HIDDEN:
    case MC_LC_SUSPENDED:
        g_lvgl_paused = 1;
        break;
    case MC_LC_VISIBLE:
    case MC_LC_RESUMED:
        g_lvgl_paused = 0;
        lv_obj_invalidate(lv_scr_act());  // 强制全屏重绘
        break;
    }
    mc_surface_ack_lifecycle(p->surf, state);
}

// 主循环
while (running) {
    if (!g_lvgl_paused) lv_timer_handler();
    usleep(5000);
}
```

### 6.5 应用 main 函数模板

```c
int main(int argc, char **argv) {
    mc_ctx_t *ctx = mc_connect("dashboard");
    mc_surface_t *surf = mc_surface_create_shm(
        ctx, 800, 480, MC_FMT_BGRA8888, MC_ROLE_FULLSCREEN, 2);

    lv_init();
    static lv_color_t buf1[800 * 100], buf2[800 * 100];
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, 800 * 100);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 800;
    disp_drv.ver_res = 480;
    disp_drv.flush_cb = mc_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    static lvgl_mc_priv_t priv = { .ctx = ctx, .surf = surf };
    disp_drv.user_data = &priv;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = mc_indev_read_cb;
    indev_drv.user_data = &priv;
    lv_indev_drv_register(&indev_drv);

    create_ui();

    while (running) {
        if (!g_lvgl_paused) lv_timer_handler();
        usleep(5000);
    }

    mc_surface_destroy(surf);
    mc_disconnect(ctx);
    return 0;
}
```

---

## 第 7 章 AWTK 适配

### 7.1 两套后端

| 后端 | 平台 | 实现 |
|---|---|---|
| Software (shm) | 所有平台 | 改造 `lcd_mem_bgra8888` |
| GL (dmabuf) | T507 / RK3036 | EGL + GBM + nanovg-gl |

### 7.2 Software 后端

参考 AWTK 自带的 `lcd_mem_bgra8888.c`：

```c
typedef struct {
    lcd_mem_t base;
    mc_ctx_t *mc;
    mc_surface_t *surf;
} lcd_mc_t;

static ret_t lcd_mc_flush(lcd_t *lcd) {
    lcd_mc_t *self = (lcd_mc_t*)lcd;
    lcd_mem_t *mem = &self->base;

    rect_t *dr = &lcd->dirty_rect;
    mc_rect_t damage = { dr->x, dr->y, dr->w, dr->h };

    mc_surface_commit(self->surf, &damage, 1);

    // 等下一个 buffer 可用
    int stride;
    void *next = mc_surface_acquire(self->surf, &stride);
    mem->buff = next;
    return RET_OK;
}

lcd_t *lcd_mc_create(mc_ctx_t *ctx, int w, int h) {
    lcd_mc_t *self = TKMEM_ZALLOC(lcd_mc_t);
    self->mc = ctx;
    self->surf = mc_surface_create_shm(
        ctx, w, h, MC_FMT_BGRA8888, MC_ROLE_FULLSCREEN, 2);

    int stride;
    void *buf = mc_surface_acquire(self->surf, &stride);
    lcd_mem_init(&self->base, w, h, buf, BITMAP_FMT_BGRA8888);
    self->base.base.flush = lcd_mc_flush;
    return &self->base.base;
}
```

### 7.3 输入源

AWTK `main_loop_simple` 支持自定义输入源：

```c
static ret_t mc_input_dispatch(void *ctx_void) {
    mc_input_ctx_t *self = (mc_input_ctx_t*)ctx_void;
    mc_event_t ev;
    while (mc_dispatch(self->mc, &ev, 0) > 0) {
        if (ev.kind == MC_EV_TOUCH) {
            event_t e;
            pointer_event_t pe;
            int type;
            switch (ev.touch.state) {
            case 1: type = EVT_POINTER_DOWN; break;
            case 2: type = EVT_POINTER_MOVE; break;
            case 3: type = EVT_POINTER_UP;   break;
            default: continue;
            }
            pointer_event_init(&pe, type, self->widget,
                               ev.touch.x, ev.touch.y);
            input_dispatch_dispatch(self->dispatch, (event_t*)&pe);
        } else if (ev.kind == MC_EV_LIFECYCLE) {
            // 控制 main_loop_step
        }
    }
    return RET_OK;
}
```

### 7.4 GL 后端（T507 / RK3036）

参考 AWTK 的 `awtk-linux-fb/awtk-port/lcd_nanovg_gl/`：

```c
typedef struct {
    lcd_t base;
    mc_ctx_t *mc;
    mc_surface_t *surf;

    // EGL
    EGLDisplay egl_dpy;
    EGLContext egl_ctx;
    EGLConfig  egl_cfg;

    // GBM (用于分配 dmabuf)
    int gbm_fd;
    struct gbm_device *gbm;

    // 三槽轮换 (GL pipeline 异步)
    struct {
        struct gbm_bo  *bo;
        EGLImageKHR     img;
        GLuint          fbo;
        GLuint          tex;
        int             dmabuf_fd;
        uint32_t        stride;
        uint64_t        modifier;
    } slots[3];
    int cur_slot;

    // GL 函数指针
    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
    PFNEGLDUPNATIVEFENCEFDANDROIDPROC eglDupNativeFenceFDANDROID;
} lcd_mc_gl_t;

static ret_t init_gl_slots(lcd_mc_gl_t *self, int w, int h) {
    self->gbm_fd = open("/dev/dri/renderD128", O_RDWR);
    self->gbm = gbm_create_device(self->gbm_fd);

    for (int i = 0; i < 3; i++) {
        // 1. 分配 GBM BO
        self->slots[i].bo = gbm_bo_create(
            self->gbm, w, h, GBM_FORMAT_ARGB8888,
            GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);

        // 2. 导出 dma-buf fd
        self->slots[i].dmabuf_fd = gbm_bo_get_fd(self->slots[i].bo);
        self->slots[i].stride    = gbm_bo_get_stride(self->slots[i].bo);
        self->slots[i].modifier  = gbm_bo_get_modifier(self->slots[i].bo);

        // 3. 包装为 EGLImage
        EGLint attrs[] = {
            EGL_WIDTH, w,
            EGL_HEIGHT, h,
            EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_ARGB8888,
            EGL_DMA_BUF_PLANE0_FD_EXT, self->slots[i].dmabuf_fd,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)self->slots[i].stride,
            EGL_NONE
        };
        self->slots[i].img = self->eglCreateImageKHR(
            self->egl_dpy, EGL_NO_CONTEXT,
            EGL_LINUX_DMA_BUF_EXT, NULL, attrs);

        // 4. 绑定到 FBO
        glGenTextures(1, &self->slots[i].tex);
        glBindTexture(GL_TEXTURE_2D, self->slots[i].tex);
        self->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, self->slots[i].img);

        glGenFramebuffers(1, &self->slots[i].fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, self->slots[i].fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, self->slots[i].tex, 0);
    }
    return RET_OK;
}

static ret_t lcd_mc_gl_flush(lcd_t *lcd) {
    lcd_mc_gl_t *self = (lcd_mc_gl_t*)lcd;
    int i = self->cur_slot;

    // 创建 EGL fence
    EGLSyncKHR sync = eglCreateSyncKHR(self->egl_dpy,
                                       EGL_SYNC_NATIVE_FENCE_ANDROID,
                                       NULL);
    glFlush();
    int fence_fd = self->eglDupNativeFenceFDANDROID(self->egl_dpy, sync);
    eglDestroySyncKHR(self->egl_dpy, sync);

    rect_t *dr = &lcd->dirty_rect;
    mc_rect_t damage = { dr->x, dr->y, dr->w, dr->h };

    mc_surface_commit_dmabuf(self->surf,
                              self->slots[i].dmabuf_fd,
                              fence_fd,
                              self->slots[i].stride,
                              self->slots[i].modifier,
                              &damage, 1);

    // 切下一槽
    self->cur_slot = (i + 1) % 3;
    glBindFramebuffer(GL_FRAMEBUFFER, self->slots[self->cur_slot].fbo);
    return RET_OK;
}
```

### 7.5 像素格式注意

- AWTK 默认 BGRA8888（内存里 B,G,R,A）
- mc 内部统一 BGRA8888，故 AWTK Software 后端零开销对接
- GL 后端用 `DRM_FORMAT_ARGB8888`，内存布局也是 B,G,R,A（DRM 命名约定是反的），同样兼容

---

## 第 8 章 硬件加速与平台后端

### 8.1 加速抽象接口

```c
struct mc_accel_surface {
    uint16_t w, h;
    uint32_t stride;
    uint32_t format;        // DRM_FORMAT_*
    enum {
        ACCEL_BUF_VIRT,      // 普通虚拟地址
        ACCEL_BUF_PHYS,      // 物理地址 (老 G2D)
        ACCEL_BUF_DMABUF,    // dma-buf fd
    } buf_type;
    union {
        void    *virt;
        uintptr_t phys;
        int      dmabuf_fd;
    };
};

struct mc_accel_ops {
    int  (*init)(void);
    int  (*blit)(struct mc_accel_surface *dst, int dx, int dy,
                 struct mc_accel_surface *src, int sx, int sy,
                 int w, int h);
    int  (*blend)(struct mc_accel_surface *dst, int dx, int dy,
                  struct mc_accel_surface *src, int sx, int sy,
                  int w, int h, uint8_t global_alpha);
    int  (*fill)(struct mc_accel_surface *dst,
                 int x, int y, int w, int h, uint32_t color);
    int  (*sync)(void);
    void (*deinit)(void);

    uint32_t caps;          // ACCEL_CAP_DMABUF, ACCEL_CAP_ALPHA, ...
};

extern struct mc_accel_ops accel_sunxi_g2d;   // T113/T507
extern struct mc_accel_ops accel_rockchip_rga;// RV1126/RV1106/RK3036
extern struct mc_accel_ops accel_cpu_neon;    // 回退
```

**运行期选择**：读 `/sys/firmware/devicetree/base/compatible` 探测，按编译开关启用。

### 8.2 Allwinner G2D（T113 / T507）

- 设备：`/dev/g2d`，ioctl
- 用户库：`libsunxi-g2d`（BSP）或自封装
- 支持：blit、stretch blit、alpha blend、rotation
- 输入：物理地址或 dma-buf fd（新内核）
- 限制：
  - T113 G2D 单源单目标，多源合成需多次 ioctl
  - 像素格式：ARGB8888、RGB565、YUV 等
  - **dma-buf 输入需要内核驱动支持** `G2D_BLT_FLAG_DMA_BUF`

伪代码：
```c
static int g2d_blend(struct mc_accel_surface *dst, int dx, int dy,
                     struct mc_accel_surface *src, int sx, int sy,
                     int w, int h, uint8_t alpha)
{
    g2d_blt_h blt = {0};
    blt.flag_h = G2D_BLT_PIXEL_ALPHA | G2D_BLT_FLAG_DMA_BUF;
    blt.src_image_h.fd = src->dmabuf_fd;   // 或 .bbuff = phys
    blt.src_image_h.format = G2D_FORMAT_ARGB8888;
    blt.src_image_h.clip_rect = {sx, sy, w, h};
    blt.dst_image_h.fd = dst->dmabuf_fd;
    blt.dst_image_h.clip_rect = {dx, dy, w, h};
    return ioctl(g2d_fd, G2D_CMD_BITBLT_H, &blt);
}
```

### 8.3 Rockchip RGA（RV1126 / RV1106 / RK3036）

- 设备：`/dev/rga`
- 用户库：`librga`（C++）或 `linux-rga` C 封装
- 支持：blit、scale、rotation、color space convert、alpha
- 输入：dma-buf fd（推荐）或 phys/virt
- RGA 版本：
  - RK3036：RGA1（老，性能弱）
  - RV1126：RGA2
  - RV1106：RGA2（精简版）

伪代码：
```c
static int rga_blend(struct mc_accel_surface *dst, int dx, int dy,
                     struct mc_accel_surface *src, int sx, int sy,
                     int w, int h, uint8_t alpha)
{
    rga_info_t s = {0}, d = {0};
    s.fd = src->dmabuf_fd;
    s.mmuFlag = 1;
    s.blend = 0xff0405;  // SRC_OVER
    rga_set_rect(&s.rect, sx, sy, w, h,
                 src->stride / 4, src->h, RK_FORMAT_BGRA_8888);

    d.fd = dst->dmabuf_fd;
    d.mmuFlag = 1;
    rga_set_rect(&d.rect, dx, dy, w, h,
                 dst->stride / 4, dst->h, RK_FORMAT_BGRA_8888);

    return c_RkRgaBlit(&s, &d, NULL);
}
```

### 8.4 CPU NEON 回退

ARM Cortex-A 系列都有 NEON。手写 BGRA8888 over BGRA8888 的 alpha blend：

```c
// 简化版, 实际用 NEON intrinsics 展开
void blend_bgra_neon(uint8_t *dst, uint8_t *src, int n_pixels) {
    // 每次处理 4 像素 = 16 字节
    for (int i = 0; i < n_pixels; i += 4) {
        uint8x16_t s = vld1q_u8(src + i*4);
        uint8x16_t d = vld1q_u8(dst + i*4);
        // alpha = s[3] 等
        // out = s + d * (1 - s_alpha)
        // ...
        vst1q_u8(dst + i*4, out);
    }
}
```

性能基准（A53 1GHz，800×480 全屏 blend）：~3ms。够用。

### 8.5 DMABUF 互操作矩阵

| 链路 | T113 | T507 | RV1126 | RV1106 | RK3036 |
|---|---|---|---|---|---|
| dma-heap 分配 | ✓ (5.4+) | ✓ | ✓ | ✓ | △ (老内核需 ION) |
| G2D 读 dma-buf | ✓ | ✓ | - | - | - |
| RGA 读 dma-buf | - | - | ✓ | ✓ | △ (老 RGA1) |
| EGL 导出 dma-buf | - | ✓ (Mali Bifrost) | - | - | △ (Mali-400) |
| EGL 导入 dma-buf | - | ✓ | - | - | △ |
| FBIOPAN 双 buf | ✓ | ✓ | ✓ | ✓ | ✓ |

T113 / RV1126 / RV1106 没有 GPU，dma-buf 仅用于 CPU 渲染但要给 G2D/RGA 零拷贝消费。
T507 / RK3036 有 GPU，完整 dma-buf 互操作路径可用。

### 8.6 Vsync

- T113/T507：`FBIO_WAITFORVSYNC` ioctl
- RV1126/RV1106：DRM/KMS 提供，fbdev 模拟下也有 `FBIO_WAITFORVSYNC`
- 回退：`timerfd` 16.6ms 周期

---

## 第 9 章 启动器与配置

### 9.1 mc-launcher

简易守护进程：读配置 → 启动 APP → 监控崩溃。

**配置 `/etc/mc/launcher.conf`**：

```ini
[compositor]
binary = /usr/bin/mc-compositor
args   = --config /etc/mc/compositor.conf

[app:dashboard]
binary    = /opt/app/dashboard
autostart = yes
respawn   = yes
respawn_delay   = 1000ms
respawn_max     = 5

[app:settings]
binary    = /opt/app/settings
autostart = no       ; 由 dashboard 通过 bus 触发

[app:alert]
binary    = /opt/app/alert
autostart = no
role_hint = popup
```

### 9.2 Compositor 配置

`/etc/mc/compositor.conf`：

```ini
[display]
width      = 800
height     = 480
format     = bgra8888
fb_device  = /dev/fb0
vsync      = on

[input]
touch_device = /dev/input/event1
calibration  = identity
swap_xy      = no
invert_x     = no
invert_y     = no

[accel]
backend      = auto         ; auto / g2d / rga / cpu
dmabuf_heap  = /dev/dma_heap/system

[security]
socket_mode  = 0660
socket_group = video

[limits]
max_clients              = 16
max_surfaces_per_client  = 4
max_buffer_size_mb       = 8
max_payload_kb           = 64
```

---

## 第 10 章 测试方案

### 10.1 单元测试（Compositor 端）

| 模块 | 测试点 |
|---|---|
| proto | TLV pack/unpack round-trip、边界值、坏数据 |
| surface | 创建/销毁、z-order、buffer 状态机、SHM 和 DMABUF 两种路径 |
| input | hit-test、多 slot tracking、grab 释放 |
| bus | topic 通配匹配、订阅退订、广播 |
| compose | damage 合并、不透明快路径、fence 等待与超时 |

工具：自写 minimal test framework 或 `cmocka`。

### 10.2 集成测试

`mc-test-client` 模拟：
- 多客户端并发连接
- 高频 commit（压测 buffer 同步）
- 异常断开（kill -9）
- 触摸事件注入（uinput）
- dma-buf 路径（如平台支持）

### 10.3 兼容性测试矩阵

| 平台 | LVGL APP | AWTK soft APP | AWTK GL APP | 弹窗 | Bus | dma-buf |
|---|---|---|---|---|---|---|
| T113 | ✓ | ✓ | n/a | ✓ | ✓ | △ |
| T507 | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| RV1126 | ✓ | ✓ | n/a | ✓ | ✓ | △ |
| RV1106 | ✓ | ✓ | n/a | ✓ | ✓ | △ |
| RK3036 | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |

### 10.4 性能基准

| 指标 | 目标 | 测量方法 |
|---|---|---|
| 触摸延迟（evdev→APP） | ≤ 8ms | uinput 注入 + 时间戳 |
| Commit→上屏（SHM） | ≤ 16ms | trace + fb pan |
| Commit→上屏（DMABUF） | ≤ 16ms | 同上 + fence trace |
| CPU 占用（idle） | ≤ 2% | top |
| CPU 占用（60fps 合成） | ≤ 15% | top |
| 内存（mc 常驻） | ≤ 4MB | /proc/pid/status |
| 启动时间 | ≤ 200ms | strace + 时间戳 |

---

## 第 11 章 安全考虑

### 11.1 威胁模型

假设：所有 APP 同公司开发，**不防恶意 APP**。但要防：
- APP bug 导致 Compositor 崩溃
- APP 占用过多资源

### 11.2 防护措施

| 威胁 | 防护 |
|---|---|
| 巨型消息 OOM | `payload_len` 上限 64KB |
| 无限创建 surface | 每 client 最多 4 个 |
| 无限创建 client | 全局最多 16 个 |
| 不读 socket 导致 mc 阻塞 | client socket buffer 满即断开 |
| shm 大小炸 | 单 buffer ≤ `screen_size * 2` |
| 死循环不 commit | 监控可加（N 秒无 commit 警告） |
| EVIOCGRAB 抢不到 | 启动时检查，失败即退出 |
| socket 路径竞争 | 启动时尝试 connect 探活，否则 unlink |
| dma-buf fd 泄漏 | Compositor 合成完成必 close |
| fence_fd 超时 | poll 8ms 超时，跳帧并日志 |

### 11.3 权限

- mc-compositor 以 `video` group 运行（不需要 root，除非 fb 节点限制）
- `/var/run/mc.sock` `0660 root:video`
- APP 进程加入 `video` group
- dma-heap 节点权限：`/dev/dma_heap/system` 默认 root，需提前 udev 规则放权

---

## 第 12 章 实施路线图

### Phase 0：原型验证（1 周）

- [ ] proto pack/unpack
- [ ] socket server + 1 client 握手
- [ ] 单 surface + 单 buffer + CPU blit 到 fb
- [ ] evdev 透传（不做 hit-test）
- [ ] 目标：跑通一个 LVGL APP，能显示能触摸

### Phase 1：核心功能（2 周）

- [ ] 多 surface + z-order
- [ ] 双 buffer + eventfd 同步
- [ ] Hit-test + 输入路由
- [ ] 生命周期（VISIBLE/HIDDEN/REQUEST_FOCUS）
- [ ] POPUP 角色 + modal
- [ ] Bus broker
- [ ] AWTK Software port
- [ ] 目标：LVGL + AWTK 两个 APP 共存，能切换能弹窗

### Phase 2：硬件加速（1 周）

- [ ] G2D backend（T113/T507）
- [ ] RGA backend（RV1126/RV1106）
- [ ] dma-heap shm 分配
- [ ] FBIOPAN 双 fb 切换
- [ ] 目标：60fps @ 800×480，CPU < 15%

### Phase 3：DMABUF + GPU 客户端（1 周，仅 T507/RK3036）

- [ ] DMABUF buf_type 协议路径
- [ ] EGL fence 同步
- [ ] AWTK GL port (T507)
- [ ] AWTK GL port (RK3036, Mali-400)
- [ ] 目标：AWTK 在 T507 上 GPU 渲染流畅运行

### Phase 4：稳健性（1 周）

- [ ] mc-launcher
- [ ] 崩溃恢复、自动重连
- [ ] 配置文件
- [ ] 日志系统（syslog + 文件）
- [ ] 单元测试、集成测试
- [ ] 目标：72 小时压测无崩溃

### Phase 5：增强（按需）

- [ ] 横竖屏切换
- [ ] 动画转场（淡入淡出、滑动）
- [ ] 监控/诊断接口（`mc-ctl status`）

**总工期估算**：单人 6~7 周到 Phase 4 可上线。

---

## 第 13 章 风险与决策记录

### 13.1 风险登记

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| G2D/RGA 驱动不稳定 | 中 | 中 | CPU NEON 回退兜底 |
| LVGL 多进程未踩过坑 | 低 | 中 | port 层 ~120 行，问题易定位 |
| AWTK lcd_mem 接口变更 | 低 | 低 | 锁定 AWTK 版本 |
| T507 Mali EGL fbdev 后端 dmabuf 导出不完整 | 中 | 高 | 回退 PBUFFER + glReadPixels |
| RK3036 Mali-400 驱动老旧 | 高 | 中 | 仅做最小验证；不达标则只用 software 后端 |
| GBM 在 fbdev-only 系统未必可用 | 中 | 中 | 用 dma-heap + EGL_EXT_image_dma_buf_import 自分配 |
| fence 同步出错导致撕裂 | 中 | 中 | 调试期 glFinish 兜底；poll 超时跳帧 |
| 协议设计后期需大改 | 中 | 高 | 留版本号，TLV 易扩展，CAPS 协商能力 |
| dma-heap 内核版本不够 | 中 | 中 | 回退 ION 或 memfd + memcpy |

### 13.2 关键决策

1. **协议自研而非用 Wayland**
   - 原因：Wayland 协议过于复杂（XML codegen、wl_registry、wl_seat…），目标场景不需要
   - 代价：失去生态兼容；但本场景所有 APP 都是自研，无生态需求

2. **TLV 而非 protobuf**
   - 原因：protobuf 引入 ~200KB lib，TLV 自己写 100 行
   - 代价：手写序列化，但可控

3. **单线程 epoll**
   - 原因：多线程复杂度高，单线程在目标负载下足够
   - 代价：单核 CPU 瓶颈；目标平台多核，但单线程仍 < 15% CPU 余量大

4. **Compositor 不做 GL 合成，但支持 GL 客户端（v1.1 修订）**
   - 原因：合成是简单 2D 操作，G2D/RGA 完全胜任；引入 GL 会增加启动延迟、依赖体积、崩溃面
   - 代价：Compositor 无法做 shader 特效；但目标场景不需要
   - **客户端 GL 渲染通过 dma-buf 互操作支持**，不影响客户端能力

5. **强制 BGRA8888 内部格式**
   - 原因：统一像素格式简化合成；AWTK/LVGL 均可对接
   - 代价：800×480 多 1.5MB shm/buffer，可接受

6. **双轨 Buffer（SHM + DMABUF）**
   - 原因：覆盖无 GPU 平台（T113/RV1126/RV1106）和有 GPU 平台（T507/RK3036）
   - 代价：Compositor 多 ~300 行处理 dmabuf；客户端 SDK 多两套 API

7. **不做 GPU 是 Compositor 自身，不限制客户端**
   - 这是 v1.1 相对 v1.0 的关键澄清

---

## 第 14 章 交付物清单

```
mc/
├── compositor/
│   ├── src/
│   │   ├── main.c
│   │   ├── proto.{h,c}
│   │   ├── transport.{h,c}
│   │   ├── surface.{h,c}              # SHM 和 DMABUF 统一
│   │   ├── compose.{h,c}
│   │   ├── input.{h,c}
│   │   ├── bus.{h,c}
│   │   ├── lifecycle.{h,c}
│   │   ├── gal/{fbdev.c}
│   │   ├── ial/{evdev.c}
│   │   └── accel/{g2d.c, rga.c, cpu_neon.c, dmabuf.c}
│   ├── tests/
│   └── Makefile
├── libmc/
│   ├── include/mc.h
│   ├── src/{connect,surface_shm,surface_dmabuf,event,bus,util}.c
│   └── Makefile
├── ports/
│   ├── lvgl/{lv_port_mc.{h,c}, README.md}
│   ├── awtk-soft/{lcd_mc.{h,c}, input_mc.{h,c}, README.md}
│   └── awtk-gl/{lcd_mc_gl.{h,c}, README.md}
├── launcher/
│   ├── mc-launcher.c
│   └── examples/launcher.conf
├── tools/
│   ├── mc-ctl.c            # 命令行诊断
│   └── mc-test-client.c    # 集成测试
├── examples/
│   ├── lvgl-dashboard/
│   ├── awtk-settings-soft/
│   ├── awtk-settings-gl/   # T507/RK3036
│   └── popup-alert/
├── docs/
│   ├── DESIGN-v1.1.md      # 本文档
│   ├── PROTOCOL.md         # 协议详细规范
│   ├── PORTING.md          # 移植指南
│   └── API.md              # libmc API
└── README.md
```

---

## 附录 A 协议消息完整参考

### A.1 消息字段详表

**CL_HELLO (0x01)**
| Tag | 类型 | 必需 | 说明 |
|---|---|---|---|
| 0x0101 NAME | string | ✓ | APP 名（≤ 32 字符） |
| 0x0102 VERSION | u32 | ✓ | (major << 16) \| minor |
| 0x0103 PID | u32 | ✓ | 客户端 PID |
| 0x0104 CAPS | u32 | ✓ | 能力位图 |

**SV_WELCOME (0x81)**
| Tag | 类型 | 说明 |
|---|---|---|
| 0x0101 CLIENT_ID | u32 | 分配给客户端的 ID |
| 0x0201 SCREEN_W | u16 | 屏宽 |
| 0x0202 SCREEN_H | u16 | 屏高 |
| 0x0203 SCREEN_FORMAT | u8 | 推荐格式 |
| 0x010A PIXEL_DPI | u16 | DPI（可选） |
| 0x010D CAPS | u32 | 服务端能力位图 |
| 0x010E MODIFIERS | u64[] | 支持的 dma-buf modifier |

**CL_CREATE_SURFACE (0x02)**
| Tag | 类型 | 必需 | 说明 |
|---|---|---|---|
| 0x0201 WIDTH | u16 | ✓ | |
| 0x0202 HEIGHT | u16 | ✓ | |
| 0x0203 FORMAT | u8 | ✓ | MC_FMT_* |
| 0x0204 ROLE | u8 | ✓ | MC_ROLE_* |
| 0x0205 N_BUF | u8 | ✓ | 1~3 |
| 0x0206 POPUP_X | i16 | POPUP | |
| 0x0207 POPUP_Y | i16 | POPUP | |
| 0x0208 MODAL | u8 | POPUP | |
| 0x0209 BUF_TYPE | u8 | ✓ | MC_BUF_SHM/DMABUF |

**SV_SURFACE_OK (0x82)**
| Tag | 类型 | 说明 |
|---|---|---|
| 0x0301 SID | u32 | 分配的 surface ID |
| 0x0304 STRIDE | u32 | 像素行间距（仅 SHM） |
| 0x0305 SIZE | u32 | buffer 大小（仅 SHM） |
| 0x0205 N_BUF | u8 | |

SCM_RIGHTS：
- SHM 路径：`[shm_fd × N_BUF, event_fd]`
- DMABUF 路径：`[event_fd]`（无 buffer fd）

**CL_COMMIT (0x04)**
| Tag | 类型 | 必需 | 说明 |
|---|---|---|---|
| 0x0301 SID | u32 | ✓ | |
| 0x0302 BUF_IDX | u8 | SHM | shm 路径用 |
| 0x0303 DAMAGE | rect[] | ✓ | 脏区数组 |
| 0x0308 DMABUF_FORMAT | u32 | DMABUF | DRM_FORMAT_* |
| 0x0309 DMABUF_MODIFIER | u64 | DMABUF | |
| 0x030A DMABUF_STRIDE | u32 | DMABUF | |
| 0x030B DMABUF_OFFSET | u32 | DMABUF | |

SCM_RIGHTS（仅 DMABUF）：`[dmabuf_fd, fence_fd?]`

**SV_INPUT (0x85)**
| Tag | 类型 | 说明 |
|---|---|---|
| 0x0301 SID | u32 | 目标 surface |
| 0x0401 TYPE | u8 | DOWN=1/MOVE=2/UP=3/CANCEL=4 |
| 0x0402 X | i16 | 局部坐标 |
| 0x0403 Y | i16 | |
| 0x0404 SLOT | u8 | 多点触摸 slot |
| 0x0405 PRESSURE | u8 | 0~255 |
| 0x0406 TIMESTAMP | u32 | ms |

### A.2 完整 Tag 表

```
通用 (0x01xx):
  0x0101 NAME              string
  0x0102 VERSION           u32
  0x0103 PID               u32
  0x0104 CAPS              u32     客户端能力
  0x010A PIXEL_DPI         u16
  0x010B CODE              u32     错误码
  0x010C MSG               string  错误消息
  0x010D SERVER_CAPS       u32
  0x010E MODIFIERS         u64[]

Surface (0x02xx):
  0x0201 WIDTH             u16
  0x0202 HEIGHT            u16
  0x0203 FORMAT            u8
  0x0204 ROLE              u8
  0x0205 N_BUF             u8
  0x0206 POPUP_X           i16
  0x0207 POPUP_Y           i16
  0x0208 MODAL             u8
  0x0209 BUF_TYPE          u8

Buffer (0x03xx):
  0x0301 SID               u32
  0x0302 BUF_IDX           u8
  0x0303 DAMAGE            rect[]  每个 = i16 x4
  0x0304 STRIDE            u32
  0x0305 SIZE              u32
  0x0306 SEQ               u32
  0x0307 HAS_FOCUS         u8
  0x0308 DMABUF_FORMAT     u32
  0x0309 DMABUF_MODIFIER   u64
  0x030A DMABUF_STRIDE     u32
  0x030B DMABUF_OFFSET     u32

Input (0x04xx):
  0x0401 TYPE              u8
  0x0402 X                 i16
  0x0403 Y                 i16
  0x0404 SLOT              u8
  0x0405 PRESSURE          u8
  0x0406 TIMESTAMP         u32

Lifecycle (0x05xx):
  0x0501 STATE             u8

Bus (0x06xx):
  0x0601 TOPIC             string
  0x0602 PAYLOAD           bytes
  0x0603 SENDER            string
```

### A.3 CAPS 位图

```
CAP_DMABUF             = 1 << 0   支持 dma-buf
CAP_FENCE              = 1 << 1   支持 fence sync
CAP_BUS                = 1 << 2   支持 Bus
CAP_MULTI_SURFACE      = 1 << 3   单 client 多 surface
CAP_ROTATION           = 1 << 4   横竖屏切换
CAP_OPACITY            = 1 << 5   全局透明度
```

### A.4 时序图汇总

#### 启动 & 创建 SHM Surface

```
APP                                  COMPOSITOR
 │ connect("/var/run/mc.sock") ─────────►│
 │ CL_HELLO ───────────────────────────►│
 │◄── SV_WELCOME (cid, screen, caps) ───│
 │ CL_CREATE_SURFACE (buf_type=SHM) ───►│
 │                                       │ memfd_create × 2
 │                                       │ eventfd_create
 │◄── SV_SURFACE_OK ─────────────────────│
 │     + SCM_RIGHTS[shm0, shm1, efd]    │
 │ mmap × 2                              │
 │ epoll_add(efd)                        │
```

#### 创建 DMABUF Surface（T507/RK3036）

```
APP                                  COMPOSITOR
 │ CL_CREATE_SURFACE (buf_type=DMABUF) ►│
 │◄── SV_SURFACE_OK (no buf_fd)         │
 │     + SCM_RIGHTS[efd]                 │
 │                                       │
 │ # APP 自分配 GBM BO × 3              │
 │ # 包装 EGLImage, 绑 FBO              │
 │                                       │
 │ # 渲染循环                            │
 │ glBindFramebuffer(slot[i].fbo)        │
 │ <draw>                                │
 │ EGLSyncKHR sync = ...                 │
 │ int fence_fd = eglDupNativeFence...   │
 │                                       │
 │ CL_COMMIT (dmabuf_fd, fence_fd) ────►│
 │   + SCM_RIGHTS[dmabuf_fd, fence_fd]  │
 │                                       │ poll(fence_fd)
 │                                       │ accel_blend(fb, dmabuf)
 │                                       │ close(dmabuf_fd, fence_fd)
 │◄── write(efd, 1) ─────────────────────│
 │ # 切下一 slot                         │
```

#### 触摸事件分发

```
                COMPOSITOR             APP-fg          APP-bg
                     │                    │              │
   evdev event ──►   │                    │              │
                     │ hit-test           │              │
                     │ -> APP-fg          │              │
                     │ local 坐标转换     │              │
                     │── SV_INPUT ───────►│              │
                     │ (后台 APP 不发)                   │
```

#### 前后台切换

```
APP-A (前台)         COMPOSITOR          APP-B (后台->前台)
   │                     │                     │
   │                     │◄── CL_REQUEST_FOCUS │
   │                     │ z-order 调整         │
   │◄── SV_LIFECYCLE     │                     │
   │     (HIDDEN)        │                     │
   │ 停 lv_timer_handler │                     │
   │── CL_ACK_LIFECYCLE ►│                     │
   │                     │── SV_LIFECYCLE ────►│
   │                     │   (VISIBLE)         │
   │                     │                     │ 恢复 main_loop
   │                     │                     │ 全屏重绘
   │                     │◄── CL_COMMIT ───────│
```

#### 弹窗叠加

```
背景 APP (FULLSCREEN)   COMPOSITOR    弹窗 APP (POPUP, modal)
       │                     │                  │
       │── CL_COMMIT ───────►│                  │
       │                     │◄── CL_CREATE_SURFACE
       │                     │     (role=POPUP, modal=1,
       │                     │      x=200, y=120, w=400, h=240)
       │                     │                  │
       │                     │◄── CL_COMMIT ────│
       │                     │ G2D blit 背景    │
       │                     │ G2D blend 弹窗   │
       │ (输入不再来)         │── SV_INPUT ─────►│
       │                     │ (所有触摸到弹窗)  │
```

---

## 附录 B 平台特性矩阵

| 维度 | T113 | T507 | RV1126 | RV1106 | RK3036 |
|---|---|---|---|---|---|
| CPU | A7 ×2 | A53 ×4 | A7 ×1 | A7 ×1 | A7 ×4 |
| 2D | G2D | G2D | RGA2 | RGA2 mini | RGA1 |
| GPU | - | Mali-G31 MP2 | - | - | Mali-400 MP2 |
| 典型分辨率 | 480/800 | 1080p | 1080p | 1080p | 720p |
| RAM | 64~128M | 256~512M | 256M | 64~128M | 256M+ |
| 内核 | 5.4 | 4.9/5.4 | 4.19 | 5.10 | 4.4 |
| dma-heap | ✓ | ✓ | ✓ | ✓ | △ |
| AWTK 后端 | soft | soft 或 gl | soft | soft | soft 或 gl |
| LVGL 后端 | soft | soft | soft | soft | soft |

---

## 附录 C 术语表

| 术语 | 全称 / 解释 |
|---|---|
| Compositor | 中央合成器，本项目即 `mc-compositor` |
| Surface | 客户端的一个显示区域，对应一个窗口 |
| Buffer | Surface 的像素存储，可以是 shm 或 dmabuf |
| Damage | 脏区，client 通知 compositor 哪些像素变了 |
| TLV | Tag-Length-Value，本协议的载荷格式 |
| SCM_RIGHTS | Unix socket fd-passing 控制消息 |
| GBM | Generic Buffer Manager，DRM 配套的 BO 分配 |
| dma-buf | 跨进程/跨子系统共享 buffer 的内核机制 |
| EGLImage | EGL 中跨进程/跨 API 共享的图像对象 |
| Fence | GPU 同步原语，等待渲染完成 |
| G2D | Allwinner 2D 加速器 |
| RGA | Rockchip Raster Graphic Acceleration |
| FBIOPAN_DISPLAY | fbdev 双 buffer 切换 ioctl |

---

## 结语

v1.1 相对 v1.0 的主要更新：

1. **澄清 "Compositor 不做 GPU" 的范围**：是 Compositor 自身不用 GL 合成，但客户端可以用 GPU 渲染
2. **新增双轨 Buffer 模型**：SHM（软渲染）+ DMABUF（GPU 渲染）
3. **新增 AWTK GL Port 章节**（§7.4）：EGL + GBM + dma-buf 完整链路
4. **协议扩展**：CREATE_SURFACE 增 BUF_TYPE，COMMIT 增 dmabuf/fence 字段，CAPS 协商
5. **DMABUF 互操作矩阵**（§8.5）：各平台能力清晰
6. **路线图新增 Phase 3**：DMABUF + GPU 客户端
7. **决策记录修订**：把 v1.0 第 13 章关于 GPU 的论述细化为两条

下一步建议：

1. 评审本设计文档，确认无大方向问题
2. 按 Phase 0 起步：实现 proto + 单客户端 demo（约 1 周）
3. 平台优先级：T113（无 GPU，简单）→ T507（验证 dmabuf 全链路）→ RV 系列

需要进一步细化的方向（按需）：

- 第 4 章合成算法的具体伪代码到可实现级
- proto 模块完整 C 代码
- T507 EGL + GBM 实际可运行 demo
- AWTK 版本锁定与 patch 列表
