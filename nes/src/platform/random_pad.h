/*
 * Mando aleatorio de --seed: el mismo en NESRecomp y GBRecomp. Este archivo es idéntico en los
 * dos repositorios (retrorecomp/tests/contract.py lo comprueba): si lo cambias, cámbialo en el otro.
 *
 * Mantiene una combinación de botones entre 4 y 43 cuadros. Start es poco frecuente (en muchos
 * juegos pausa la partida) y nunca se pulsa Select; A, B y la derecha pesan más, para avanzar en
 * el juego. Con la misma semilla, la secuencia es la misma en las dos consolas.
 *
 * Los botones salen como bits lógicos RP_*; cada consola los pasa a su registro (en la NES son
 * los mismos bits; en el Game Boy cambia el orden de la cruz).
 */
#ifndef RANDOM_PAD_H
#define RANDOM_PAD_H

enum {
    RP_A = 0x01, RP_B = 0x02, RP_SELECT = 0x04, RP_START = 0x08,
    RP_UP = 0x10, RP_DOWN = 0x20, RP_LEFT = 0x40, RP_RIGHT = 0x80
};

typedef struct {
    unsigned rng;           /* xorshift de 32 bits */
    unsigned char now;      /* combinación actual */
    long until;             /* cuadro en que cambia */
} RandomPad;

static inline void random_pad_init(RandomPad *p, unsigned seed)
{
    p->rng = seed ? seed : 1;
    p->now = 0;
    p->until = 0;
}

static inline unsigned random_pad_next(RandomPad *p)
{
    p->rng ^= p->rng << 13; p->rng ^= p->rng >> 17; p->rng ^= p->rng << 5;
    return p->rng & 0xFFFFFFFFu;
}

/* Botones (RP_*) en el cuadro `frame`; los cuadros se piden en orden, empezando en 0 */
static inline unsigned char random_pad(RandomPad *p, long frame)
{
    if (frame >= p->until) {
        unsigned r = random_pad_next(p);
        p->until = frame + 4 + (long)(r % 40);
        if ((r >> 24) % 24 == 0) p->now = RP_START;
        else switch ((r >> 8) % 6) {
            case 0: p->now = 0; break;
            case 1: p->now = RP_RIGHT; break;
            case 2: p->now = RP_A; break;
            case 3: p->now = RP_RIGHT | RP_A; break;
            case 4: p->now = RP_LEFT | ((r >> 16) & 1 ? RP_A : RP_B); break;
            default: p->now = (unsigned char)((1u << ((r >> 16) % 8)) & ~(unsigned)RP_SELECT); break;
        }
    }
    return p->now;
}

#endif
