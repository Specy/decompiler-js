// Types of the Emscripten module scripts/compile_decompiler.sh builds: the
// exports listed there (host/wasm/exports.cc) and the runtime methods used.
// Pointers are offsets into HEAPU8; addresses are doubles.

export interface DecompilerModule {
    HEAPU8: Uint8Array
    FS: {
        mkdirTree(path: string): void
        writeFile(path: string, data: Uint8Array | string): void
    }
    UTF8ToString(pointer: number): string
    stringToNewUTF8(text: string): number

    _malloc(size: number): number
    _free(pointer: number): void
    _decompiler_version(): number
    _decompiler_error(): number
    _decompiler_add_specs(directory: number): number
    _program_open(image: number, size: number, languageId: number): number
    _program_warnings(program: number): number
    _program_decompile(program: number, address: number): number
    _program_close(program: number): void
}

export interface DecompilerModuleOptions {
    /** Where the module finds decompiler.wasm; defaults to beside decompiler.mjs. */
    locateFile?: (path: string, scriptDirectory: string) => string
}

export default function createDecompilerModule(options?: DecompilerModuleOptions): Promise<DecompilerModule>
