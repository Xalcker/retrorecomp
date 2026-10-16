#!/usr/bin/perl
# Genera jugadas pseudoaleatorias reproducibles en formato de entradas grabadas
# (src/platform/inputs.h): un botón distinto cada `paso` cuadros.
# Uso: perl gen_inputs.pl desde hasta paso semilla BOTON[,BOTON...] > salida
use strict;
use warnings;
my ($from, $to, $step, $seed, $list) = @ARGV;
die "uso: gen_inputs.pl desde hasta paso semilla BOTONES\n" unless $list;
my @buttons = split /,/, $list;
my $x = $seed || 1;
for (my $f = $from; $f < $to; $f += $step) {
    $x = ($x * 1103515245 + 12345) % 2147483648;
    my $b = $buttons[($x >> 8) % @buttons];
    my $hold = $step > 10 ? 8 : int($step / 2) || 1;
    print "$f $b\n", $f + $hold, " -\n";
}
