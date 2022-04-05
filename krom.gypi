{
	'sources': [
		'Kinc/Sources/kinc/rootunit.c',
		'Kinc/Sources/kinc/graphics4/g4unit.c',
		'Kinc/Sources/kinc/input/inputunit.c',
		'Kinc/Sources/kinc/io/iounit.c',
		'Kinc/Sources/kinc/math/mathunit.c',
		'Kinc/Backends/System/Microsoft/Sources/kinc/backend/microsoftunit.c',
		'Kinc/Backends/System/Windows/Sources/kinc/backend/windowsunit.c',
		'Kinc/Backends/System/Windows/Sources/kinc/backend/windowscppunit.cpp',
		'Kinc/Backends/Graphics4/Direct3D11/Sources/kinc/backend/compute.c',
		'Kinc/Backends/Graphics4/Direct3D11/Sources/kinc/backend/graphics4/d3d11unit.c',
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
	],
	'libraries': [
		'dxguid.lib',
		'dsound.lib',
		'dinput8.lib',
		'ws2_32.lib',
		'Winhttp.lib',
		'wbemuuid.lib',
		'd3d11.lib',
		'Ole32.lib',
		'OleAut32.lib'
	]
}
