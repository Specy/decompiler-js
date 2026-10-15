#include <stdio.h>
#include <stdlib.h>
static void farewell(void) { puts("atexit handler"); }
int main(int argc, char **argv) {
  atexit(farewell);
  printf("hello %s argc=%d\n", argv[0], argc);
  return 3;
}
