var solution = new Solution('Krom');
var project = new Project('Krom');

project.addFile('Sources/**');
//project.addIncludeDir('V8/include');

project.addLib('V8/Libraries/osx/debug/libicudata.a');
project.addLib('V8/Libraries/osx/debug/libicui18n.a');
project.addLib('V8/Libraries/osx/debug/libicuuc.a');
project.addLib('V8/Libraries/osx/debug/libv8_base.a');
project.addLib('V8/Libraries/osx/debug/libv8_external_snapshot.a');
project.addLib('V8/Libraries/osx/debug/libv8_libbase.a');
project.addLib('V8/Libraries/osx/debug/libv8_libplatform.a');

project.setDebugDir('Deployment');

project.addSubProject(Solution.createProject('Kore'));

solution.addProject(project)

return solution;
