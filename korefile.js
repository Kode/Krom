let project = new Project('Krom');

project.addFile('Sources/**');
//project.addIncludeDir('V8/include');

if (platform === Platform.Windows) {
	project.addLib('Winmm');
	project.addLib('V8/Libraries/win32/debug/icui18n');
	project.addLib('V8/Libraries/win32/debug/icuuc');
	project.addLib('V8/Libraries/win32/debug/v8_base_0');
	project.addLib('V8/Libraries/win32/debug/v8_base_1');
	project.addLib('V8/Libraries/win32/debug/v8_base_2');
	project.addLib('V8/Libraries/win32/debug/v8_base_3');
	project.addLib('V8/Libraries/win32/debug/v8_external_snapshot');
	project.addLib('V8/Libraries/win32/debug/v8_libbase');
	project.addLib('V8/Libraries/win32/debug/v8_libplatform');
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

project.setDebugDir('Deployment');

Project.createProject('Kore').then((kore) => {
	project.addSubProject(kore);
	resolve(project);
});
