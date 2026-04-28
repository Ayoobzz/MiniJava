#include <stdio.h>
#include <stdlib.h>
#include "tgc.h"
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
struct array { int* array; int length; };
tgc_t gc;
int main(int argc, char *argv[]) {
  tgc_start(&gc, &argc);int i;

  i = 0;

  while ((i < 10)) {
    if ((i == 5)) {
      break;
    }
    else {
      
    }
    i = (i + 1);
  }

  printf("%d\n", i);

  tgc_stop(&gc);

  return 0;
}
