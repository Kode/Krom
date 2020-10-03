let project = new Project('Krom');

project.cpp = true;
project.cpp11 = true;
project.linkTimeOptimization = false;
project.addFile('Sources/**');

await project.addProject('Chakra/Build');

project.setDebugDir('Deployment');

resolve(project);
