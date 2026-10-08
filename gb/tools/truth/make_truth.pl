#!/usr/bin/perl
# Convierte los marcadores del desensamblado instrumentado en un mapa por byte.
# Uso: perl make_truth.pl markers.txt archivo.sym archivo.map TAMAÑO > truth.txt
# Salida: un carácter por byte de la ROM, 64 por línea:
#   C = código, D = datos, - = fuera de toda sección (relleno)
use strict;
use warnings;

my ($markers_path, $sym_path, $map_path, $size) = @ARGV;
die "uso: make_truth.pl markers.txt sym map tamaño\n" unless $size;

# Tipo de cada marcador
my %kind;
open my $m, '<', $markers_path or die "$markers_path: $!";
while (<$m>) {
    my ($n, $k) = split;
    $kind{$n} = $k;
}
close $m;

sub linear {
    my ($bank, $addr) = @_;
    return $bank ? $bank * 0x4000 + ($addr - 0x4000) : $addr;
}

# Dirección de cada marcador
my @marks;  # [dirección lineal, tipo]
open my $s, '<', $sym_path or die "$sym_path: $!";
while (<$s>) {
    next unless /^([0-9a-fA-F]+):([0-9a-fA-F]+)\s+\S*__m(\d+)\s*$/;
    my $addr = linear(hex $1, hex $2);
    next if $addr >= $size;
    push @marks, [$addr, $kind{$3} // '?'];
}
close $s;

# Secciones de ROM
my @sections;  # [inicio, fin exclusivo]
open my $mp, '<', $map_path or die "$map_path: $!";
my $bank = 0;
while (<$mp>) {
    if (/^ROM[0X] bank #(\d+)/) { $bank = $1; next; }
    last if /^(WRAM|HRAM|SRAM|VRAM|OAM)/;
    if (/SECTION: \$([0-9a-fA-F]+)-\$([0-9a-fA-F]+)/) {
        push @sections, [linear($bank, hex $1), linear($bank, hex $2) + 1];
    }
}
close $mp;

my @map = ('-') x $size;
@marks = sort { $a->[0] <=> $b->[0] } @marks;
for my $sec (@sections) {
    my ($start, $end) = @$sec;
    my @in = grep { $_->[0] >= $start && $_->[0] < $end } @marks;
    # Bytes antes del primer marcador (no debería haber): datos
    for my $i (0 .. $#in) {
        my $from = $in[$i][0];
        my $to = $i < $#in ? $in[$i + 1][0] : $end;
        $map[$_] = $in[$i][1] for $from .. $to - 1;
    }
}

my $line = '';
for my $i (0 .. $size - 1) {
    $line .= $map[$i];
    if (length $line == 64) { print "$line\n"; $line = ''; }
}
print "$line\n" if length $line;

my %count;
$count{$_}++ for @map;
printf STDERR "código %d bytes, datos %d, relleno %d\n", $count{C} // 0, $count{D} // 0, $count{'-'} // 0;
