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

export interface ElfImage {
    regions: Region[]
    functions: FunctionName[]
}

const PT_LOAD = 1
const PF_X = 1
const PF_W = 2
const SHT_SYMTAB = 2
const SHT_RELA = 4
const SHT_DYNSYM = 11
const STT_FUNC = 2

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
}

/** Regions: each PT_LOAD segment at its link-time address, its file bytes
 * followed by zeros up to its memory size. Functions: every defined STT_FUNC
 * symbol, GLOBAL before WEAK before LOCAL at a shared address, then each PLT
 * stub named after the symbol of the relocation filling its GOT slot. */
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
                value: u64(at + 8)
            })
        }
        return symbols
    }
    const isSymbolTable = (section: Section | undefined) =>
        section !== undefined && (section.type === SHT_SYMTAB || section.type === SHT_DYNSYM)

    const bindingRank = (binding: number) => (binding === 1 ? 0 : binding === 2 ? 1 : binding === 0 ? 2 : 3)
    const candidates: { rank: number; order: number; function: FunctionName }[] = []
    for (const section of sections) {
        if (!isSymbolTable(section)) continue
        for (const symbol of readSymbols(section)) {
            if (symbol.type !== STT_FUNC || symbol.shndx === 0 || symbol.value === 0 || !symbol.name) continue
            candidates.push({
                rank: bindingRank(symbol.binding),
                order: candidates.length,
                function: { address: symbol.value, name: symbol.name }
            })
        }
    }
    candidates.sort((a, b) => a.rank - b.rank || a.order - b.order)
    const functions = candidates.map((candidate) => candidate.function)

    const slotNames = new Map<number, string>()
    for (const section of sections) {
        if (section.type !== SHT_RELA || !isSymbolTable(sections[section.link])) continue
        const symbols = readSymbols(sections[section.link])
        for (let at = section.offset; at + 24 <= section.offset + section.size; at += 24) {
            const symbolIndex = Number(view.getBigUint64(at + 8, true) >> 32n)
            if (symbolIndex === 0 || symbolIndex >= symbols.length) continue
            const name = symbols[symbolIndex].name
            const slot = u64(at)
            if (name && !slotNames.has(slot)) slotNames.set(slot, name)
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

    return { regions, functions }
}
