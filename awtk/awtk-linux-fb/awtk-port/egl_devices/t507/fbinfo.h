#ifndef _FBINFO_H
#define _FBINFO_H

#include "base/types_def.h"

BEGIN_C_DECLS

int fb_info(const char *filename, int *width, int *height);
EGLNativeWindowType createNativeWindow(unsigned short width, unsigned short height);

END_C_DECLS

#endif



