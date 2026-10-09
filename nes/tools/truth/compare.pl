#!/usr/bin/perl
# Compara un mapa de código (gbrecomp --codemap o difftest --executed) contra
# el mapa verdadero del desensamblado (build_truth.sh).
#
# Uso: perl compare.pl truth.txt mapa.txt [simbolos.sym] [máximo de tramos]
#   Con el .sym original del desensamblado, los tramos se nombran con la
#   rutina que los contiene.
use strict;
use warnings;

my ($truth_path, $map_path, $sym_path, $max) = @ARGV;
die "uso: compare.pl truth.txt mapa.txt [sym] [máximo]\n" unless $map_path;
$max //= 12;

sub load {
    my ($path) = @_;
    open my $f, '<', $path or die "$path: $!";
    local $/;
    my $s = <$f>;
    close $f;
    $s =~ s/\s+//g;
    return $s;
}

my $truth = load($truth_path);
my $map = load($map_path);
die "tamaños distintos\n" if length $truth != length $map;
my $n = length $truth;

# Símbolos (sin los marcadores ni las etiquetas locales)
my @syms;
if ($sym_path) {
    open my $s, '<', $sym_path or die "$sym_path: $!";
    while (<$s>) {
        next unless /^([0-9a-fA-F]+):([0-9a-fA-F]+)\s+(\S+)/;
        my ($bank, $addr, $name) = (hex $1, hex $2, $3);
        next if $name =~ /__m\d/ || $name =~ /\./;
        my $lin = $bank ? $bank * 0x4000 + ($addr - 0x4000) : $addr;
        push @syms, [$lin, $name];
    }
    @syms = sort { $a->[0] <=> $b->[0] } @syms;
}

sub where {
    my ($lin) = @_;
    my $best = '';
    for my $s (@syms) {
        last if $s->[0] > $lin;
        $best = $s->[0] == $lin ? $s->[1] : sprintf("%s+\$%X", $s->[1], $lin - $s->[0]);
    }
    return $best;
}

sub addr_text {
    my ($lin) = @_;
    my $bank = int($lin / 0x4000);
    my $addr = $bank ? 0x4000 + $lin % 0x4000 : $lin;
    return sprintf("%02X:%04X", $bank, $addr);
}

my ($code, $found, $marked, $false) = (0, 0, 0, 0);
for my $i (0 .. $n - 1) {
    my $t = substr($truth, $i, 1);
    my $m = substr($map, $i, 1) eq 'C';
    $code++ if $t eq 'C';
    $marked++ if $m;
    $found++ if $m && $t eq 'C';
    $false++ if $m && $t ne 'C';
}

printf "Código según el desensamblado: %d bytes\n", $code;
printf "Marcado como código:           %d bytes\n", $marked;
printf "  Exhaustividad (código encontrado): %d de %d = %.1f%%\n", $found, $code, $code ? 100 * $found / $code : 0;
printf "  Precisión (lo marcado que sí es código): %d de %d = %.1f%%\n", $found, $marked, $marked ? 100 * $found / $marked : 0;
printf "  Datos decodificados como código: %d bytes\n", $false;

# Tramos contiguos de una condición
sub runs {
    my ($cond) = @_;
    my @r;
    my $start = -1;
    for my $i (0 .. $n) {
        my $c = $i < $n && $cond->($i);
        if ($c && $start < 0) { $start = $i; }
        elsif (!$c && $start >= 0) { push @r, [$start, $i - $start]; $start = -1; }
    }
    return sort { $b->[1] <=> $a->[1] } @r;
}

my @missed = runs(sub { substr($truth, $_[0], 1) eq 'C' && substr($map, $_[0], 1) ne 'C' });
printf "\nCódigo no encontrado: %d tramos\n", scalar @missed;
for my $r (@missed[0 .. ($#missed < $max - 1 ? $#missed : $max - 1)]) {
    printf "  %s  %5d bytes  %s\n", addr_text($r->[0]), $r->[1], where($r->[0]);
}

my @fp = runs(sub { substr($truth, $_[0], 1) ne 'C' && substr($map, $_[0], 1) eq 'C' });
printf "\nDatos tomados por código: %d tramos\n", scalar @fp;
for my $r (@fp[0 .. ($#fp < $max - 1 ? $#fp : $max - 1)]) {
    printf "  %s  %5d bytes  %s\n", addr_text($r->[0]), $r->[1], where($r->[0]);
}
