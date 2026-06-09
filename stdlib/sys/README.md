# `sys` — system-specific parameters

`import sys` to read the command-line arguments of a cheatah program.

| cheatah | Python | meaning |
|---|---|---|
| `sys.argv` | `sys.argv` | the argument list: `argv[0]` is the program name, `argv[1:]` the user arguments |

```python
import io
import sys

# print each argument on its own line
for a in sys.argv {
    io.print(a)
}
io.print("argument count:", len(sys.argv))
```

`sys.argv` is a `list[str]` — index it (`sys.argv[1]`), slice it (`sys.argv[1:]`),
take its `len(...)`, and iterate it like any list.

## When is `sys.argv` populated?

The `cheatah` runtime forwards the program's command-line arguments into
`sys.argv` when it runs a module:

```sh
purrc myprog.purr -o myprog.so       # a loadable module (purrc never emits a binary)
cheatah myprog.so one two three      # sys.argv == ["myprog.so", "one", "two", "three"]
```

The runtime does this through an exported `cheatah_set_argv` hook that the `sys`
module provides, so only programs that `import sys` receive their arguments;
everything else is unaffected.

To run a program as its own command (`myprog one two three`), build a launcher
with `cheatah_add_program()` — it invokes the runtime on the module for you. The
package manager [`biome`](../../pkg-manager/) is built exactly that way.
