# Krom

[Kore](https://github.com/Kode/Kore) + [Chakra](https://github.com/Microsoft/ChakraCore) combined.

Krom is a highly portable runtime for JavaScript based multimedia applications. It executes [JS](https://github.com/luboslenco/krom_jstest) or webassembly through Chakra and is fully supported in [Kha](https://github.com/Kode/Kha) as one of the backends, see [bindings](https://github.com/Kode/Kha/blob/master/Backends/Krom/Krom.hx). Krom is optimized for very fast development cycles and directly supports hot-patching of code, shaders and assets.

Note that Krom does not rely on web APIs. It rather exposes full, native hardware capabilities and in particular surpasses WebGL in features and speed.

## Build instructions

(build process is still being refined...)

## Running

`krom [assetsdir shadersdir [--flags]]`

If no arguments are provided, assets and shaders are loaded from the executable path.
