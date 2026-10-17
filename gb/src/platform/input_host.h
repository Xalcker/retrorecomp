/*
 * Lo que inputs.h y cli.h comunes (../shared, los mismos de NESRecomp, GENRecomp, NEORecomp y SMSRecomp)
 * toman del Game Boy:
 *   - los botones (PAD_* de gb.h) y un solo jugador al grabar;
 *   - dos líneas opcionales al principio de las entradas grabadas: "modelo dmg|cgb|sgb" (con qué modelo se
 *     grabó; un mismo juego tarda distinto en cada uno) y "paleta up+a" (la combinación del modo de
 *     compatibilidad del GBC);
 *   - las opciones propias: --model dmg|cgb|sgb (por omisión, el de las entradas o el de la ROM),
 *     --palette.
 */
#ifndef GB_INPUT_HOST_H
#define GB_INPUT_HOST_H

#include <stdio.h>
#include <string.h>

#include "gb.h"
#include "random_pad.h"

typedef u8 InputPad;
#define INPUT_BUTTONS 8
#define INPUT_NAMES "A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN"     /* orden de PAD_* */
#define INPUT_PLAYERS 1

/* Mando aleatorio de --seed (random_pad.h) con los bits del Game Boy: los cuatro primeros botones
 * coinciden con los de la NES; la cruz va en otro orden */
static inline InputPad input_from_rp(unsigned rp)
{
    InputPad pad = (InputPad)(rp & (RP_A | RP_B | RP_SELECT | RP_START));
    if (rp & RP_UP) pad |= PAD_UP;
    if (rp & RP_DOWN) pad |= PAD_DOWN;
    if (rp & RP_LEFT) pad |= PAD_LEFT;
    if (rp & RP_RIGHT) pad |= PAD_RIGHT;
    return pad;
}

/* "dmg", "cgb", "sgb", "auto" → GB_MODEL_*; cualquier otra cosa → -2 */
static inline int model_from_name(const char *s)
{
    if (!strcmp(s, "dmg")) return GB_MODEL_DMG;
    if (!strcmp(s, "cgb")) return GB_MODEL_CGB;
    if (!strcmp(s, "sgb")) return GB_MODEL_SGB;
    if (!strcmp(s, "auto")) return GB_MODEL_AUTO;
    return -2;
}

static inline const char *model_name(const GB *g)
{
    return g->cgb_hw ? "cgb" : g->sgb ? "sgb" : "dmg";
}

/* Las entradas grabadas: el modelo y la paleta del encabezado */
#define INPUT_HOST_SCRIPT_FIELDS int model; char palette[16];     /* GB_MODEL_AUTO y "" si no lo dicen */
#define INPUT_HOST_SCRIPT_INIT(s) ((s)->model = GB_MODEL_AUTO)
#define INPUT_HOST_LINE(s, line) gb_input_line((line), &(s)->model, (s)->palette, sizeof (s)->palette)

static inline int gb_input_line(const char *line, int *model, char *palette, size_t size)
{
    char word[16];
    if (sscanf(line, " modelo %15s", word) == 1) return (*model = model_from_name(word)) < GB_MODEL_AUTO ? -1 : 1;
    if (sscanf(line, " paleta %15s", word) == 1) { snprintf(palette, size, "%s", word); return 1; }
    return 0;
}

/* Grabación: el encabezado con el modelo (y la paleta, si se eligió) */
static inline void inputs_record_header(FILE *f, const GB *g, const char *palette)
{
    fprintf(f, "modelo %s\n", model_name(g));
    if (palette && *palette) fprintf(f, "paleta %s\n", palette);
}

/* cli.h: --model y --palette; el modelo y la paleta, si no se dan, de --input */
#define CLI_HAS_MODEL
#define CLI_HOST_FIELDS const char *palette; int model;
#define CLI_HOST_VALUE(c, a, v) (!strcmp((a), "--palette") ? ((c)->palette = (v), 1) : 0)
#define CLI_HOST_DONE(c) \
    gb_cli_done(&(c)->model, (c)->model_arg, (c)->input ? (c)->script.model : GB_MODEL_AUTO, \
                &(c)->palette, (c)->input ? (c)->script.palette : "")

static inline int gb_cli_done(int *model, const char *model_arg, int script_model,
                              const char **palette, const char *script_palette)
{
    *model = model_arg ? model_from_name(model_arg) : GB_MODEL_AUTO;
    if (*model < GB_MODEL_AUTO) { fprintf(stderr, "modelo desconocido: %s\n", model_arg); return 2; }
    if (*model == GB_MODEL_AUTO) *model = script_model;
    if (!*palette && *script_palette) *palette = script_palette;
    return 0;
}

#endif
