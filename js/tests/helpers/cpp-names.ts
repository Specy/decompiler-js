// A C++ fixture's names in their namespaces, from the table beside it
// (fixtures/elf/<fixture>.names.tsv), as a caller with a demangler would pass
// them: each line a symbol, then the namespaces its name is in, outermost
// first, then the name, separated by tabs. The package itself reads no
// mangled name.

export type NameTable = Map<string, string[]>

export function readNameTable(text: string): NameTable {
    const table: NameTable = new Map()
    for (const line of text.split('\n')) {
        if (line === '') continue
        const [symbol, ...path] = line.split('\t')
        table.set(symbol, path)
    }
    return table
}

/** The entry named as the table names its symbol, in its namespaces, with
 * the symbol it had as its `symbol`; unchanged when the table has none. */
export function inNamespace<T extends { name: string }>(
    entry: T,
    table: NameTable
): T & { namespace?: string[]; symbol?: string } {
    const path = table.get(entry.name)
    if (!path) return entry
    const namespace = path.slice(0, -1)
    return { ...entry, name: path[path.length - 1], ...(namespace.length > 0 ? { namespace } : {}), symbol: entry.name }
}
