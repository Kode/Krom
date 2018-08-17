const os = require('os');

let project = new Project('Krom');

project.cpp11 = true;
project.addFile('Sources/**');

await project.addProject('Chakra/Build');

project.setDebugDir('Deployment');

resolve(project);
