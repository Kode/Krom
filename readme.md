# Krom

[Kore](https://github.com/Kode/Kore) + [Chakra](https://github.com/Microsoft/ChakraCore) combined.

Krom is a highly portable runtime for JavaScript based multimedia applications. It executes [JS](https://github.com/luboslenco/krom_jstest) or webassembly through Chakra and is fully supported in [Kha](https://github.com/Kode/Kha) as one of the backends, see [bindings](https://github.com/Kode/Kha/blob/master/Backends/Krom/Krom.hx). Krom is optimized for very fast development cycles and directly supports hot-patching of code, shaders and assets.

Note that Krom does not rely on web APIs. It rather exposes full, native hardware capabilities and in particular surpasses WebGL in features and speed.

## Build instructions

* For Windows: Run node Kore/make and compile in Visual Studio for x64
* For macOS: Run node Kore/make --noshaders and compile in Xcode
* For Linux: Run node Kore/make --compiler clang --compile

## Running

`krom [assetsdir shadersdir [--flags]]`

If no arguments are provided, assets and shaders are loaded from the executable path.

## Debugging

To debug Krom itself, just start it in Visual Studio or Xcode (Linux IDEs are not yet setup automatically for Krom debugging). The debug protocol can be debugged using an "attach" debug configuration in Kode Studio or Visual Studio Code. First start Krom in your C++ IDE with parameters ala `/path/to/project/build/krom --debug 9988` and then start a launch config which looks something like this:

`{
	"type": "krom",
	"request": "attach",
	"name": "Krom-Test",
	"preLaunchTask": "Kha: Build for Krom",
	"port": 9988
}`
