const project = new Project('Krom');

await project.addProject('Kinc');

project.flatten();

resolve(project);
