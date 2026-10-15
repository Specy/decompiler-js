
undefined8 main(void)

{
  int iVar1;
  long lVar2;
  undefined8 uVar3;
  long in_FS_OFFSET;
  undefined1 auStack_98 [136];
  long lStack_10;
  
  lStack_10 = *(long *)(in_FS_OFFSET + 0x28);
  printf("Enter password: ");
  lVar2 = fgets(auStack_98,0x80,stdin);
  if (lVar2 == 0) {
    uVar3 = 1;
  }
  else {
    lVar2 = strcspn(auStack_98,0x2015);
    auStack_98[lVar2] = 0;
    iVar1 = strcmp(auStack_98,"hunter2");
    if (iVar1 == 0) {
      puts("Correct!");
    }
    else {
      puts("Wrong!");
    }
    uVar3 = 0;
  }
  if (lStack_10 != *(long *)(in_FS_OFFSET + 0x28)) {
    uVar3 = __stack_chk_fail();
  }
  return uVar3;
}
