#!/usr/bin/perl
# Une mapas de código (formato de gbrecomp --codemap): C si alguno dice C.
# Uso: perl merge_maps.pl mapa1 mapa2 ... > unión
use strict;
use warnings;
my @m;
for my $path (@ARGV) {
    open my $f, '<', $path or die "$path: $!";
    local $/;
    (my $s = <$f>) =~ s/\s+//g;
    close $f;
    my @c = split //, $s;
    for my $i (0 .. $#c) {
        $m[$i] = 'C' if $c[$i] eq 'C';
        $m[$i] //= '.';
    }
}
my $out = join '', @m;
$out =~ s/(.{64})/$1\n/g;
print $out;
