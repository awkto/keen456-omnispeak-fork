# Oracle targets (run as: make -C oracle <target>). Game data and the Borland toolchain are never in the repo; targets SKIP without them.
.PHONY: verify verify-k4 verify-k5 verify-k6
verify: verify-k4 verify-k5 verify-k6
verify-k4 verify-k5 verify-k6:
	@tools/verify-dos.sh $(patsubst verify-k%,%,$@)

# KEENDUMP trace-writing build (oracle/docs/TRACE_FORMAT.md) in its own staging dir; needs verify-k<N> first.
.PHONY: keendump-k4 keendump-k5 keendump-k6
keendump-k4 keendump-k5 keendump-k6:
	@tools/keendump.sh build $(patsubst keendump-k%,%,$@)

# Symbol tables from the TLINK /s maps of the byte-identical rebuilds (see oracle/docs/SYMBOLS.md).
.PHONY: symbols
KEEN_BUILD ?= $(HOME)/keen-build
symbols:
	@for e in 4 5; do \
	  m=$(KEEN_BUILD)/k$$e/KEEN4-6/KEEN$$e/OBJ/RCK$$e.MAP; \
	  [ -f $$m ] || tools/verify-dos.sh $$e >/dev/null; \
	  if [ -f $$m ]; then python3 tools/map2syms.py sym $$m docs/symbols/k$$e.sym; else echo "SKIP: no map for Keen $$e (needs ~/bcc + game data)"; fi; \
	done

# Full oracle pass (rebuild, golden traces, fork vs DOS): meant for a droplet, see oracle/harness/oracle-run.sh
.PHONY: oracle
oracle:
	@harness/oracle-run.sh
