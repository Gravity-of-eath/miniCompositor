#ifndef __YPS_AWTK_STR_TOOL_H__
#define __YPS_AWTK_STR_TOOL_H__

#include "tkc/wstr.h"
#include "yps_vector.h"
/**
 * @brief 根据指定分隔符分割字符串
 * 
 */
ret_t yps_awtk_wstr_split(wstr_t* input_str, wchar_t* delimiter, yps_vector* output_strs);

#endif