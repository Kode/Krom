let project = new Project('Krom', __dirname);

project.addFile('Sources/**');
project.addIncludeDir('V8/include');

if (platform === Platform.Windows) {
	project.addLib('Dbghelp');
	project.addLib('Shlwapi');
	project.addLib('Winmm');
	project.addLib('V8/Libraries/win32/debug/v8.dll');
	project.addLib('V8/Libraries/win32/debug/v8_libbase');
	project.addLib('V8/Libraries/win32/debug/v8_libplatform');

	project.setDebugDir('Deployment/debug/win32');
}

if (platform === Platform.OSX) {
	project.addLib('V8/Libraries/osx/debug/libicudata.a');
	project.addLib('V8/Libraries/osx/debug/libicui18n.a');
	project.addLib('V8/Libraries/osx/debug/libicuuc.a');
	project.addLib('V8/Libraries/osx/debug/libv8_base.a');
	project.addLib('V8/Libraries/osx/debug/libv8_external_snapshot.a');
	project.addLib('V8/Libraries/osx/debug/libv8_libbase.a');
	project.addLib('V8/Libraries/osx/debug/libv8_libplatform.a');
}

Project.createProject('Kore', __dirname).then((kore) => {
	project.addSubProject(kore);
	resolve(project);
});
