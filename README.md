
## Overview

gdnsd is an Authoritative-only DNS server. The initial g stands for Geographic, as gdnsd offers a plugin system for geographic (or other sorts of) balancing, redirection, and service-state-conscious failover. The plugin system can also do things like weighted address/cname records.  If you don't care about these features you can ignore them :).

gdnsd is written in C using libev and pthreads with a focus on high performance, low latency service. It does not offer any form of caching or recursive service, and does not support DNSSEC.  There's a strong focus on making the code efficient, lean, and resilient.  The code has a decent regression testsuite with full branch coverage on the core packet parsing and generation code, and some scripted QA tools for e.g. valgrind validation, clang-analyzer, etc.

The geographically-aware features also support the emerging EDNS Client Subnet draft ( https://datatracker.ietf.org/doc/draft-vandergaast-edns-client-subnet/ , http://afasterinternet.com ) for receiving more-precise network location information from intermediate shared caches.

## Resources

This project is hosted at Github: https://github.com/blblack/gdnsd 

Release downloads: http://downloads.gdnsd.net/

Bug reports: https://github.com/blblack/gdnsd/issues

Wikified docs: https://github.com/blblack/gdnsd/wiki

Google Group for discussion: https://groups.google.com/forum/#!forum/gdnsd

See the INSTALL file for details on prerequisites and build procedure
for working from the source tree or a source tarball.

The documentation is included in the source tree in POD format
and installed as manpages and textfiles on installation.

## Basic Configuration

If all you need to do is serve an authoritative domain or two, all you need is your standard BIND/RFC1035-style zonefiles, with the files named same as their respective zone names and placed in gdnsd's zones directory.

If you need to do trickier things, a world of non-default options are available: check out the Documentation.

## The Plugins

A core feature (and the reason for starting this project, although it's more of a niche thing) is that gdnsd has an API for dso-based plugins for pluggable address resolution (conceptually similar to Apache modules) for A, AAAA, and CNAME records.

The core daemon also includes basic HTTP and raw TCP monitoring services with anti-flap that plugins can use to make failover decisions. Sufficient hooks exist to implement your own custom monitoring solution as well. The state of services monitored by this code is also reflected in gdnsd's own HTTP output.

Included with the gdnsd core are 3 trivial/testing plugins called null, static, reflect. Reflect is useful for real-world debugging (it sends the cache's IP or the edns-client-subnet IP back to the requester as answer data).

Other useful plugins included are simplefo, multifo, and weighted, which do various forms of address failover and weighting.

Finally, there are also two included meta-plugins named metafo and geoip, which do higher-level failover (and geographic mapping) of resources defined in terms of other plugins.

## Interop Testing

My test servers/domains are at: http://gdnsd.net . These tend to run the latest release (stable or dev) of gdnsd at any given time. If you have any questions about conformance, interoperability, etc, feel free to query these servers and see how they react.

## Portability

The primary target platform is modern x86_64 Linux.  The code also compiles and works fine on MacOS X.  In theory, it should be portable to any reasonably-modern POSIXy platform with a good C99 compiler.  Several releases ago I spent some effort testing that it did in fact compile, pass tests, and operate correctly at runtime on a number of *BSDs, OpenSolaris w/ Sun's compiler, and even an embedded Linux router with a big-endian MIPS CPU.  YMMV on such targets with the modern codebase as I only regularly test Linux and Mac targets, but clean portability patches are always welcome.  There's a FreeBSD port at: http://portsmon.freebsd.org/portoverview.py?category=dns&portname=gdnsd

## Building From Source

In general, this is a standard autoconf-style project: ./configure && make check && sudo make install

If your starting point is a tarball download, the following prerequisites apply:

* A basically functional POSIX build environment with a C99 compiler
* libev headers and libraries, version 4.x: distro pkg or http://software.schmorp.de/pkg/libev.html

The following are optional, but generally recommended:

* liburcu aka userspace-rcu headers and libraries. Use distro pkg or http://lttng.org/urcu/
* libcap headers and libraries on Linux hosts (use distro pkg generally)

The following have no real effect on the build or runtime, but are required in order to run the testsuite:

* Perl 5.8.1 or higher
* Perl modules: Net::DNS 0.63+, LWP 5.805+, Socket6, IO::Socket::INET6, HTTP::Daemon

If working directly from a git clone rather than a tarball, in addition to all of the above:
* ./autogen.sh will construct the configure script to get started
* You may need to install updated autoconf, automake, and libtool packages
* You will need a working copy of Ragel: http://www.complang.org/ragel/ (or distro package)

## Versioning and Public Branches

Starting with version 1.8.0, gdnsd will switch to SemVer as currently documented here: http://semver.org/ .  Any bugfix-only releases after 1.8.0 would be 1.8.x (as before), a release of backwards-compatible feature updates would bump us to 1.9.0, and actual compatibility breakage will require a bump to 2.0.0.

The old policy contributed to very long wait times between feature releases, and almost always lead to backwards-compat breakage for any feature-release update as well.  Hopefully with the new versioning policy we'll see feature releases happen in smaller bites more often, and with better backwards compatibility.  At this time I'm only considering user-level features for SemVer, not the Plugin API, which has always been a bit unstable and semi-private.  The Plugin API will at least be stable for bugfix-only releases, but we may yet break it on minor feature releases for a while longer before it becomes more-stable.  It has its own internal binary versioning to prevent the loading of 3rd-party plugins which need updating.

The "master" branch on github will always represent the numerically-latest public release, as well as bugfix traffic on the latest release.  If there ends up being a reason to, for example, release a 1.8.N bugfix after there's already been a 1.9.x release, a separate public 1.8 branch would be forked from the appropriate point on master's timeline.  Simple bugfix commits can merge directly to master (and wherever else seems appropriate).  Complex bugfix branches could also merge directly to master when complete.  Bugfixes intended for release under multiple concurrent feature-lines (e.g. a bugfix applicable to and destined for 1.10.1, 1.9.4, and 1.8.8) should probably be merged to the newest applicable release branch (master if current), and then older versions can be dealt with via cherry-picking as warranted.

The existing "dev" branch will become the integration point for private/public feature branches destined for the next planned feature-release, and features merged there should be mostly complete other than needing a bit of QA work.  Master commits will be regularly pulled back to dev, but dev will only be pushed back to master when it's time for a feature release.  All actual version-bump work (NEWS file updates, configure.ac version number, etc) will occur in the master branch (or the appropriate past-release-series offshoot), not in dev.  There won't be any official tarball releases directly from dev.

## Project History

This project was originally conceived circa early 2007.  I had just begun a new job at Logitech (still my current employer at this time).  We needed some geographic DNS redirection features for the project I was working on and when I surveyed the landscape at the time I didn't find any acceptable options.  The available open source options (e.g. a trivial BIND patch) were insufficient on features, and the commercial options (e.g. F5's hardware solution) were prohibitively expensive for the scale of the project in question.

The first internal drafts of this daemon were written in pure Perl using the POE framework and Net::DNS for packet-mangling.  It didn't even support full authoritative service, you simply delegated a hostname off to this server (e.g. "www") and it could do geographic redirection of A-records for that one hostname.  It worked reasonably well and was even deployed to production successfully in this form.

However, two key issues were apparent.  First, the performance and reliability wasn't great, and DNS really needs to be performant and reliable.  Packet captures were showing a small percentage of totally dropped requests, and another small percentage of very slow (e.g. ~200ms+) responses before even accounting for actual network delays.  I did some extensive debugging on these issues, but ultimately I was unable to resolve them completely and had to chalk it up to "Hey this software is running on top of a huge complex stack of ever-changing and often slightly-buggy CPAN modules in a dynamic scripting language."

The other issue was that operating via delegation didn't allow for geographic redirection of the domain's root name, and of course I couldn't move the whole domain to the special server because it didn't support all of the other basic features of a normal authoritative DNS server.

To resolve these issues, I re-wrote the core code in C and added all the basic features for a full authoritative server.  I left the config-file parsing in Perl, which generated a binary blob the C code consumed for runtime (containing binary structs for all of the config and zonefile data).  Once that was working reasonably well and deployed to production at Logitech, I talked it over with my boss at the time (hi Dean!) and open-sourced the code.  This lead to the first public release of the code (0.02) in mid-2008.

Late in 2009, I started working on a major refactor.  The primary goals here were to move the configuration parsing into the C code like a normal daemon, add a ton of new testsuite coverage (100% branch coverage of the core packet parse/generate code was the goal, which was achieved), and to move the geographic redirection code out to a DSO-based plugin so that other techniques could be experimented on without touching the relatively stable core DNS code.  This effort finally lead to a new stable release series (1.0.x) in April of 2010.

The rest is all relatively well-documented in the NEWS file.

## COPYRIGHT AND LICENSING

gdnsd is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

gdnsd is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with gdnsd.  If not, see <http://www.gnu.org/licenses/>.

