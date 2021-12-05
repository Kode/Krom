let project = new Project('Krom');

project.cpp11 = true;
project.linkTimeOptimization = false;
project.macOSnoArm = true;
project.addFile('Sources/**');

await project.addProject('Chakra/Build');

project.setDebugDir('Deployment');

resolve(project);
