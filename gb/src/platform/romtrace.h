/*
 * Direcciones de ROM que ejecutó el intérprete, para el ciclo de trazas (--uncovered y gbtrace →
 * gbrecomp --trace). Lo comparten difftest, gbtrace y el ejecutable del juego (main_sdl.c), como
 * ramtrace.h para el código de RAM.
 *
 * Una línea "BB:AAAA" lleva el banco y la dirección con que el CPU ejecutó la instrucción
 * (trace_line.h). Casi siempre el banco 0 está en $0000-$3FFF y los demás en $4000-$7FFF, pero en
 * el modo 1 del MBC1 $0000-$3FFF muestra otro banco: $20, $40 o $60 en las ROMs de más de 512 KB,
 * y $10, $20 o $30 en los cartuchos de varios juegos (MBC1M), donde así corre cada juego. Ese
 * código se anota con su dirección real ("10:0150"), y gbrecomp lo compila para $0000 (las vistas
 * del banco 0, en gbrecomp.c). Antes salía como 0x4000 + desplazamiento ("10:4150"): gbrecomp lo
 * compilaba para $4000, donde nunca corre, y Bomberman Collection seguía en el intérprete.
 *
 * Por eso se cuenta por ranura y no por banco: la ranura es el banco, más ROM_SLOT_LOW si es un
 * banco distinto del 0 visto en $0000-$3FFF (el mismo banco puede correr en las dos regiones).
 */
#ifndef ROMTRACE_H
#define ROMTRACE_H

#include "gb.h"

#define ROM_MAX_BANKS 512                  /* 8 MB */
#define ROM_SLOT_LOW ROM_MAX_BANKS
#define ROM_SLOTS (2 * ROM_MAX_BANKS)

/* Ranura y desplazamiento en el banco de la instrucción en pc; 0 si pc no está en la ROM */
static inline int rom_slot(const GB *g, u16 pc, u32 *slot, u16 *off)
{
    u32 bank;
    if (pc < 0x4000) bank = g->mbc.bank0;
    else if (pc < 0x8000) bank = g->mbc.bankx;
    else return 0;
    if (bank >= ROM_MAX_BANKS) return 0;
    *slot = pc < 0x4000 && bank ? ROM_SLOT_LOW + bank : bank;
    *off = (u16)(pc & 0x3FFF);
    return 1;
}

static inline u32 rom_slot_bank(u32 slot) { return slot % ROM_MAX_BANKS; }

/* La dirección con que corrió */
static inline u16 rom_slot_addr(u32 slot, u16 off)
{
    return (u16)(slot == 0 || slot >= ROM_SLOT_LOW ? off : 0x4000 + off);
}

#endif
