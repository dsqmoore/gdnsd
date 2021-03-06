
# Basic plugin tests

use _GDT ();
use FindBin ();
use File::Spec ();
use Test::More tests => 5;

my $soa = 'example.com 86400 SOA ns1.example.com hostmaster.example.com 1 7200 1800 259200 900';

my $pid = _GDT->test_spawn_daemon(File::Spec->catfile($FindBin::Bin, 'gdnsd.conf'));

_GDT->test_dns(
    qname => 'example.com', qtype => 'SOA',
    answer => $soa,
);

_GDT->test_dns(
    qname => 'meta1.example.com', qtype => 'A',
    answer => [
        'meta1.example.com 86400 A 192.0.2.111',
        'meta1.example.com 86400 A 192.0.2.112',
    ],
);

_GDT->test_dns(
    qname => 'meta2.example.com', qtype => 'A',
    answer => [
        'meta2.example.com 86400 A 192.0.2.111',
        'meta2.example.com 86400 A 192.0.2.112',
    ],
);

_GDT->test_kill_daemon($pid);
