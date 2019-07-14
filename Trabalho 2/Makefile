homebanking:
	$(MAKE) -s -C server
	$(MAKE) -s -C user

.PHONY: run

run: homebanking
	cp server/server $(HOME)/bin
	cp user/user $(HOME)/bin