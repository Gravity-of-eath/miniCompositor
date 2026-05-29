#ifndef TK_INPUT_THREAD_MC_H
#define TK_INPUT_THREAD_MC_H

#include "tkc/thread.h"
#include "input_dispatcher.h"

BEGIN_C_DECLS

tk_thread_t *input_thread_mc_run(input_dispatch_t dispatch, void *ctx,
                                 int32_t w, int32_t h);

END_C_DECLS

#endif /* TK_INPUT_THREAD_MC_H */
