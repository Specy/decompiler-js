// Measures the package as it is published (dist/): the time to load the
// decompiler and open a program, the time to decompile the test fixtures'
// functions, first and warm, and the memory the module holds. Prints a JSON
// summary; js/README.md's Performance section records one run.
//
//   npm run build && npm run measure
//
// "First" is a function's first decompilation in its program, "warm" the
// median of the next ones. Each program is opened with prototypes/glibc.h.
import { readFileSync } from 'node:fs'
import { loadDecompiler } from '../dist/index.mjs'
import { readElfImage } from '../tests/helpers/elf-image.ts'

const fixtures = new URL('../tests/fixtures/elf/', import.meta.url)
const glibc = readFileSync(new URL('../dist/prototypes/glibc.h', import.meta.url), 'utf8')
const ms = (value) => Math.round(value * 10) / 10
const median = (values) => [...values].sort((a, b) => a - b)[Math.floor(values.length / 2)]
const time = (run) => {
    const start = performance.now()
    const result = run()
    return [performance.now() - start, result]
}
const mb = ({ size, used }) => ({ sizeMB: ms(size / 2 ** 20), usedMB: ms(used / 2 ** 20) })

/** Each defined function symbol's size by address, the largest at an address. */
function functionSizes(file) {
    const view = new DataView(file.buffer, file.byteOffset, file.byteLength)
    const u64 = (at) => Number(view.getBigUint64(at, true))
    const shoff = u64(40)
    const shentsize = view.getUint16(58, true)
    const shnum = view.getUint16(60, true)
    const sizes = new Map()
    for (let i = 0; i < shnum; i++) {
        const header = shoff + i * shentsize
        if (view.getUint32(header + 4, true) !== 2) continue // SHT_SYMTAB
        const offset = u64(header + 24)
        const size = u64(header + 32)
        for (let at = offset; at + 24 <= offset + size; at += 24) {
            if ((view.getUint8(at + 4) & 0xf) !== 2 || view.getUint16(at + 6, true) === 0) continue // STT_FUNC, defined
            const address = u64(at + 8)
            sizes.set(address, Math.max(sizes.get(address) ?? 0, u64(at + 16)))
        }
    }
    return sizes
}

/** The image's functions at distinct addresses, each under its first name. */
function distinct(functions) {
    const byAddress = new Map()
    for (const fn of functions) if (!byAddress.has(fn.address)) byAddress.set(fn.address, fn)
    return [...byAddress.values()]
}

function measureFunctions(program, functions, warmRuns) {
    return functions.map((fn) => {
        const [first, result] = time(() => program.decompile(fn.address))
        const warm = []
        for (let i = 0; i < warmRuns(first); i++) warm.push(time(() => program.decompile(fn.address))[0])
        return {
            name: fn.name,
            address: `0x${fn.address.toString(16)}`,
            firstMs: ms(first),
            warmMs: warm.length ? ms(median(warm)) : undefined,
            ...('error' in result ? { error: result.error } : {})
        }
    })
}

const report = { node: process.version, date: new Date().toISOString() }

const [loadMs, decompiler] = await (async () => {
    const start = performance.now()
    const loaded = await loadDecompiler({ architecture: 'x86-64' })
    return [performance.now() - start, loaded]
})()
report.loadMs = ms(loadMs)
report.memoryAfterLoad = mb(decompiler.memory())

const image = (name) => ({ ...readElfImage(new Uint8Array(readFileSync(new URL(name, fixtures)))), prototypes: glibc })

// crackme, named by its symbols.
{
    const crackme = image('crackme')
    const [openMs, program] = time(() => decompiler.openProgram(crackme))
    const [reopenMs, again] = time(() => decompiler.openProgram(crackme))
    again.dispose()
    report.crackme = {
        openMs: ms(openMs),
        openAgainMs: ms(reopenMs),
        functions: measureFunctions(program, distinct(crackme.functions), () => 20)
    }
    program.dispose()
}

// crackme-stripped, named by hand as a disassembler would name it.
{
    const stripped = image('crackme-stripped')
    const named = [
        { address: 0x1100, name: '_start' },
        { address: 0x1130, name: 'FUN_calm_lynx' },
        { address: 0x1160, name: 'FUN_shy_newt' },
        { address: 0x11a0, name: 'FUN_quiet_heron' },
        { address: 0x11e0, name: 'FUN_bold_crane' },
        { address: 0x11e9, name: 'FUN_brave_otter' }
    ]
    const [openMs, program] = time(() => decompiler.openProgram({ ...stripped, functions: [...named, ...stripped.functions] }))
    report.crackmeStripped = {
        openMs: ms(openMs),
        functions: measureFunctions(program, distinct([...named, ...stripped.functions]), () => 20)
    }
    program.dispose()
}

// hello-atexit-static: its 20 largest functions, then every function once.
{
    const file = new Uint8Array(readFileSync(new URL('hello-atexit-static', fixtures)))
    const hello = image('hello-atexit-static')
    const sizes = functionSizes(file)
    const functions = distinct(hello.functions)
    const [openMs, program] = time(() => decompiler.openProgram(hello))
    const largest = [...functions].sort((a, b) => (sizes.get(b.address) ?? 0) - (sizes.get(a.address) ?? 0)).slice(0, 20)
    report.helloAtexitStatic = {
        symbols: hello.functions.length,
        functions: functions.length,
        openMs: ms(openMs),
        largest: measureFunctions(program, largest, (first) => (first > 2000 ? 2 : 5)).map((entry, i) => ({
            ...entry,
            bytes: sizes.get(largest[i].address)
        }))
    }

    const all = []
    // Calls to an address the image names no function at, which the
    // decompiler names itself (func_0x00401000).
    let unnamedCalls = 0
    const [totalMs] = time(() => {
        for (const fn of functions) {
            const [elapsed, result] = time(() => program.decompile(fn.address))
            all.push({ name: fn.name, ms: elapsed, error: 'error' in result ? result.error : undefined })
            if ('error' in result) continue
            for (const line of result.lines)
                for (const token of line.tokens)
                    if (token.kind === 'function' && line.text.slice(token.start, token.end).startsWith('func_0x')) unnamedCalls++
        }
    })
    const times = all.map((entry) => entry.ms).sort((a, b) => a - b)
    const at = (fraction) => ms(times[Math.min(times.length - 1, Math.floor(times.length * fraction))])
    report.helloAtexitStatic.everyFunction = {
        totalMs: ms(totalMs),
        decompiled: all.filter((entry) => !entry.error).length,
        errors: all.filter((entry) => entry.error).map(({ name, error }) => ({ name, error })),
        medianMs: at(0.5),
        p90Ms: at(0.9),
        p99Ms: at(0.99),
        under100Ms: all.filter((entry) => entry.ms < 100).length,
        slowest: [...all]
            .sort((a, b) => b.ms - a.ms)
            .slice(0, 5)
            .map(({ name, ms: elapsed }) => ({ name, ms: ms(elapsed) })),
        unnamedCalls,
        memoryAfter: mb(decompiler.memory())
    }
    program.dispose()
    report.helloAtexitStatic.memoryAfterDispose = mb(decompiler.memory())
}

decompiler.dispose()
console.log(JSON.stringify(report, null, 2))
