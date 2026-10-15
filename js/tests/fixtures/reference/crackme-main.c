
xunknown8 main(void)

{
  int4 iVar1;
  int8 iVar2;
  xunknown8 xVar3;
  int8 in_FS_OFFSET;
  xunknown1 axStack_98 [136];
  int8 iStack_10;
  
  iStack_10 = *(int8 *)(in_FS_OFFSET + 0x28);
  printf(0x2004);
  iVar2 = fgets(axStack_98,0x80,xRam0000000000004010);
  if (iVar2 == 0) {
    xVar3 = 1;
  }
  else {
    iVar2 = strcspn(axStack_98,0x2015);
    axStack_98[iVar2] = 0;
    iVar1 = strcmp(axStack_98,0x2017);
    if (iVar1 == 0) {
      puts(0x201f);
    }
    else {
      puts(0x2028);
    }
    xVar3 = 0;
  }
  if (iStack_10 != *(int8 *)(in_FS_OFFSET + 0x28)) {
    xVar3 = __stack_chk_fail();
  }
  return xVar3;
}
