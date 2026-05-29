/**
 * File:   yps_widget_label_register.h
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


#ifndef TK_YPS_WIDGET_LABEL_REGISTER_H
#define TK_YPS_WIDGET_LABEL_REGISTER_H

#include "base/widget.h"

BEGIN_C_DECLS

/**
 * @method  yps_widget_label_register
 * 注册控件。
 *
 * @annotation ["global"]
 *
 * @return {ret_t} 返回RET_OK表示成功，否则表示失败。
 */
ret_t yps_widget_label_register(void);

/**
 * @method  yps_widget_label_supported_render_mode
 * 获取支持的渲染模式。
 *
 * @annotation ["global"]
 *
 * @return {const char*} 返回渲染模式。
 */
const char* yps_widget_label_supported_render_mode(void);

END_C_DECLS

#endif /*TK_YPS_WIDGET_LABEL_REGISTER_H*/
