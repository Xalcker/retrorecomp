/*
 * Lo que retro_run común (tools/retrorecomp/shared, el mismo de NEORecomp) toma del Genesis: las
 * descripciones de botón de Genesis Plus GX y BlastEm, y la RAM del 68000 ($FF0000), como genramdump.
 */
#ifndef GEN_ORACLE_HOST_H
#define GEN_ORACLE_HOST_H

#define ORACLE_BUTTONS \
    { "up", GEN_UP }, { "down", GEN_DOWN }, { "left", GEN_LEFT }, { "right", GEN_RIGHT }, \
    { "a", GEN_A }, { "b", GEN_B }, { "c", GEN_C }, { "x", GEN_X }, { "y", GEN_Y }, { "z", GEN_Z }, \
    { "start", GEN_START }, { "mode", GEN_MODE },
#define ORACLE_RAM_BASE 0xFF0000
#define ORACLE_RAM_NAME "la RAM del 68000"

#endif
