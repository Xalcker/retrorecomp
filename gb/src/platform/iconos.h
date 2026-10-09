/*
 * Íconos de la ventana: Game Boy, Game Boy Color y Super Game Boy (un control
 * de SNES). RGBA de ICONO_LADO x ICONO_LADO. Ver iconos.c y tools/iconos.py.
 */
#ifndef ICONOS_H
#define ICONOS_H

#define ICONO_LADO 64

extern const unsigned char icono_gb[ICONO_LADO * ICONO_LADO * 4];
extern const unsigned char icono_gbc[ICONO_LADO * ICONO_LADO * 4];
extern const unsigned char icono_sgb[ICONO_LADO * ICONO_LADO * 4];

#endif
