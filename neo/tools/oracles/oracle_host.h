/*
 * Lo que retro_run común (tools/retrorecomp/shared, el de GENRecomp) toma del Neo Geo: las descripciones
 * de botón de Geolith y FBNeo, y la RAM del 68000 ($100000), como neoramdump.
 */
#ifndef NEO_ORACLE_HOST_H
#define NEO_ORACLE_HOST_H

#define ORACLE_BUTTONS \
    { "up", NEO_UP }, { "down", NEO_DOWN }, { "left", NEO_LEFT }, { "right", NEO_RIGHT }, \
    { "a", NEO_A }, { "b", NEO_B }, { "c", NEO_C }, { "d", NEO_D }, \
    { "start", NEO_START }, { "select", NEO_SELECT }, { "select/coin", NEO_SELECT },
#define ORACLE_RAM_BASE 0x100000
#define ORACLE_RAM_NAME "la RAM del 68000"

#endif
