def options(opt):
	opt.load('compiler_c')
def configure(cnf):
	cnf.load('compiler_c')
	cnf.check_cfg(package='libnl-3.0', args='--cflags --libs', uselib_store='libnl-3')
	cnf.check_cfg(package='libnl-genl-3.0', args='--cflags --libs', uselib_store='libnl-genl-3')
	cnf.env.CFLAGS.append('-std=c11')
	cnf.env.CFLAGS.append('-pedantic')
	cnf.env.CFLAGS.append('-Wextra')
	cnf.env.CFLAGS.append('-Wall')
	cnf.env.CFLAGS.append('-Wzero-as-null-pointer-constant')
	cnf.env.CFLAGS.append('-Weverything')
	# Security-specific flags
	cnf.env.CFLAGS.append('-pie')
	cnf.env.CFLAGS.append('-fPIE')
	cnf.env.CFLAGS.append('-fPIC')
	cnf.env.CFLAGS.append('-Wformat')
	cnf.env.CFLAGS.append('-Wformat-security')
	cnf.env.CFLAGS.append('-Werror=format-security')
	cnf.env.CFLAGS.append('-D_FORTIFY_SOURCE=2')
	cnf.env.CFLAGS.append('-fstack-protector-strong')
	cnf.env.LDFLAGS.append('-pie')

	cnf.env.CFLAGS.append("-O3")

	# Debugging
	cnf.env.CFLAGS.append('-g')
	cnf.env.CFLAGS.append('-ggdb')

def build(bld):
	bld(features='c cprogram', source='node_exp.c', target='node_exp', use=['libnl-3', 'libnl-genl-3'])
