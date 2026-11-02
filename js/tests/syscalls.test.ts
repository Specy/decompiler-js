// The `syscalls` option: with 'linux', a `syscall` instruction whose number
// the decompiler finds in RAX is a call of that Linux system call, with its
// parameters, rather than Ghidra's operation without inputs.
import { readFile } from 'node:fs/promises'
import { loadDecompiler, type Decompiler, type DecompiledFunction, type ProgramImage } from '../src'

const specsUrl = new URL('../../specs/x86-64/', import.meta.url)

const hex = (text: string) => Uint8Array.from(text.match(/../g)!, (byte) => parseInt(byte, 16))

/**
 * A program written by hand that asks for a letter, as the smallest ones do,
 * calling the kernel directly:
 *
 *   main:      mov eax, 1; mov edi, 1; lea rsi, [rel prompt]; mov edx, 15; syscall
 *              xor eax, eax; xor edi, edi; lea rsi, [rel answer]; mov edx, 16; syscall
 *              cmp byte [rel answer], 'A'; je accepted
 *              lea rsi, [rel retry]; mov edx, 10; jmp print_result
 *   accepted:  lea rsi, [rel accepted_message]; mov edx, 9
 *   print_result:
 *              mov eax, 1; mov edi, 1; syscall
 *              mov eax, 60; xor edi, edi; syscall
 *
 * The exit is the last instruction of its segment. Its strings, written with
 * .ascii, have no terminators, so a literal runs on to the end of them.
 */
const BRANCH: ProgramImage = {
    regions: [
        {
            address: 0x401000,
            bytes: hex(
                'b801000000bf01000000488d35ef0f0000ba0f0000000f0531c031ff488d35ff1f0000ba100000000f05803df11f0000' +
                    '41740e488d35d50f0000ba0a000000eb0c488d35d10f0000ba09000000b801000000bf010000000f05b83c00000031ff0f05'
            ),
            readOnly: true,
            executable: true
        },
        {
            address: 0x402000,
            bytes: new TextEncoder().encode('Type a letter: Try again\nAccepted\n'),
            readOnly: true,
            executable: false
        },
        { address: 0x403022, bytes: new Uint8Array(16), readOnly: false, executable: false }
    ],
    functions: [{ address: 0x401000, name: 'main' }],
    data: [{ address: 0x403022, name: 'answer', size: 1 }],
    strings: [
        { address: 0x402000, length: 15 },
        { address: 0x40200f, length: 10 },
        { address: 0x402019, length: 9 }
    ],
    prototypes: ''
}

/** Small functions, one after another (offsets from 0x401000). */
const SMALL = 0x401000
const FUNCTIONS = {
    unknown: SMALL, //               mov eax, 999; syscall; ret
    fromParameter: SMALL + 0x08, //  mov rax, rdi; syscall; ret
    undeclared: SMALL + 0x0e, //     mov eax, 175; mov edi, 0x1000; mov esi, 0x40; xor edx, edx; mov r10d, 4; syscall; ret
    pread: SMALL + 0x28, //          mov eax, 17; mov edi, 3; xor esi, esi; mov edx, 0x10; mov r10d, 8; syscall; ret
    inARow: SMALL + 0x42, //         mov eax, 39; syscall; mov rdi, rax; mov eax, 62; mov esi, 9; syscall; ret
    stack: SMALL + 0x59 //           sub rsp, 0x18; mov [rsp+8], rdi; mov eax, 1; mov edi, 1; xor esi, esi; nop;
    //                               mov edx, 8; syscall; mov rax, [rsp+8]; add rsp, 0x18; ret
}
const SMALL_IMAGE: ProgramImage = {
    regions: [
        {
            address: SMALL,
            bytes: hex(
                'b8e70300000f05c34889f80f05c3b8af000000bf00100000be4000000031d241ba040000000f05c3b811000000bf03000000' +
                    '31f6ba1000000041ba080000000f05c3b8270000000f054889c7b83e000000be090000000f05c34883ec1848897c2408' +
                    'b801000000bf0100000031f690ba080000000f05488b4424084883c418c3'
            ),
            readOnly: true,
            executable: true
        }
    ],
    functions: Object.entries(FUNCTIONS).map(([name, address]) => ({ address, name })),
    data: [],
    strings: [],
    prototypes: ''
}

