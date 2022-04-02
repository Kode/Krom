let project = new Project('Krom');

await project.addProject('Kinc');

project.cppStd = 'c++11';
project.linkTimeOptimization = false;
project.macOSnoArm = true;
project.addFile('Sources/**');

await project.addProject('Chakra/Build');

project.setDebugDir('Deployment');

project.flatten();

resolve(project);
