undefined8 main(void)

{
  int iVar1;
  char *pcVar2;
  undefined8 uVar3;
  size_t sVar4;
  long in_FS_OFFSET;
  char acStack_98 [136];
  long lStack_10;
  
  lStack_10 = *(long *)(in_FS_OFFSET + 0x28);
  printf("Enter password: ");
  pcVar2 = fgets(acStack_98,0x80,stdin);
  if (pcVar2 == (char *)0x0) {
    uVar3 = 1;
  }
  else {
    sVar4 = strcspn(acStack_98,"\n");
    acStack_98[sVar4] = '\0';
    iVar1 = strcmp(acStack_98,"hunter2");
    if (iVar1 == 0) {
      puts("Correct!");
    }
    else {
      puts("Wrong!");
    }
    uVar3 = 0;
  }
  if (lStack_10 != *(long *)(in_FS_OFFSET + 0x28)) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return uVar3;
}
