#include "spine2d/spine2d.h"
#include "gtest/gtest.h"

TEST(spine2d, basic) {
  value_t v;
  widget_t* w = spine2d_create(NULL, 10, 20, 30, 40);

  widget_destroy(w);
}
