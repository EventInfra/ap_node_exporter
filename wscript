def options(opt):
	opt.load('compiler_c')
def configure(cnf):
	cnf.load('compiler_c')
	cnf.check_cfg(package='libnl-3.0', args='--cflags --libs', uselib_store='libnl-3')
	cnf.check_cfg(package='libnl-genl-3.0', args='--cflags --libs', uselib_store='libnl-genl-3')
	if not cnf.env.CFLAGS:
		cnf.env.CFLAGS = []
	cnf.env.CFLAGS.append('-std=c11')
	cnf.env.CFLAGS.append('-pedantic')
	cnf.env.CFLAGS.append('-Wextra')
	cnf.env.CFLAGS.append('-Wall')
	cnf.env.CFLAGS.append('-Wint-to-pointer-cast')
	cnf.env.CFLAGS.append('-Wno-missing-field-initializers')
	cnf.env.CFLAGS.append('-Wignored-qualifiers')
	cnf.env.CFLAGS.append('-Wcast-qual')
	cnf.env.CFLAGS.append('-Wcast-align')
	cnf.env.CFLAGS.append('-fstrict-aliasing')
	cnf.env.CFLAGS.append('-Wstrict-aliasing')
	cnf.env.CFLAGS.append('-Wshadow')

	# Security-specific flags
	cnf.env.CFLAGS.append('-fPIE')
	cnf.env.CFLAGS.append('-fPIC')
	cnf.env.CFLAGS.append('-Wformat=2')
	cnf.env.CFLAGS.append('-Wformat-security')
	cnf.env.CFLAGS.append('-Wformat-signedness')
	cnf.env.CFLAGS.append('-Werror=format-security')
	cnf.env.CFLAGS.append('-D_FORTIFY_SOURCE=2')
	#cnf.env.CFLAGS.append('-fstack-protector-strong')
	cnf.env.CFLAGS.append('-D_POSIX_C_SOURCE=200809')
	cnf.env.LDFLAGS.append('-pie')

	cnf.env.CFLAGS.append("-O3")

	# Debugging
	cnf.env.CFLAGS.append('-g')
	cnf.env.CFLAGS.append('-ggdb')

def build(bld):
	bld(features='c cprogram', source='node_exp.c', target='node_exp', use=['libnl-3', 'libnl-genl-3'])
