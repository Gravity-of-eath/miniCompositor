/**
 * File:   lcd_linux_egl.c
 * Author: AWTK Develop Team
 * Brief:  linux egl lcd
 *
 * Copyright (c) 2020 - 2025 Guangzhou ZHIYUAN Electronics Co.,Ltd.
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
 * 2020-11-06 Lou ZhiMing <luozhiming@zlg.com> created
 *
 */

#ifdef WITH_LINUX_EGL

#include <signal.h>
#include "tkc/mem.h"
#include "glad/glad.h"
#include "awtk_global.h"
#include "lcd_linux_egl.h"
#include "../egl_devices/egl_devices.h"
#include "native_window/native_window_fb_gl.h"
#include "base/window_manager.h"
#include "base/native_window.h"
#include "window_manager/window_manager_default.h"

static lcd_egl_context_t* s_egl_context_lcd = NULL;

static void on_app_exit(void) {

}

static void on_signal_int(int sig) {
  tk_quit();
}

static ret_t lcd_linux_gles_swap_buffer(native_window_t* win) {
  lcd_egl_context_t* lcd = NULL;
  return_value_if_fail(win != NULL, RET_BAD_PARAMS);

  lcd = (lcd_egl_context_t*)(win->handle);
  return_value_if_fail(lcd != NULL, RET_BAD_PARAMS);

  return egl_devices_swap_buffers(lcd->elg_ctx);
}

static ret_t lcd_linux_gles_make_current(native_window_t* win) {
  ret_t ret = RET_OK;
  lcd_egl_context_t* lcd = NULL;
  return_value_if_fail(win != NULL, RET_BAD_PARAMS);

  lcd = (lcd_egl_context_t*)(win->handle);
  return_value_if_fail(lcd != NULL, RET_BAD_PARAMS);

  ret = egl_devices_make_current(lcd->elg_ctx);

  if (ret == RET_OK) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glViewport(0, 0, lcd->w, lcd->h);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
  }
  return ret;
}

static ret_t lcd_linux_gles_destroy(native_window_t* win) {
  ret_t ret = RET_OK;
  lcd_egl_context_t* lcd = NULL;
  return_value_if_fail(win != NULL, RET_BAD_PARAMS);

  lcd = (lcd_egl_context_t*)(win->handle);
  return_value_if_fail(lcd != NULL, RET_BAD_PARAMS);

  ret = egl_devices_dispose(lcd->elg_ctx);
  if (ret == RET_OK) {
    TKMEM_FREE(lcd);
  }
  return ret;
}

/* Weak hook: backends can override to receive each frame's bounding-box
 * dirty rect, captured here in begin_frame. The default no-op keeps
 * non-mc backends (egl_for_t507, x11, etc.) unaffected. */
__attribute__((weak))
void lcd_egl_on_dirty_rect(int x, int y, int w, int h) {
  (void)x; (void)y; (void)w; (void)h;
}

static ret_t (*lcd_egl_linux_begin_frame_default)(lcd_t* lcd, const dirty_rects_t* drs);
static ret_t lcd_egl_linux_begin_frame(lcd_t* lcd, const dirty_rects_t* drs) {
  /* AWTK passes drs=NULL when lcd->support_dirty_rect=FALSE (our case;
   * we MUST keep it FALSE to ensure full-FBO GL render every frame --
   * see comment in lcd_linux_egl_create). But the dirty rect data is
   * still tracked separately on the native_window. Read it from there
   * to give mc a smaller commit damage rect, so the compositor only
   * blits the changed pixels to fb. Visually-correct (full FBO render)
   * + cheap compose (small fb write) is the goal. */
  rect_t r = { 0, 0, lcd->w, lcd->h };
  widget_t* wm = window_manager();
  if (wm != NULL) {
    window_manager_default_t* wmd = WINDOW_MANAGER_DEFAULT(wm);
    if (wmd != NULL && wmd->native_window != NULL) {
      rect_t nr = wmd->native_window->dirty_rects.max;
      if (nr.w > 0 && nr.h > 0) r = nr;
    }
  }
  lcd_egl_on_dirty_rect(r.x, r.y, r.w, r.h);
  (void)drs;  /* drs is NULL with our setup; intentional. */
  if (lcd_egl_linux_begin_frame_default != NULL) {
    return lcd_egl_linux_begin_frame_default(lcd, drs);
  }
  return RET_OK;
}

static ret_t (*lcd_egl_linux_resize_default)(lcd_t* lcd, wh_t w, wh_t h, uint32_t line_length);
static ret_t lcd_egl_linux_resize(lcd_t* lcd, wh_t w, wh_t h, uint32_t line_length) {
  ret_t ret = RET_OK;

  ret = egl_devices_resize(s_egl_context_lcd->elg_ctx, w, h);
  return_value_if_fail(ret == RET_OK, ret);

  s_egl_context_lcd->w = w;
  s_egl_context_lcd->h = h;

  if (lcd_egl_linux_resize_default != NULL) {
    lcd_egl_linux_resize_default(lcd, w, h, line_length);
  }

  return ret;
}

lcd_egl_context_t* lcd_linux_egl_create(const char* filename) {
  native_window_t* win = NULL;
  lcd_egl_context_t* lcd = TKMEM_ZALLOC(lcd_egl_context_t);
  return_value_if_fail(lcd != NULL, NULL);

  lcd->elg_ctx = egl_devices_create(filename);
  goto_error_if_fail(lcd->elg_ctx != NULL);

  lcd->w = egl_devices_get_width(lcd->elg_ctx);
  lcd->h = egl_devices_get_height(lcd->elg_ctx);
  lcd->ratio = egl_devices_get_ratio(lcd->elg_ctx);

  win = native_window_fb_gl_init(lcd->w, lcd->h, lcd->ratio);
  goto_error_if_fail(win != NULL);

  lcd_t* lcd_nanovg = native_window_get_lcd(win);
  goto_error_if_fail(lcd_nanovg != NULL);

  lcd_egl_linux_resize_default = lcd_nanovg->resize;
  lcd_nanovg->resize = lcd_egl_linux_resize;
  lcd_egl_linux_begin_frame_default = lcd_nanovg->begin_frame;
  lcd_nanovg->begin_frame = lcd_egl_linux_begin_frame;
  /* Leave lcd->support_dirty_rect at its default FALSE (set in
   * lcd_vgcanvas.inc). On a GL FBO double-buffer setup we MUST render
   * every frame's full FBO -- a partial redraw clipped to dirty rect
   * would leave stale pixels in the other buffer and produce flicker
   * outside the dirty rect on every page flip. The mc backend pays the
   * cost of a full-screen commit each frame in exchange for visual
   * correctness; compositor-side rate limiting (MC_COMPOSE_HZ) keeps
   * the load bounded. */

  s_egl_context_lcd = lcd;
  win->handle = (void*)lcd;
  native_window_fb_gl_set_swap_buffer_func(win, lcd_linux_gles_swap_buffer);
  native_window_fb_gl_set_make_current_func(win, lcd_linux_gles_make_current);
  native_window_fb_gl_set_destroy_func(win, lcd_linux_gles_destroy);

  atexit(on_app_exit);
  signal(SIGINT, on_signal_int);
  signal(SIGTERM, on_signal_int);

  return lcd;
error :
  native_window_fb_gl_deinit();
  return NULL;
}

#endif /*WITH_LINUX_EGL*/
