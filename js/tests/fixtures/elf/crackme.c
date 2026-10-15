#include <stdio.h>
#include <string.h>

int main(void) {
  char password[128];
  printf("Enter password: ");
  if (!fgets(password, sizeof(password), stdin)) return 1;
  password[strcspn(password, "\n")] = 0;
  if (!strcmp(password, "hunter2")) puts("Correct!");
  else puts("Wrong!");
  return 0;
}
