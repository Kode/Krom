import json
import subprocess
from sys import platform

print(subprocess.check_output(['Kinc/Tools/windows_x64/kmake.exe', '--json']))

data = json.loads('{}')
with open('build/Krom.json', 'r') as f:
	data = json.load(f)

gypi = {
	'sources': [
	],
	'include_dirs': [
	],
	'defines': [
	]
}

gypi['sources'].append('src/krom/main.cpp')
for file in data['files']:
	gypi['sources'].append(file.replace('\\', '/'))

for include in data['includes']:
	gypi['include_dirs'].append(include.replace('\\', '/'))

gypi['defines'].append('KINC_NO_MAIN')
for define in data['defines']:
	gypi['defines'].append(define)

with open('krom.gypi', 'w') as f:
    json.dump(gypi, f, indent=4)

libs_gypi = {
	'libraries': [
	]
}

if platform == "win32":
	libs_gypi['libraries'].append('OleAut32')
for lib in data['libraries']:
	libs_gypi['libraries'].append(lib)

with open('krom_libs.gypi', 'w') as f:
	json.dump(libs_gypi, f, indent=4)
