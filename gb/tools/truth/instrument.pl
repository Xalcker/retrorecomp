#!/usr/bin/perl
# Instrumenta un desensamblado RGBDS para saber qué bytes de la ROM son código
# y cuáles datos: antes de cada línea que emite bytes inserta una etiqueta
# marcadora (local, para no romper el alcance de las etiquetas locales) y anota
# su tipo en un archivo aparte. Al ensamblar con --export-all, el .sym da la
# dirección de cada marcador.
#
# Uso: perl instrument.pl dir_origen dir_destino tipos.txt archivo.asm...
#   Las macros de código y de datos se configuran abajo.
use strict;
use warnings;

my ($src, $dst, $types_path, @files) = @ARGV;
die "uso: instrument.pl origen destino tipos.txt archivos...\n" unless @files;

# Mnemónicos del SM83
my %code = map { $_ => 1 } qw(
    ld ldh ldi ldd jp jr call ret reti rst push pop inc dec add adc sub sbc and
    or xor cp rlca rla rrca rra rlc rl rrc rr sla sra srl swap bit set res halt
    stop nop di ei daa cpl scf ccf
);
my %data = map { $_ => 1 } qw(db dw dl ds incbin);
# Macros del desensamblado de Tetris
my %code_macros = map { $_ => 1 } qw(wait_for_serial_interrupt);
my %data_macros = map { $_ => 1 } qw(notes rest endsection noise call);

open my $types, '>', $types_path or die "$types_path: $!";
my $n = 0;

for my $file (@files) {
    open my $in, '<', "$src/$file" or die "$src/$file: $!";
    open my $out, '>', "$dst/$file" or die "$dst/$file: $!";
    my $have_global = 0;    # ¿ya hay etiqueta global en la sección actual?
    my $in_macro = 0;       # dentro de una definición MACRO ... ENDM
    my $in_rept = 0;        # dentro de REPT ... ENDR (no se puede repetir una etiqueta)

    while (my $line = <$in>) {
        my $body = $line;
        $body =~ s/;.*$//;
        $body =~ s/\s+$//;

        if ($body =~ /^\s*([A-Za-z_][\w#]*):\s*MACRO\b/i || $body =~ /^\s*MACRO\b/i) { $in_macro = 1; }
        if ($in_macro) {
            print $out $line;
            $in_macro = 0 if $body =~ /^\s*ENDM\b/i;
            next;
        }
        if ($body =~ /^\s*SECTION\b/i) { $have_global = 0; print $out $line; next; }

        # Etiqueta al principio de la línea
        my $label = '';
        if ($body =~ /^([A-Za-z_][\w.]*::?)\s*(.*)$/ && $body !~ /^\S+\s+(EQU|=|SET)\b/i) {
            $label = $1;
            $body = $2;
            $have_global = 1 unless $label =~ /^\./ || $label =~ /\.\w/;
        } elsif ($body =~ /^(\.[\w]+:?:?)\s*(.*)$/) {
            $label = $1;
            $body = $2;
        }

        my ($tok) = $body =~ /^\s*([A-Za-z_][\w]*)/;
        my $kind = '';
        if (defined $tok) {
            my $t = lc $tok;
            if ($t eq 'rept' || $t eq 'for') { $kind = $in_rept ? '' : 'C'; }
            elsif ($code{$t} || $code_macros{$t}) { $kind = 'C'; }
            elsif ($data{$t} || $data_macros{$t}) { $kind = 'D'; }
        }
        $kind = '' if $in_rept;
        $in_rept++ if defined $tok && $tok =~ /^(rept|for)$/i;
        $in_rept-- if defined $tok && $tok =~ /^endr$/i;

        if ($kind eq '') { print $out $line; next; }

        # Línea que emite bytes: etiqueta original, marcador y contenido
        $n++;
        print $out "$label\n" if $label ne '';
        if ($have_global) {
            print $out ".__m$n\n";
        } else {
            print $out "__m${n}::\n";
            $have_global = 1;
        }
        print $types "$n $kind $file:$.\n";
        (my $rest = $line) =~ s/^\s*\Q$label\E//;
        print $out "    $rest";
        $rest =~ /\n$/ or print $out "\n";
    }
    close $in;
    close $out;
}
close $types;
print "marcadores: $n\n";
