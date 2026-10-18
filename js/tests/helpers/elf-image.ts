// Reads a little-endian ELF64 executable into the regions and function names
// a program image takes. The same rules as host/native/elf_image.cc, which
// made the native reference, so the wasm decompiles the image the reference
// was decompiled from.

export interface Region {
    address: number
    bytes: Uint8Array
    readOnly: boolean
    executable: boolean
}

export interface FunctionName {
    address: number
    name: string
}

export interface DataName {
    address: number
    name: string
    size?: number
}

export interface StringRange {
    address: number
    length: number
}

export interface Pointer {
    address: number
    name: string
}

export interface ElfImage {
    regions: Region[]
    functions: FunctionName[]
    data: DataName[]
    strings: StringRange[]
    pointers: Pointer[]
}

const PT_LOAD = 1
const PF_X = 1
const PF_W = 2
const SHT_SYMTAB = 2
const SHT_RELA = 4
const SHT_DYNSYM = 11
const STT_NOTYPE = 0
const STT_OBJECT = 1
const STT_FUNC = 2
const STT_GNU_IFUNC = 10
const R_X86_64_GLOB_DAT = 6
const R_X86_64_JUMP_SLOT = 7

interface Section {
    name: string
    nameOffset: number
    type: number
    link: number
    address: number
    offset: number
    size: number
    entsize: number
}

interface Symbol {
    name: string
    type: number
    binding: number
    shndx: number
    value: number
    size: number
}

/** Regions: each PT_LOAD segment at its link-time address, its file bytes
 * followed by zeros up to its memory size. Functions: every defined STT_FUNC
 * symbol, GLOBAL before WEAK before LOCAL at a shared address, then each PLT
 * stub named after the symbol of the relocation filling its GOT slot. Data:
 * every defined STT_OBJECT symbol without its version (`stdin`), with its
 * size, in the same order; then the GOT slot of each function (a GLOB_DAT or
 * JUMP_SLOT relocation of a function, an ifunc or an untyped symbol), named
 * `name@got`; then the strings of `.rodata` (each NUL-terminated run of
 * printable bytes) that have a word in them, named `s_` and their first 24
 * characters as a word. The slots and strings are named as the editor's
 * Listing names them. Strings: those runs, their NUL included. Pointers:
 * those GOT slots, each holding the address of its function. */
