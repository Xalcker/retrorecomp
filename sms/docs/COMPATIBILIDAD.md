# Compatibilidad

Registro de pruebas de SMSRecomp (Sega Master System y Game Gear, NTSC/EE. UU.). Llega en la fase 7 de
[PLAN.md](PLAN.md): la prueba de compatibilidad común de retrorecomp (`tools/compat/run.py` y `report.py`), con
las colecciones de SMS y de Game Gear en el mismo informe (`docs/compat/index.html`, campo `coleccion`) y los
oráculos (Genesis Plus GX y Gearsystem). Las ROMs no se versionan.

Hasta entonces, lo que se comprueba con ROMs está en `make test` (ver «Estado de la validación» en
[ARQUITECTURA.md](ARQUITECTURA.md)): `smsrecomp --info` con las ROMs libres de `make testroms`.
