#include "yps_awtk_str_tool.h"
#include <stdlib.h>

ret_t yps_awtk_wstr_split(wstr_t* input_str, wchar_t* delimiter, yps_vector* output_strs) {
  wchar_t tmp_str[128];
  wchar_t* ptr = NULL;
  wchar_t* token = NULL;
 
  if (input_str == NULL || delimiter == NULL || output_strs == NULL) {
    return 0;
  }
  
  token = wcstok(input_str->str, delimiter, &ptr);  // Get first token
  while (token != NULL) {
    memset(tmp_str, 0, 128*sizeof(wchar_t));
    memcpy(tmp_str, token, wcslen(token)*sizeof(wchar_t));
    yps_vector_push_back(output_strs, tmp_str);
    token = wcstok(NULL, delimiter, &ptr);  // Get next token
  }
  return RET_OK;
}
