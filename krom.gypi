{
	'sources': [
		'src/krom/main.cpp',
		'Kinc/Sources/kinc/rootunit.c',
		'Kinc/Sources/kinc/graphics4/g4unit.c',
		'Kinc/Sources/kinc/input/inputunit.c',
		'Kinc/Sources/kinc/io/iounit.c',
		'Kinc/Sources/kinc/math/mathunit.c',
		'Kinc/Sources/kinc/audio1/a1unit.c',
		'Kinc/Sources/kinc/audio2/audio.c',
		'Kinc/Backends/System/Microsoft/Sources/kinc/backend/microsoftunit.c',
		'Kinc/Backends/System/Windows/Sources/kinc/backend/windowsunit.c',
		'Kinc/Backends/System/Windows/Sources/kinc/backend/windowscppunit.cpp',
		'Kinc/Backends/Graphics4/Direct3D11/Sources/kinc/backend/compute.c',
		'Kinc/Backends/Graphics4/Direct3D11/Sources/kinc/backend/graphics4/d3d11unit.c',
		'Kinc/Backends/Audio2/WASAPI/Sources/kinc/backend/WASAPI.winrt.cpp',
		'Kinc/Sources/kinc/libs/stb_vorbis.c'
	],
	'include_dirs': [
		'Kinc/Sources',
		'Kinc/Backends/System/Microsoft/Sources',
		'Kinc/Backends/System/Windows/Sources',
		'Kinc/Backends/Graphics4/Direct3D11/Sources'
	],
	'defines': [
		'KORE_LZ4X',
		'KORE_WINDOWS',
		'KORE_MICROSOFT',
		'KINC_NO_DIRECTSHOW',
		'_CRT_SECURE_NO_WARNINGS',
		'_WINSOCK_DEPRECATED_NO_WARNINGS',
		'KORE_DIRECT3D',
		'KORE_DIRECT3D11',
		'KINC_NO_MAIN'
	]
}
