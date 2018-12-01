.DEFAULT_GOAL := run

TOP := ../..
OUTDIR := results

SRCS := $(wildcard *.cc *.h)

run: clean-results $(SRCS) | $(OUTDIR)
	./run-test.bash $(TOP) $(OUTDIR)
	tree $(OUTDIR)

$(OUTDIR): ; mkdir -p $@

clean-results:
	( cd $(TOP) && $(RM) *txt *pcap *sca )
	rm -rf $(OUTDIR)
