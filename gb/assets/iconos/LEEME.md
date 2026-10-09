# Íconos

`GB.png`, `GBC.png` y `SGB.png` son de **Retro game console icons**, de
KyleBing: https://github.com/KyleBing/retro-game-console-icons
(`series_trimui/300w@1x`), con licencia
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

Aquí están sin cambios. `tools/iconos.py` los convierte en los íconos de los
juegos recompilados, con estos cambios: cada uno se recorta al dibujo, se
centra en un cuadrado transparente con un margen del 6 % y se escala.

- `src/platform/iconos.c`: 64x64 RGBA, el ícono de la ventana (Game Boy,
  Game Boy Color o Super Game Boy, según la consola encendida).
- `src/platform/icono_gb.ico`, `icono_gbc.ico`, `icono_sgb.ico`: 16 a 256 px,
  el ícono del `.exe` en Windows.

Los proyectos generados llevan los íconos convertidos y este crédito en su
`LEEME.txt` y en Ayuda > Acerca de.
