
EXTRA_DIST = _GDT.pm _FakeGeoIP.pm AAAA66.pm $(srcdir)/[0-9]*/* $(srcdir)/[0-9]*/*/*
TESTOUT_DIR = $(abs_builddir)/testout
TEXEC = TESTOUT_DIR=$(TESTOUT_DIR) TESTPORT_START=$(TESTPORT_START) $(PERL) -I$(srcdir) -MTest::Harness -e "runtests(@ARGV)"
ALLTESTS = $(srcdir)/[0-9]*/*.t

precheck:
	@if test "x$(HAVE_TESTSUITE_MODULES)" != x1; then \
		echo "Cannot 'make check' or 'make installcheck' without Net::DNS 0.63+ and LWP 5.805+"; \
		exit 101; \
	fi
	@if [ ! -d $(TESTOUT_DIR) ]; then \
		$(MKDIR_P) $(TESTOUT_DIR); \
	fi
	@echo Test data/outputs will be stored at $(TESTOUT_DIR)

# To run just one test subdir: make check TRUN=007plugins/*.t
check-local: precheck
	${AM_V_GEN}if test "${TRUN}x" != "x"; then \
		TOP_BUILDDIR=$(abs_top_builddir) $(TEXEC) $(srcdir)/${TRUN}; \
	else \
		TOP_BUILDDIR=$(abs_top_builddir) $(TEXEC) $(ALLTESTS); \
	fi

installcheck-local: precheck
	${AM_V_GEN}if test "${TRUN}x" != "x"; then \
		INSTALLCHECK_SBINDIR=$(sbindir) $(TEXEC) $(srcdir)/${TRUN}; \
	else \
		INSTALLCHECK_SBINDIR=$(sbindir) $(TEXEC) $(ALLTESTS); \
	fi

clean-local:
	rm -rf $(TESTOUT_DIR)

include $(top_srcdir)/docs.am
