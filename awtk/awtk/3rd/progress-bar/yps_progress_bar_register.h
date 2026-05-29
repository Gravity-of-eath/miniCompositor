/**
 * File:   yps_progress_bar_register.h
 * Author: yps
 * Brief:  yps_progress_bar
 *
 * Copyright (c) 2024 - 2024 
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
 * 2024-7-4 taylor.yao created
 *
 */


#ifndef TK_YPS_PROGRESS_BAR_REGISTER_H
#define TK_YPS_PROGRESS_BAR_REGISTER_H

#include "base/widget.h"

BEGIN_C_DECLS

/**
 * @method  yps_progress_bar_register
 * 注册控件。
 *
 * @annotation ["global"]
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_progress_bar_register(void);

/**
 * @method  yps_progress_bar_supported_render_mode
 * 获取支持的渲染模式。
 *
 * @annotation ["global"]
 *
 * @return {const char*} 返回渲染模式。
 */
const char* yps_progress_bar_supported_render_mode(void);

END_C_DECLS

#endif /*TK_YPS_PROGRESS_BAR_REGISTER_H*/
