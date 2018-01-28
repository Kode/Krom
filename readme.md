# Krom

[Kore](https://github.com/Kode/Kore) + [V8](https://developers.google.com/v8/) combined.

Krom allows you to build portable applications or embed into existing ones. It executes [JS](https://github.com/luboslenco/krom_jstest) or webassembly through V8 and is fully supported in [Kha](https://github.com/Kode/Kha) as one of the backends. See [bindings](https://github.com/Kode/Kha/blob/master/Backends/Krom/Krom.hx). In Krom, native C++ part is precompiled and the JS part is developed with 'zero' compile times, live code patching and deployed to all OSs at once.

Note that Krom does not rely on web technologies. It rather exposes full, native hardware capabilities. Think of it as an extension to the Kore library with scripting support.

## Build instructions

For release builds, edit korefile.js and set release to true. Run `node Kore/make --help` for additional flags, like switching the graphics API.

### Windows

Run `node Kore/make --noshaders` and compile resulting Visual Studio project located in build/ folder.

### Linux

Run `node Kore/make --noshaders`, open the Code::Blocks project and change the rpath option to `-Wl,-rpath=XORIGIN/`.
After compiling cd to build/bin/Release and run `chrpath -r "\$ORIGIN/" Krom`.

### macOS

Run `node Kore/make --noshaders`

Open build/Krom.xcodeproj

Add Library Search Paths (`/path/to/Krom/V8/Libraries/macos/debug` or `/path/to/Krom/V8/Libraries/macos/release`)

Add Runtime Search Paths (`@loader_path/../Resources/macos`)

## Build instructions for V8

Set some env vars for Windows:
set PATH=path\to\depot_tools;%PATH%
set DEPOT_TOOLS_WIN_TOOLCHAIN=0
set GYP_MSVS_VERSION=2015

debug:
gn args out.gn/debug

is_debug = true
is_component_build = true
v8_enable_i18n_support = false

ninja -C out.gn/debug

release:
gn args out.gn/release

is_debug = false
is_component_build = true
v8_enable_i18n_support = false

ninja -C out.gn/release

## Running

`krom [assetsdir shadersdir [--flags]]`

If no arguments are provided, assets and shaders are loaded from executable path.
