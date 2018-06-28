const os = require('os');

let project = new Project('Krom');

const release = false;
const build = release ? 'release' : 'debug';
let system = 'linux';
if (os.platform() === 'darwin') {
	system = 'macos';
}
else if (os.platform() === 'win32') {
	system = 'win32';
}
const libdir = 'V8/Libraries/' + system + '/' + build + '/';

project.cpp11 = true;
project.addFile('Sources/**');
project.addIncludeDir('V8/include');

if (platform === Platform.Windows) {
	if (!release) {
		project.addLib('Dbghelp');
		project.addLib('Shlwapi');
	}
	project.addLib('bcrypt');
	project.addLib('Crypt32');
	project.addLib('Winmm');
	project.addLib(libdir + 'v8.dll');
	project.addLib(libdir + 'v8_libbase.dll');
	project.addLib(libdir + 'v8_libplatform.dll');
}

if (platform === Platform.OSX) {
	project.addLib(libdir + 'libv8.dylib');
	project.addLib(libdir + 'libv8_libplatform.dylib');
	project.addLib(libdir + 'libv8_libbase.dylib');
}

if (platform === Platform.Linux) {
	project.addLib('../' + libdir + 'libv8.so');
	project.addLib('../' + libdir + 'libv8_libplatform.so');
	project.addLib('../' + libdir + 'libv8_libbase.so');
	project.addLib('../' + libdir + 'libc++.so');
	project.addLib('libssl');
	project.addLib('libcrypto');
}

project.setDebugDir('Deployment/' + build + '/' + system);

resolve(project);