let decompiler: Decompiler
let glibc: string

beforeAll(async () => {
    decompiler = await loadDecompiler({ architecture: 'x86-64', specsUrl })
    glibc = await readFile(new URL('../prototypes/glibc.h', import.meta.url), 'utf8')
})

afterAll(() => decompiler.dispose())

function decompiled(image: ProgramImage, address: number, syscalls?: 'none' | 'linux') {
    const program = decompiler.openProgram(image, syscalls && { syscalls })
    try {
        const result = program.decompile(address)
        if ('error' in result) throw new Error(result.error)
        return { result, warnings: program.warnings }
    } finally {
        program.dispose()
    }
}

/** The body's statements, after its declarations, without comments. */
function statements(result: Extract<DecompiledFunction, { lines: unknown }>) {
    const lines = result.lines.map((line) => line.text)
    const body = lines.indexOf('{')
    const declarations = lines.indexOf('  ', body)
    return lines
        .slice((declarations === -1 ? body : declarations) + 1)
        .map((text) => text.trim())
        .filter((text) => text !== '' && text !== '}' && !text.startsWith('/*'))
}

test("without a table, each syscall is Ghidra's operation, as in Ghidra", () => {
    const { result } = decompiled(BRANCH, 0x401000)
    expect(statements(result)).toEqual(['syscall();', 'syscall();', 'syscall();', 'syscall();', 'halt_baddata();'])
})

test('with Linux, the system calls are calls with their parameters, and exit does not return', () => {
    const { result, warnings } = decompiled(BRANCH, 0x401000, 'linux')
    expect(warnings).toEqual([])
    expect(result.text).toBe(
        [
            'void main(void)',
            '',
            '{',
            '  ulong count;',
            '  char *buf;',
            '  ',
            '  sys_write(1,"Type a letter: Try again\\nAccepted\\n",0xf);',
            '  sys_read(0,&answer,0x10);',
            "  if (answer == 'A') {",
            '    buf = "Accepted\\n";',
            '    count = 9;',
            '  }',
            '  else {',
            '    buf = "Try again\\nAccepted\\n";',
            '    count = 10;',
            '  }',
            '  sys_write(1,buf,count);',
            '                    /* WARNING: Subroutine does not return */',
            '  sys_exit(0);',
            '}'
        ].join('\n')
    )
    // each call comes from its syscall instruction
    const exit = result.lines.find((line) => line.text.includes('sys_exit'))!
    expect(exit.addresses).toEqual([0x401060])
    // a system call is not in the program: its name links nowhere
    const line = result.lines.find((line) => line.text.includes('sys_write'))!
    const write = line.tokens.find((token) => line.text.slice(token.start, token.end) === 'sys_write')!
    expect(write.kind).toBe('function')
    expect(write.target).toBeUndefined()
})

test('a number Linux does not have, or one not found, stays the operation, reading the number', () => {
    expect(statements(decompiled(SMALL_IMAGE, FUNCTIONS.unknown, 'linux').result)).toEqual([
        'uVar1 = syscall(999);',
        'return uVar1;'
    ])
    expect(statements(decompiled(SMALL_IMAGE, FUNCTIONS.fromParameter, 'linux').result)).toEqual([
        'uVar1 = syscall(param_1);',
        'return uVar1;'
    ])
})

test('the fourth parameter is in R10, declared or not', () => {
    expect(statements(decompiled(SMALL_IMAGE, FUNCTIONS.pread, 'linux').result)).toEqual([
        'sys_pread64(3,(char *)0x0,0x10,8);',
        'return;'
    ])
    // init_module has no declaration: the decompiler finds its parameters in the syscall convention's registers
    expect(statements(decompiled(SMALL_IMAGE, FUNCTIONS.undeclared, 'linux').result)).toEqual([
        'sys_init_module(0x1000,0x40,0,4);',
        'return;'
    ])
})

test('a system call returns its result in RAX', () => {
    expect(statements(decompiled(SMALL_IMAGE, FUNCTIONS.inARow, 'linux').result)).toEqual([
        'lVar1 = sys_getpid();',
        'sys_kill((int)lVar1,9);',
        'return;'
    ])
})

