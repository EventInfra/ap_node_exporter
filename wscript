def options(opt):
	opt.load('compiler_c')
def configure(cnf):
	cnf.load('compiler_c')
	cnf.check_cfg(package='libmicrohttpd', args='--cflags --libs', uselib_store='libmicrohttpd')
	cnf.check_cfg(package='libnl-3.0', args='--cflags --libs', uselib_store='libnl-3')
	cnf.check_cfg(package='libnl-genl-3.0', args='--cflags --libs', uselib_store='libnl-genl-3')
	cnf.env.CFLAGS.append('-Wall')
	cnf.env.CFLAGS.append('-Wextra')

def build(bld):
	bld(features='c cprogram', source='node_exp.c', target='node_exp', use=['libmicrohttpd', 'libnl-3', 'libnl-genl-3'])
