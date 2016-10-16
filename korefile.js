const os = require('os');

let project = new Project('Krom', __dirname);

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
	project.addLib('Winmm');
	project.addLib(libdir + 'v8.dll');
	project.addLib(libdir + 'v8_libbase.dll');
	project.addLib(libdir + 'v8_libplatform');
}

if (platform === Platform.OSX) {
	project.addLib(libdir + 'libv8.dylib');
	project.addLib(libdir + 'libv8_libplatform.a');
	project.addLib(libdir + 'libv8_libbase.dylib');
	project.addLib(libdir + 'libicui18n.dylib');
	project.addLib(libdir + 'libicuuc.dylib');
}

if (platform === Platform.Linux) {
	project.addLib('../' + libdir + 'libv8.so');
	project.addLib('../' + libdir + 'libv8_libplatform.a');
	project.addLib('../' + libdir + 'libv8_libbase.so');
	project.addLib('../' + libdir + 'libicui18n.so');
	project.addLib('../' + libdir + 'libicuuc.so');
}

project.setDebugDir('Deployment/' + build + '/' + system);

Project.createProject('Kore', __dirname).then((kore) => {
	project.addSubProject(kore);
	resolve(project);
});