test('the stack pointer is the same after a system call as before it', () => {
    // The value stored at [rsp+8] before the call is the one loaded after it:
    // without the stack pointer lowered for the call, the load reads 8 bytes
    // above the store, a slot nothing wrote (`return uStack_8;`).
    const { result } = decompiled(SMALL_IMAGE, FUNCTIONS.stack, 'linux')
    expect(result.text).toContain('sys_write(1,')
    expect(statements(result).at(-1)).toBe('return param_1;')
})

test.each([
    ['no prototypes', () => ''],
    // whose `struct dirent` and `struct option` make `dirent` and `option` type names
    ['prototypes/glibc.h', () => glibc]
])('every system call Linux numbers is named, and every declaration applies, with %s', (_, prototypes) => {
    // mov eax, n; syscall, for each number up to Linux 7.0's last and a few
    // past it, those that do not return last.
    const noreturn = [15, 60, 231]
    const numbers = [...Array(480).keys()].filter((n) => !noreturn.includes(n)).concat(noreturn)
    const bytes = new Uint8Array(numbers.length * 7 + 1)
    numbers.forEach((n, i) => bytes.set([0xb8, n & 0xff, n >> 8, 0, 0, 0x0f, 0x05], i * 7))
    bytes[bytes.length - 1] = 0xc3
    const image: ProgramImage = {
        regions: [{ address: SMALL, bytes, readOnly: true, executable: true }],
        functions: [{ address: SMALL, name: 'everything' }],
        data: [],
        strings: [],
        prototypes: prototypes()
    }
    const program = decompiler.openProgram(image, { syscalls: 'linux' })
    const result = program.decompile(SMALL)
    if ('error' in result) throw new Error(result.error)
    expect(program.warnings).toEqual([])
    const calls = result.text.match(/\bsys_\w+\(/g)!
    // 385 numbers in Linux 7.0's table; the first noreturn call (rt_sigreturn) ends the function
    expect(calls).toHaveLength(385 - 2)
    expect(new Set(calls).size).toBe(calls.length)
    expect(result.text).toContain('sys_write(')
    expect(result.text).toContain('sys_rseq_slice_yield(')
    expect(result.text).not.toContain('sys_exit(')
    // the 95 numbers below 480 Linux has no system call for
    expect(result.text.match(/\bsyscall\(/g)).toHaveLength(480 - 385)
    program.dispose()
})

test("a declaration of the caller's that keeps a system call's from applying is a warning", () => {
    const program = decompiler.openProgram({ ...SMALL_IMAGE, prototypes: 'typedef int fd;' }, { syscalls: 'linux' })
    const result = program.decompile(FUNCTIONS.pread)
    if ('error' in result) throw new Error(result.error)
    expect(program.warnings).toEqual([expect.stringMatching(/^sys_pread64: .* \(in `long syscall sys_pread64\(/)])
    // still a call in the syscall convention
    expect(statements(result)).toEqual(['sys_pread64(3,0,0x10,8);', 'return;'])
    program.dispose()
})

test('programs open at once keep their own tables, sharing one translator', () => {
    const linux = decompiler.openProgram(BRANCH, { syscalls: 'linux' })
    const none = decompiler.openProgram(BRANCH)
    const small = decompiler.openProgram(SMALL_IMAGE, { syscalls: 'linux' })
    const text = (program: typeof linux, address: number) => {
        const result = program.decompile(address)
        if ('error' in result) throw new Error(result.error)
        return result.text
    }
    for (let round = 0; round < 2; ++round) {
        expect(text(linux, 0x401000)).toContain('sys_exit(0);')
        expect(text(none, 0x401000)).toContain('halt_baddata();')
        expect(text(small, FUNCTIONS.inARow)).toContain('sys_kill((int)lVar1,9);')
    }
    none.dispose()
    expect(text(linux, 0x401000)).toContain('sys_read(0,&answer,0x10);')
    linux.dispose()
    small.dispose()
})

test('an unknown table is refused', () => {
    expect(() => decompiler.openProgram(SMALL_IMAGE, { syscalls: 'bsd' as 'linux' })).toThrow(
        'Not a system call table: bsd'
    )
})
