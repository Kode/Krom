# Krom

## Build instructions

For release builds, edit korefile.js and set release to true.

### macOS

Run `node Kore/make --noshaders`

Open build/Krom.xcodeproj

Add Library Search Paths (`/path/to/Krom/V8/Libraries/macos/debug` or `/path/to/Krom/V8/Libraries/macos/release`)

Add Runtime Search Paths (`@loader_path/../Resources/macos`)
