void _start(undefined8 param_1,undefined8 param_2,undefined8 param_3)

{
  undefined8 unaff_retaddr;
  undefined1 auStack_8 [8];
  
  __libc_start_main(main,unaff_retaddr,&stack0x00000008,0,0,param_3,auStack_8);
  do {
                    /* WARNING: Do nothing block with infinite loop */
  } while( true );
}
