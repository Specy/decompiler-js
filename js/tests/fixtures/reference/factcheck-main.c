undefined8 main(int param_1)

{
  int iVar1;
  undefined4 uVar2;
  char *pcVar3;
  int *piVar4;
  undefined8 uVar5;
  undefined8 uVar6;
  void *thrown_exception;
  undefined4 *puVar7;
  long *plVar8;
  long in_FS_OFFSET;
  int aiStack_1fc [4];
  undefined1 uStack_1e9;
  int *piStack_1e8;
  undefined1 auStack_1e0 [16];
  undefined1 *puStack_1d0;
  undefined1 *puStack_1c8;
  undefined1 *puStack_1c0;
  undefined1 *puStack_1b8;
  undefined1 *puStack_1b0;
  undefined1 *puStack_1a8;
  undefined1 *puStack_1a0;
  undefined1 auStack_198 [32];
  undefined1 auStack_178 [32];
  undefined1 auStack_158 [48];
  undefined1 auStack_128 [32];
  undefined1 auStack_108 [32];
  undefined1 auStack_e8 [32];
  undefined1 auStack_c8 [32];
  undefined1 auStack_a8 [32];
  undefined1 auStack_88 [32];
  undefined1 auStack_68 [56];
  long lStack_30;
  
  lStack_30 = *(long *)(in_FS_OFFSET + 0x28);
  puStack_1d0 = auStack_158;
  aiStack_1fc[0] = param_1;
  std::string::basic_string<std::allocator<char>>(auStack_128,"picoCTF{wELF_d0N3_mate_",auStack_158)
  ;
  std::__new_allocator<char>::~__new_allocator(auStack_158);
  puStack_1c8 = auStack_158;
  std::string::basic_string<std::allocator<char>>(auStack_108,"1",auStack_158);
  std::__new_allocator<char>::~__new_allocator(auStack_158);
  puStack_1c0 = auStack_158;
  std::string::basic_string<std::allocator<char>>(auStack_e8,"c",auStack_158);
  std::__new_allocator<char>::~__new_allocator(auStack_158);
  puStack_1b8 = auStack_158;
  std::string::basic_string<std::allocator<char>>(auStack_c8,"4",auStack_158);
  std::__new_allocator<char>::~__new_allocator(auStack_158);
  puStack_1b0 = auStack_158;
  std::string::basic_string<std::allocator<char>>(auStack_a8,"9",auStack_158);
  std::__new_allocator<char>::~__new_allocator(auStack_158);
  pcVar3 = (char *)std::string::operator[](auStack_108,0);
  if (*pcVar3 < 'B') {
    std::string::operator+=(auStack_128,auStack_c8);
  }
  pcVar3 = (char *)std::string::operator[](auStack_e8,0);
  if (*pcVar3 != 'A') {
    std::string::operator+=(auStack_128,auStack_a8);
  }
  pcVar3 = (char *)std::string::operator[](auStack_c8,0);
  if (*pcVar3 == 'G') {
    std::string::operator+=(auStack_128,auStack_108);
  }
  else {
    std::string::operator+=(auStack_128,auStack_e8);
  }
  std::string::operator+=(auStack_128,0x7d);
  std::vector<std::string>::vector(auStack_198);
  std::vector<std::string>::push_back(auStack_198,auStack_128);
  std::map<std::string, int>::map(auStack_158);
  iVar1 = aiStack_1fc[0];
  piVar4 = (int *)std::map<std::string, int>::operator[](auStack_158,auStack_128);
  *piVar4 = iVar1;
  piStack_1e8 = aiStack_1fc;
  puStack_1a8 = auStack_1e0;
  std::vector<int>::vector(auStack_178,C.18.0,3,auStack_1e0);
  std::__new_allocator<int>::~__new_allocator(auStack_1e0);
  uVar5 = std::vector<int>::end(auStack_178);
  uVar6 = std::vector<int>::begin(auStack_178);
  std::sort<__gnu_cxx::__normal_iterator<int*, std::vector<int>>, main::'lambda'(int, int)>
            (uVar6,uVar5);
  std::make_unique<game::Boss, char const (&) [5]>(auStack_1e0,"boss");
  puStack_1a0 = &uStack_1e9;
  std::string::basic_string<std::allocator<char>>(auStack_88,"p",&uStack_1e9);
  game::Player::Player(auStack_68,auStack_88);
  std::string::~basic_string(auStack_88);
  std::__new_allocator<char>::~__new_allocator(&uStack_1e9);
  if (5 < aiStack_1fc[0]) {
    thrown_exception = __cxa_allocate_exception(0x10);
    std::runtime_error::runtime_error(thrown_exception,"too many");
    if (lStack_30 != *(long *)(in_FS_OFFSET + 0x28)) {
                    /* WARNING: Subroutine does not return */
      __stack_chk_fail();
    }
                    /* WARNING: Subroutine does not return */
    __cxa_throw(thrown_exception,std::runtime_error::typeinfo,_ZNSt13runtime_errorD1Ev@got);
  }
  uVar5 = greeting();
  uVar5 = std::operator<<<char, std::char_traits<char>, std::allocator<char>>(std::cout,uVar5);
  uVar5 = std::operator<<<std::char_traits<char>>(uVar5,0xa03e);
  puVar7 = (undefined4 *)std::vector<int>::operator[](auStack_178,0);
  uVar2 = game::clampTo<int>(*puVar7,0,10);
  uVar2 = main::'lambda'(int)::operator()(&piStack_1e8,uVar2);
  uVar5 = std::ostream::operator<<(uVar5,uVar2);
  plVar8 = (long *)std::unique_ptr<game::Boss>::operator->(auStack_1e0);
  uVar2 = (**(code **)(*plVar8 + 0x10))(plVar8);
  uVar5 = std::ostream::operator<<(uVar5,uVar2);
  uVar6 = std::unique_ptr<game::Boss>::operator*(auStack_1e0);
  uVar2 = game::operator+(auStack_68,uVar6);
  uVar5 = std::ostream::operator<<(uVar5,uVar2);
  uVar2 = sum(auStack_178);
  uVar5 = std::ostream::operator<<(uVar5,uVar2);
  uVar2 = sum(auStack_198);
  uVar5 = std::ostream::operator<<(uVar5,uVar2);
  uVar6 = std::map<std::string, int>::size(auStack_158);
  uVar5 = std::ostream::operator<<(uVar5,uVar6);
  std::ostream::operator<<(uVar5,_ZSt4endlIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_@got);
  game::Player::~Player(auStack_68);
  std::unique_ptr<game::Boss>::~unique_ptr(auStack_1e0);
  std::vector<int>::~vector(auStack_178);
  std::map<std::string, int>::~map(auStack_158);
  std::vector<std::string>::~vector(auStack_198);
  std::string::~basic_string(auStack_a8);
  std::string::~basic_string(auStack_c8);
  std::string::~basic_string(auStack_e8);
  std::string::~basic_string(auStack_108);
  std::string::~basic_string(auStack_128);
  if (lStack_30 != *(long *)(in_FS_OFFSET + 0x28)) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return 0;
}
