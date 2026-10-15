import { defineConfig } from 'tsdown'

export default defineConfig({
    entry: {
        index: './src/index.ts'
    },
    dts: true,
    format: ['esm'],
    clean: true,
    deps: {
        // The Emscripten module locates decompiler.wasm beside itself at runtime,
        // and at 2 MB the wasm is fetched rather than inlined, so both are copied
        // to dist/wasm/ by scripts/copy-assets.mjs and never bundled.
        neverBundle: ['./wasm/decompiler.mjs']
    }
})
