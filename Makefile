.PHONY: all
all:
	@bin/waf build

.PHONY: clean
clean:
	@bin/waf clean

.PHONY: configure
configure:
	@bin/waf configure
