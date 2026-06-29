# nbdkit
# Copyright Red Hat
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution.
#
# * Neither the name of Red Hat nor the names of its contributors may be
# used to endorse or promote products derived from this software without
# specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY RED HAT AND CONTRIBUTORS ''AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
# PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL RED HAT OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
# USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
# OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.

include $(top_srcdir)/common-rules.mk

EXTRA_DIST = \
	magic-parameter.txt \
	make-links.sh \
	nbdkit.pod \
	nbdkit-captive.pod \
	nbdkit-client.pod \
	nbdkit_debug.pod \
	nbdkit_debug_hexdump.pod \
	nbdkit_error.pod \
	nbdkit_export_name.pod \
	nbdkit-filter.pod \
	nbdkit_is_tls.pod \
	nbdkit-loop.pod \
	nbdkit_name.pod \
	nbdkit_nanosleep.pod \
	nbdkit_parse_bool.pod \
	nbdkit_parse_delay.pod \
	nbdkit_parse_int.pod \
	nbdkit_parse_probability.pod \
	nbdkit_parse_size.pod \
	nbdkit_peer_name.pod \
	nbdkit_peer_tls_dn.pod \
	nbdkit-plugin.pod \
	nbdkit-probing.pod \
	nbdkit-protocol.pod \
	nbdkit_read_password.pod \
	nbdkit_realpath.pod \
	nbdkit-release-notes-1.4.pod \
	nbdkit-release-notes-1.6.pod \
	nbdkit-release-notes-1.8.pod \
	nbdkit-release-notes-1.10.pod \
	nbdkit-release-notes-1.12.pod \
	nbdkit-release-notes-1.14.pod \
	nbdkit-release-notes-1.16.pod \
	nbdkit-release-notes-1.18.pod \
	nbdkit-release-notes-1.20.pod \
	nbdkit-release-notes-1.22.pod \
	nbdkit-release-notes-1.24.pod \
	nbdkit-release-notes-1.26.pod \
	nbdkit-release-notes-1.28.pod \
	nbdkit-release-notes-1.30.pod \
	nbdkit-release-notes-1.32.pod \
	nbdkit-release-notes-1.34.pod \
	nbdkit-release-notes-1.36.pod \
	nbdkit-release-notes-1.38.pod \
	nbdkit-release-notes-1.40.pod \
	nbdkit-release-notes-1.42.pod \
	nbdkit-release-notes-1.44.pod \
	nbdkit-release-notes-1.46.pod \
	nbdkit-release-notes-1.48.pod \
	nbdkit-security.pod \
	nbdkit-service.pod \
	nbdkit_shutdown.pod \
	nbdkit_stdio_safe.pod \
	nbdkit_strdup_intern.pod \
	nbdkit_timestamp.pod \
	nbdkit-tls.pod \
	nbdkit-tracing.pod \
	synopsis.txt \
	$(NULL)

# These man pages are links to other man pages, they are not generated
# from pod sources.  Therefore we have to distribute them in the
# tarball.
non_generated_mans = \
	nbdkit_absolute_path.3 \
	nbdkit_debug_hexdiff.3 \
	nbdkit_disconnect.3 \
	nbdkit_parse_int8_t.3 \
	nbdkit_parse_int16_t.3 \
	nbdkit_parse_int32_t.3 \
	nbdkit_parse_int64_t.3 \
	nbdkit_parse_uint8_t.3 \
	nbdkit_parse_uint16_t.3 \
	nbdkit_parse_uint32_t.3 \
	nbdkit_parse_uint64_t.3 \
	nbdkit_parse_unsigned.3 \
	nbdkit_peer_gid.3 \
	nbdkit_peer_pid.3 \
	nbdkit_peer_uid.3 \
	nbdkit_peer_security_context.3 \
	nbdkit_peer_tls_issuer_dn.3 \
	nbdkit_printf_intern.3 \
	nbdkit_set_error.3 \
	nbdkit_strndup_intern.3 \
	nbdkit_vdebug.3 \
	nbdkit_verror.3 \
	nbdkit_vprintf_intern.3 \
	$(NULL)
EXTRA_DIST += $(non_generated_mans)

if HAVE_POD

