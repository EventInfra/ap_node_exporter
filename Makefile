.PHONY: all
all:
	@bin/waf build

.PHONY: clean
clean:
	@bin/waf clean

.PHONY: configure
configure:
	@bin/waf configure


.PHONY: scanbuild
scanbuild:
	@scan-build bin/waf configure clean build

.PHONY: coverity
coverity:
	@if [ -d cov-int ]; then rm -rf cov-int;fi
	@mkdir cov-int
	@cov-build --dir=cov-int bin/waf configure clean build
	@tar cvzf coverity_apnodeexporter.tgz cov-int
	@rm -rf cov-int

.PHONY: cppcheck
cppcheck:
	@cppcheck --std=c11 *.[ch]

.PHONY: ctags
ctags:
	@ctags -R .
