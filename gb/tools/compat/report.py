#!/usr/bin/env python3
"""Genera docs/compat/index.html a partir de docs/compat/results.json (lo escribe run.py). El informe es común a
NESRecomp, GBRecomp, GENRecomp y NEORecomp (tools/retrorecomp/compat.py).
Uso: python3 tools/compat/report.py [--salida DIR]"""
from run import GB, ROOT, compat

compat.informe(GB(ROOT))