export function readElfImage(file: Uint8Array): ElfImage {
    const view = new DataView(file.buffer, file.byteOffset, file.byteLength)
    const u8 = (at: number) => view.getUint8(at)
    const u16 = (at: number) => view.getUint16(at, true)
    const u32 = (at: number) => view.getUint32(at, true)
    const u64 = (at: number) => Number(view.getBigUint64(at, true))
    const cstring = (at: number) => {
        const end = file.indexOf(0, at)
        if (end < 0) throw new Error('ELF string is unterminated')
        return new TextDecoder().decode(file.subarray(at, end))
    }

    if (u32(0) !== 0x464c457f) throw new Error('not an ELF file')
    if (u8(4) !== 2 || u8(5) !== 1) throw new Error('only little-endian ELF64 files are supported')
    const phoff = u64(32)
    const shoff = u64(40)
    const phentsize = u16(54)
    const phnum = u16(56)
    const shentsize = u16(58)
    const shnum = u16(60)
    const shstrndx = u16(62)

    const regions: Region[] = []
    for (let i = 0; i < phnum; i++) {
        const at = phoff + i * phentsize
        if (u32(at) !== PT_LOAD) continue
        const flags = u32(at + 4)
        const offset = u64(at + 8)
        const filesz = u64(at + 32)
        const memsz = u64(at + 40)
        if (memsz === 0) continue
        const bytes = new Uint8Array(Math.max(filesz, memsz))
        bytes.set(file.subarray(offset, offset + filesz))
        regions.push({
            address: u64(at + 16),
            bytes,
            readOnly: (flags & PF_W) === 0,
            executable: (flags & PF_X) !== 0
        })
    }

    const sections: Section[] = []
    for (let i = 0; i < shnum; i++) {
        const at = shoff + i * shentsize
        sections.push({
            name: '',
            nameOffset: u32(at),
            type: u32(at + 4),
            address: u64(at + 16),
            offset: u64(at + 24),
            size: u64(at + 32),
            link: u32(at + 40),
            entsize: u64(at + 56)
        })
    }
    const names = sections[shstrndx]
    if (names) {
        for (const section of sections) {
            section.name = section.nameOffset < names.size ? cstring(names.offset + section.nameOffset) : ''
        }
    }

    const readSymbols = (table: Section): Symbol[] => {
        const strings = sections[table.link]
        if (!strings) throw new Error('ELF symbol table has no strings')
        const symbols: Symbol[] = []
        for (let at = table.offset; at + 24 <= table.offset + table.size; at += 24) {
            const name = u32(at)
            const info = u8(at + 4)
            symbols.push({
                name: name < strings.size ? cstring(strings.offset + name) : '',
                type: info & 0xf,
                binding: info >> 4,
                shndx: u16(at + 6),
                value: u64(at + 8),
                size: u64(at + 16)
            })
        }
        return symbols
    }
    const isSymbolTable = (section: Section | undefined) =>
        section !== undefined && (section.type === SHT_SYMTAB || section.type === SHT_DYNSYM)

    const bindingRank = (binding: number) => (binding === 1 ? 0 : binding === 2 ? 1 : binding === 0 ? 2 : 3)
    const candidates: { rank: number; order: number; function: FunctionName }[] = []
    const objects: { rank: number; order: number; data: DataName }[] = []
    for (const section of sections) {
        if (!isSymbolTable(section)) continue
        for (const symbol of readSymbols(section)) {
            if (symbol.shndx === 0 || symbol.value === 0 || !symbol.name) continue
            if (symbol.type === STT_FUNC) {
                candidates.push({
                    rank: bindingRank(symbol.binding),
                    order: candidates.length,
                    function: { address: symbol.value, name: symbol.name }
                })
            } else if (symbol.type === STT_OBJECT) {
                objects.push({
                    rank: bindingRank(symbol.binding),
                    order: objects.length,
                    data: { address: symbol.value, name: symbol.name.split('@')[0], size: symbol.size }
                })
            }
        }
    }
    candidates.sort((a, b) => a.rank - b.rank || a.order - b.order)
    const functions = candidates.map((candidate) => candidate.function)

    const slotNames = new Map<number, string>()
    const functionSlots = new Map<number, string>()
    for (const section of sections) {
        if (section.type !== SHT_RELA || !isSymbolTable(sections[section.link])) continue
        const symbols = readSymbols(sections[section.link])
        for (let at = section.offset; at + 24 <= section.offset + section.size; at += 24) {
            const info = view.getBigUint64(at + 8, true)
            const symbolIndex = Number(info >> 32n)
            if (symbolIndex === 0 || symbolIndex >= symbols.length) continue
            const symbol = symbols[symbolIndex]
            const slot = u64(at)
            if (!symbol.name) continue
            if (!slotNames.has(slot)) slotNames.set(slot, symbol.name)
            const type = Number(info & 0xffffffffn)
            const isFunction =
                symbol.type === STT_FUNC || symbol.type === STT_NOTYPE || symbol.type === STT_GNU_IFUNC
            if ((type === R_X86_64_GLOB_DAT || type === R_X86_64_JUMP_SLOT) && isFunction && !functionSlots.has(slot))
                functionSlots.set(slot, symbol.name.split('@')[0])
        }
    }
    for (const section of sections) {
        if (section.name !== '.plt' && section.name !== '.plt.sec' && section.name !== '.plt.got') continue
        const step = section.entsize !== 0 ? section.entsize : 16
        for (let stub = 0; stub + step <= section.size; stub += step) {
            const at = section.offset + stub
            let i = 0
            if (u32(at) === 0xfa1e0ff3) i += 4 // endbr64
            if (u8(at + i) === 0xf2) i += 1 // bnd
            if (u8(at + i) !== 0xff || u8(at + i + 1) !== 0x25) continue
            const next = section.address + stub + i + 6
            const name = slotNames.get(next + view.getInt32(at + i + 2, true))
            if (name) functions.push({ address: section.address + stub, name })
        }
    }

    objects.sort((a, b) => a.rank - b.rank || a.order - b.order)
    const data = objects.map((object) => object.data)
    const taken = new Set(data.map((entry) => entry.name))
    const pointers: Pointer[] = []
    for (const [address, name] of [...functionSlots].sort((a, b) => a[0] - b[0])) {
        data.push({ address, name: `${name}@got`, size: 8 })
        pointers.push({ address, name })
    }
    const strings: StringRange[] = []
    const printable = (byte: number) => (byte >= 0x20 && byte < 0x7f) || byte === 9 || byte === 10 || byte === 13
    for (const section of sections) {
        if (section.name !== '.rodata') continue
        let start = -1
        for (let i = 0; i < section.size; i++) {
            const byte = file[section.offset + i]
            if (byte === 0 && start >= 0) {
                const text = new TextDecoder().decode(file.subarray(section.offset + start, section.offset + i))
                strings.push({ address: section.address + start, length: i + 1 - start })
                const word = text
                    .slice(0, 24)
                    .replace(/[^A-Za-z0-9]+/g, '_')
                    .replace(/^_+|_+$/g, '')
                if (word) {
                    let name = `s_${word}`
                    for (let n = 2; taken.has(name); n++) name = `s_${word}_${n}`
                    taken.add(name)
                    data.push({ address: section.address + start, name, size: i + 1 - start })
                }
            }
            if (byte === 0 || !printable(byte)) start = -1
            else if (start < 0) start = i
        }
    }

    return { regions, functions, data, strings, pointers }
}
