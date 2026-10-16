/*
 * Interfaz entre el runtime y el código que genera smsrecomp (en el proyecto generado, con -DSMS_RECOMP).
 *
 * El Z80 en código nativo con el recompilador común (z80_recomp.h, en tools/retrorecomp/shared): cada vista
 * (una página de la ROM en una ranura: gen/zpPPsS*.c; y el código aprendido de la RAM, gen/zramN*.c) tiene sus
 * funciones de tramos contiguos, que llaman a la rama de z80_exec de cada instrucción con sus bytes y su PC
 * constantes: la misma semántica, los mismos accesos al bus y los mismos ciclos que el intérprete. Antes y después
 * de cada instrucción hacen lo mismo que sms_step (z80_recomp_host.h); se sale al terminar el cuadro, con una
 * interrupción o un HALT que atiende el intérprete, cuando el destino no es de la función y, tras una instrucción
 * que cambió el mapeo, para que el despachador elija la vista de nuevo.
 *
 * recomp_lookup (gen/views.c) elige con el PC y el mapeo de ahora: la vista de la página que está en esa ranura
 * (en el mapper Sega, la de la ranura 0 lleva el primer KB de la página 0) o, si ahí no hay ROM, el código de la
 * RAM, con una guarda que compara los bytes de cada instrucción con lo que hay.
 */
#ifndef RECOMP_H
#define RECOMP_H

#include "sms.h"

/* Generado por smsrecomp (gen/views.c y gen/info.c) */
Z80RecompFn recomp_lookup(Sms *g, u16 pc);
extern const char recomp_rom_sha1[41];
extern const char recomp_name[];
extern const unsigned recomp_instructions;

/* El mapeo con que entró la función de ahora (sms_recomp_step): si cambia, se vuelve al despachador */
extern u32 sms_rc_map_ver;

#endif