# These man pages are generated from pod sources.  Use a separate
# variable here so we can add only these to CLEANFILES below.
generated_mans = \
	nbdkit.1 \
	nbdkit-captive.1 \
	nbdkit-client.1 \
	nbdkit_debug.3 \
	nbdkit_debug_hexdump.3 \
	nbdkit_error.3 \
	nbdkit_export_name.3 \
	nbdkit-filter.3 \
	nbdkit_is_tls.3 \
	nbdkit-loop.1 \
	nbdkit_name.3 \
	nbdkit_nanosleep.3 \
	nbdkit_parse_bool.3 \
	nbdkit_parse_delay.3 \
	nbdkit_parse_int.3 \
	nbdkit_parse_probability.3 \
	nbdkit_parse_size.3 \
	nbdkit_peer_name.3 \
	nbdkit_peer_tls_dn.3 \
	nbdkit-plugin.3 \
	nbdkit-probing.1 \
	nbdkit-protocol.1 \
	nbdkit_read_password.3 \
	nbdkit_realpath.3 \
	nbdkit-release-notes-1.4.1 \
	nbdkit-release-notes-1.6.1 \
	nbdkit-release-notes-1.8.1 \
	nbdkit-release-notes-1.10.1 \
	nbdkit-release-notes-1.12.1 \
	nbdkit-release-notes-1.14.1 \
	nbdkit-release-notes-1.16.1 \
	nbdkit-release-notes-1.18.1 \
	nbdkit-release-notes-1.20.1 \
	nbdkit-release-notes-1.22.1 \
	nbdkit-release-notes-1.24.1 \
	nbdkit-release-notes-1.26.1 \
	nbdkit-release-notes-1.28.1 \
	nbdkit-release-notes-1.30.1 \
	nbdkit-release-notes-1.32.1 \
	nbdkit-release-notes-1.34.1 \
	nbdkit-release-notes-1.36.1 \
	nbdkit-release-notes-1.38.1 \
	nbdkit-release-notes-1.40.1 \
	nbdkit-release-notes-1.42.1 \
	nbdkit-release-notes-1.44.1 \
	nbdkit-release-notes-1.46.1 \
	nbdkit-release-notes-1.48.1 \
	nbdkit-security.1 \
	nbdkit-service.1 \
	nbdkit_shutdown.3 \
	nbdkit_stdio_safe.3 \
	nbdkit_strdup_intern.3 \
	nbdkit_timestamp.3 \
	nbdkit-tls.1 \
	nbdkit-tracing.3 \
	$(NULL)
CLEANFILES += $(generated_mans)

man_MANS = \
	$(generated_mans) \
	$(non_generated_mans) \
	$(NULL)

nbdkit.1: nbdkit.pod plugin-links.pod lang-plugin-links.pod filter-links.pod \
		synopsis.txt $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    --insert plugin-links.pod:__PLUGIN_LINKS__ \
	    --insert lang-plugin-links.pod:__LANG_PLUGIN_LINKS__ \
	    --insert filter-links.pod:__FILTER_LINKS__ \
	    --verbatim $(srcdir)/synopsis.txt:__SYNOPSIS__ \
	    $<

nbdkit-captive.1: nbdkit-captive.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-client.1: nbdkit-client.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit_debug.3: nbdkit_debug.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit_debug_hexdump.3: nbdkit_debug_hexdump.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit_error.3: nbdkit_error.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit_export_name.3: nbdkit_export_name.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-filter.3: nbdkit-filter.pod filter-links.pod \
		$(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    --insert filter-links.pod:__FILTER_LINKS__ \
	    $<

nbdkit_is_tls.3: nbdkit_is_tls.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-loop.1: nbdkit-loop.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit_name.3: nbdkit_name.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit_nanosleep.3: nbdkit_nanosleep.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit_parse_bool.3: nbdkit_parse_bool.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit_parse_delay.3: nbdkit_parse_delay.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit_parse_int.3: nbdkit_parse_int.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit_parse_probability.3: nbdkit_parse_probability.pod \
	    $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit_parse_size.3: nbdkit_parse_size.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit_peer_name.3: nbdkit_peer_name.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit_peer_tls_dn.3: nbdkit_peer_tls_dn.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-plugin.3: nbdkit-plugin.pod plugin-links.pod lang-plugin-links.pod \
		$(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    --insert plugin-links.pod:__PLUGIN_LINKS__ \
	    --insert lang-plugin-links.pod:__LANG_PLUGIN_LINKS__ \
	    $<

nbdkit-probing.1: nbdkit-probing.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-protocol.1: nbdkit-protocol.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit_read_password.3: nbdkit_read_password.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit_realpath.3: nbdkit_realpath.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-release-notes-%.1: nbdkit-release-notes-%.pod \
		$(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-security.1: nbdkit-security.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-service.1: nbdkit-service.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit_shutdown.3: nbdkit_shutdown.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit_stdio_safe.3: nbdkit_stdio_safe.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit_strdup_intern.3: nbdkit_strdup_intern.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit_timestamp.3: nbdkit_timestamp.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-tls.1: nbdkit-tls.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=1 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

nbdkit-tracing.3: nbdkit-tracing.pod $(top_builddir)/podwrapper.pl
	$(PODWRAPPER) --section=3 --man $@ \
	    --html $(top_builddir)/html/$@.html \
	    $<

endif HAVE_POD

# All plugins.
plugin-links.pod: $(top_srcdir)/configure.ac
	rm -f $@ $@-t
	$(srcdir)/make-links.sh plugin 1 $(non_lang_plugins) > $@-t
	echo \; >> $@-t
	$(srcdir)/make-links.sh plugin 3 $(lang_plugins) >> $@-t
	mv $@-t $@

# Only language plugins (in section 3).
lang-plugin-links.pod: $(top_srcdir)/configure.ac
	rm -f $@ $@-t
	$(srcdir)/make-links.sh plugin 3 $(lang_plugins) >> $@-t
	mv $@-t $@

# All filters.
filter-links.pod: $(top_srcdir)/configure.ac
	rm -f $@ $@-t
	$(srcdir)/make-links.sh filter 1 $(filters) > $@-t
	mv $@-t $@

DISTCLEANFILES = plugin-links.pod lang-plugin-links.pod filter-links.pod
