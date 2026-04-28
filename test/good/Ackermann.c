#include <stdio.h>
#include <stdlib.h>
#include "tgc.h"
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
struct array { int* array; int length; };
tgc_t gc;
struct A;
void* A_ackermann(struct A* this, int m, int n);
struct A {
  void* (**vtable)();
};
void* (*A_vtable[])() = { A_ackermann };
void* A_ackermann(struct A* this, int m, int n) {
  int res;
  if ((!((m < 0)) && !((0 < m)))) {
    res = (n + 1);
  }
  else {
    if ((!((n < 0)) && !((0 < n)))) {
      res = ({ struct A* tmp1 = this; (int) tmp1->vtable[0](tmp1, (m - 1), 1); });
    }
    else {
      res = ({ struct A* tmp1 = this; (int) tmp1->vtable[0](tmp1, (m - 1), ({ struct A* tmp1 = this; (int) tmp1->vtable[0](tmp1, m, (n - 1)); })); });
    }
  }
  return (void*)(res);
}
int main(int argc, char *argv[]) {
  tgc_start(&gc, &argc);
  printf("%d\n", ({ struct A* tmp1 = ({ struct A* res = tgc_calloc(({ extern tgc_t gc; &gc; }), 1, sizeof(*res)); res->vtable = A_vtable; res; }); (int) tmp1->vtable[0](tmp1, 3, 4); }));

  tgc_stop(&gc);

  return 0;
}
