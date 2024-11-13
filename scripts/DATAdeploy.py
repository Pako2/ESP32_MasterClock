Import("env")
import shutil
import os
#
# Dump build environment (for debug)
# print env.Dump()
#

#
# Upload actions
#

def after_build(source, target, env):
	my_flags = env.ParseFlags(env['BUILD_FLAGS'])
	defines = my_flags.get("CPPDEFINES")
	ver = ""
	for item in defines:
		if type(item)==list:
			if (item[0]=='VERSION'):
				ver = item[1]
				break
	shutil.copy(firmware_source, 'bin/data/firmware_data_%s.bin' % ver.replace('.',''))

env.AddPostAction("buildprog", after_build)

firmware_source = os.path.join(env.subst("$BUILD_DIR"), "firmware.bin")
