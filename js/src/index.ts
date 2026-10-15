// Ghidra's decompiler in WebAssembly. The caller describes a program (its
// memory and its function names) and asks for a function's C.
//
// This is the first, plain-text form of the API: types, data names, strings,
// prototypes and the token-to-address markup are still to come.
import createDecompilerModule from './wasm/decompiler.mjs'
import type { DecompilerModule } from './wasm/decompiler.mjs'

export type Architecture = 'x86-64'

export interface LoadDecompilerOptions {
    architecture: Architecture
    /** decompiler.wasm; by default the copy beside the package's module. */
    wasmUrl?: string | URL
    /** The directory holding the architecture's processor description; by
     * default the package's specs/<architecture>/. */
    specsUrl?: string | URL
}

/** A run of the program's memory at its link-time address. */
export interface ProgramRegion {
    address: number
    bytes: Uint8Array
    readOnly: boolean
    executable: boolean
}

/** A function's entry point and the name the decompiler prints for it. At an
 * address named twice, the first name given is kept. */
export interface ProgramFunction {
    address: number
    name: string
}

export interface ProgramImage {
    regions: ProgramRegion[]
    functions: ProgramFunction[]
}

export type DecompiledFunction = { address: number; text: string } | { address: number; error: string }

export interface DecompiledProgram {
    /** The C of the function starting at the address, one of the image's
     * functions, or the reason there is none. */
    decompile(address: number): DecompiledFunction
    /** Frees the program's copy of the image. */
    dispose(): void
}

export interface Decompiler {
    /** The Ghidra release the decompiler was built from and its commit. */
    readonly version: string
    readonly architecture: Architecture
    openProgram(image: ProgramImage): DecompiledProgram
    dispose(): void
}

interface ArchitectureFiles {
    language: string
    files: string[]
    defaultUrls: () => URL[]
}

// Each URL is written out in full, so bundlers that copy what
// `new URL('…', import.meta.url)` names find every file.
const architectures: Record<Architecture, ArchitectureFiles> = {
    'x86-64': {
        language: 'x86:LE:64:default:gcc',
        files: ['x86.ldefs', 'x86-64.pspec', 'x86-64-gcc.cspec', 'x86-64.sla'],
        defaultUrls: () => [
            new URL('./specs/x86-64/x86.ldefs', import.meta.url),
            new URL('./specs/x86-64/x86-64.pspec', import.meta.url),
            new URL('./specs/x86-64/x86-64-gcc.cspec', import.meta.url),
            new URL('./specs/x86-64/x86-64.sla', import.meta.url)
        ]
    }
}

const READ_ONLY = 1
const EXECUTABLE = 2

async function fetchBytes(url: URL): Promise<Uint8Array> {
    if (url.protocol === 'file:') {
        const { readFile } = await import('node:fs/promises')
        return new Uint8Array(await readFile(url))
    }
    const response = await fetch(url)
    if (!response.ok) throw new Error(`Could not fetch ${url}: ${response.status} ${response.statusText}`)
    return new Uint8Array(await response.arrayBuffer())
}

/** A directory's URL, relative ones resolved against the page. */
function directoryUrl(url: string | URL): URL {
    const href = String(url)
    return new URL(href.endsWith('/') ? href : `${href}/`, globalThis.location?.href)
}

export async function loadDecompiler(options: LoadDecompilerOptions): Promise<Decompiler> {
    const architecture = architectures[options.architecture]
    if (!architecture) throw new Error(`Unknown architecture: ${options.architecture}`)
    const specUrls =
        options.specsUrl === undefined
            ? architecture.defaultUrls()
            : architecture.files.map((file) => new URL(file, directoryUrl(options.specsUrl!)))
    const wasmUrl = options.wasmUrl
    const [module, specs] = await Promise.all([
        createDecompilerModule(
            wasmUrl === undefined
                ? {}
                : { locateFile: (path) => (path.endsWith('.wasm') ? String(wasmUrl) : path) }
        ),
        Promise.all(specUrls.map(fetchBytes))
    ])

    const directory = `/specs/${options.architecture}`
    module.FS.mkdirTree(directory)
    architecture.files.forEach((file, i) => module.FS.writeFile(`${directory}/${file}`, specs[i]))
    withString(module, directory, (pointer) => {
        if (!module._decompiler_add_specs(pointer)) throw new Error(lastError(module))
    })

    const version = module.UTF8ToString(module._decompiler_version())
    const programs = new Set<{ dispose(): void }>()
    let disposed = false
    const live = () => {
        if (disposed) throw new Error('The decompiler was disposed')
    }

    return {
        version,
        architecture: options.architecture,
        openProgram(image) {
            live()
            const handle = buildProgram(module, image, architecture.language)
            let open = true
            const program: DecompiledProgram = {
                decompile(address) {
                    if (!open) throw new Error('The program was disposed')
                    const text = module._program_decompile(handle, address)
                    if (text === 0) return { address, error: lastError(module) }
                    try {
                        return { address, text: module.UTF8ToString(text) }
                    } finally {
                        module._free(text)
                    }
                },
                dispose() {
                    if (!open) return
                    open = false
                    programs.delete(program)
                    module._program_close(handle)
                }
            }
            programs.add(program)
            return program
        },
        dispose() {
            if (disposed) return
            for (const program of [...programs]) program.dispose()
            disposed = true
        }
    }
}

function buildProgram(module: DecompilerModule, image: ProgramImage, language: string): number {
    const handle = module._image_new()
    try {
        for (const region of image.regions) {
            const pointer = module._malloc(Math.max(region.bytes.length, 1))
            try {
                module.HEAPU8.set(region.bytes, pointer)
                const flags = (region.readOnly ? READ_ONLY : 0) | (region.executable ? EXECUTABLE : 0)
                module._image_add_region(handle, region.address, pointer, region.bytes.length, flags)
            } finally {
                module._free(pointer)
            }
        }
        for (const fn of image.functions) {
            withString(module, fn.name, (pointer) => module._image_add_function(handle, fn.address, pointer))
        }
    } catch (error) {
        module._image_delete(handle)
        throw error
    }
    // program_open takes the image over, whether or not it succeeds.
    const program = withString(module, language, (pointer) => module._program_open(handle, pointer))
    if (program === 0) throw new Error(lastError(module))
    return program
}

function withString<T>(module: DecompilerModule, text: string, use: (pointer: number) => T): T {
    const pointer = module.stringToNewUTF8(text)
    try {
        return use(pointer)
    } finally {
        module._free(pointer)
    }
}

function lastError(module: DecompilerModule): string {
    return module.UTF8ToString(module._decompiler_error())
}
