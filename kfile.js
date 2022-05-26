const project = new Project('Krom');

project.followSymbolicLinks = false;

await project.addProject('Kinc');

project.flatten();

resolve(project);
