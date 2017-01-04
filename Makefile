.PHONY: all
all:
	@bin/waf configure

.PHONY: clean
clean:
	@bin/waf clean

.PHONY: configure
configure:
	@bin/waf configure
