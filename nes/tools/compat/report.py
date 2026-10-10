"""Genera docs/compat/index.html a partir de docs/compat/results.json (lo escribe run.py).
Uso: uv run tools/compat/report.py"""
import html, json, pathlib, datetime, subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
D = ROOT / "docs" / "compat"
NOMBRES = {0: "NROM", 1: "MMC1", 2: "UxROM", 3: "CNROM", 4: "MMC3", 5: "MMC5", 7: "AxROM", 9: "MMC2", 10: "MMC4",
           11: "Color Dreams", 13: "CPROM", 18: "Jaleco SS88006", 19: "Namco 163", 23: "VRC2/4", 34: "BNROM/NINA-001",
           66: "GxROM", 69: "FME-7", 71: "Camerica", 75: "VRC1", 79: "NINA-03/06", 87: "Jaleco/Konami 87",
           95: "Namco 118 (Dragon Buster)", 105: "NES-EVENT", 115: "Kasheng", 118: "TxSROM", 119: "TQROM",
           140: "Jaleco JF-11", 180: "UNROM invertido", 184: "Sunsoft 1", 206: "DxROM", 210: "Namco 175/340",
           268: "SMD132/133 (NES 2.0)", 446: "NES 2.0 multicart"}


def estado(r):
    """Etiqueta global de un juego, de mejor a peor."""
    if not r["soportado"]:
        return "sin soporte"
    if r["interprete"] == "error":
        return "no carga"
    rc = r.get("recompilador")
    if rc:
        return {"ok": "recompilado", "diferencia": "diferencia", "falla": "falla"}[rc["resultado"]]
    return "intérprete" if r["interprete"] == "ok" else "pantalla uniforme"


def main():
    rs = json.load(open(D / "results.json"))
    for r in rs:
        r["estado"] = estado(r)
    rs.sort(key=lambda r: (r["mapper"], r["juego"]))
    try:
        ver = subprocess.run(["git", "describe", "--tags", "--always", "--dirty"], cwd=ROOT, capture_output=True,
                             text=True).stdout.strip()
    except Exception:
        ver = "?"
    # Resumen por mapper
    por = {}
    for r in rs:
        m = por.setdefault(r["mapper"], {"n": 0, "ok": 0, "e2": 0, "e2ok": 0, "blanco": 0, "carga": 0})
        m["n"] += 1
        m["ok"] += r["interprete"] == "ok"
        m["blanco"] += r["interprete"] == "pantalla uniforme"
        m["carga"] += r["interprete"] == "error"
        if r.get("recompilador"):
            m["e2"] += 1
            m["e2ok"] += r["recompilador"]["resultado"] == "ok"
    tot = len(rs)
    sop = [r for r in rs if r["soportado"]]
    cargan = sum(r["interprete"] != "error" for r in sop)
    deep = [r for r in rs if r.get("recompilador")]
    deep_ok = [r for r in deep if r["recompilador"]["resultado"] == "ok"]

    filas_m = []
    for m, v in sorted(por.items()):
        sopm = m in {r["mapper"] for r in sop}
        if sopm:
            e2 = f'{v["e2ok"]} / {v["e2"]}' if v["e2"] else "—"
            cls = "ok" if v["e2ok"] == v["e2"] and v["e2"] else "mid"
            if v["carga"]:
                cls = "bad"
        else:
            e2, cls = "—", "none"
        filas_m.append(f'<tr class="{cls}"><td>{m}</td><td>{html.escape(NOMBRES.get(m, ""))}</td><td>{v["n"]}</td>'
                       f'<td>{"sí" if sopm else "no"}</td><td>{v["ok"] if sopm else "—"}</td><td>{e2}</td></tr>')

    filas_j = []
    for r in rs:
        rc = r.get("recompilador")
        cob = vel = dif = img = ""
        if rc and rc.get("fases"):
            f = rc["fases"][-1]
            cob = f'{f.get("pct_prg", "")}% PRG · {f.get("funciones", "")} fn'
            s = [d for d in f.get("semillas", []) if d.get("ciclos_recompilados_pct") is not None]
            if s:
                dif = f'{min(d["ciclos_recompilados_pct"] for d in s):.1f}% · {max(d.get("interpretadas") or 0 for d in s)} interp.'
            if rc.get("velocidad_x"):
                vel = f'{rc["velocidad_x"]:.0f}×'
        det = html.escape(rc.get("detalle", "") if rc else r.get("detalle", ""))
        slugs = r.get("thumb")
        if slugs:
            img = f'<img loading="lazy" src="thumbs/{slugs}" width="64" height="60" alt="">'
        feats = []
        if r["bateria"]:
            feats.append("batería")
        if r["cuatro_pantallas"]:
            feats.append("4 pantallas")
        if r["prg_kb"] >= 256:
            feats.append(f'PRG {r["prg_kb"]} KB')
        if r["chr_kb"] == 0:
            feats.append("CHR-RAM")
        filas_j.append(
            f'<tr data-estado="{r["estado"]}" data-mapper="{r["mapper"]}">'
            f'<td>{img}</td><td>{html.escape(r["juego"])}</td><td>{r["mapper"]}</td>'
            f'<td>{r["prg_kb"]}/{r["chr_kb"]}</td><td>{", ".join(feats)}</td>'
            f'<td><span class="b {r["estado"].replace(" ", "-")}">{r["estado"]}</span></td>'
            f'<td>{cob}</td><td>{dif}</td><td>{vel}</td><td class="det">{det}</td></tr>')

    estados = {}
    for r in rs:
        estados[r["estado"]] = estados.get(r["estado"], 0) + 1
    chips = "".join(f'<button class="chip" data-f="{k}">{k} <b>{v}</b></button>' for k, v in sorted(estados.items(), key=lambda kv: -kv[1]))
    hoy = datetime.date.today().isoformat()
    doc = f"""<!doctype html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Compatibilidad NESRecomp</title>
<style>
:root{{--bg:#fff;--fg:#1c1e21;--mut:#666;--line:#ddd;--card:#f6f7f9;--ok:#1a7f37;--mid:#9a6700;--bad:#cf222e;--none:#6e7781}}
@media (prefers-color-scheme:dark){{:root{{--bg:#14161a;--fg:#e6e8eb;--mut:#9aa1ab;--line:#2d3139;--card:#1c2026;--ok:#3fb950;--mid:#d29922;--bad:#f85149;--none:#8b949e}}}}
body{{margin:0;padding:24px 16px;background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif}}
main{{max-width:1200px;margin:auto}} h1{{margin:0 0 4px}} h2{{margin-top:36px}}
.mut{{color:var(--mut)}} .cards{{display:flex;gap:12px;flex-wrap:wrap;margin:16px 0}}
.card{{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:12px 16px;min-width:140px}}
.card b{{display:block;font-size:26px}}
table{{border-collapse:collapse;width:100%}} th,td{{padding:6px 8px;border-bottom:1px solid var(--line);text-align:left;vertical-align:middle}}
th{{position:sticky;top:0;background:var(--bg);font-size:13px}} td.det{{font-size:12px;color:var(--mut);max-width:260px;overflow-wrap:anywhere}}
.wrap{{overflow-x:auto}} img{{image-rendering:pixelated;border-radius:3px;display:block}}
tr.ok td:nth-child(6){{color:var(--ok);font-weight:600}} tr.mid td:nth-child(6){{color:var(--mid);font-weight:600}} tr.bad td:nth-child(6){{color:var(--bad);font-weight:600}}
.b{{padding:2px 8px;border-radius:10px;font-size:12px;border:1px solid currentColor;white-space:nowrap}}
.recompilado{{color:var(--ok)}} .intérprete{{color:var(--mid)}} .sin-soporte{{color:var(--none)}} .diferencia,.falla,.no-carga,.pantalla-en-blanco{{color:var(--bad)}}
.chip{{border:1px solid var(--line);background:var(--card);color:var(--fg);border-radius:14px;padding:4px 12px;margin:0 6px 6px 0;cursor:pointer;font:inherit}}
.chip.on{{outline:2px solid var(--fg)}} input{{padding:6px 10px;border:1px solid var(--line);border-radius:6px;background:var(--card);color:var(--fg);font:inherit;width:240px;max-width:100%}}
</style></head><body><main>
<h1>Compatibilidad de NESRecomp</h1>
<div class="mut">{tot} ROMs de la colección No-Intro (NES, con cabecera) · versión {html.escape(ver)} · {hoy}</div>
<div class="cards">
<div class="card"><b>{tot}</b>ROMs probadas</div>
<div class="card"><b>{len(sop)}</b>con mapper soportado</div>
<div class="card"><b>{cargan}</b>corren en el intérprete</div>
<div class="card"><b>{len(deep_ok)} / {len(deep)}</b>recompiladas sin diferencias</div>
</div>
<h2>Cómo se probó</h2>
<p><b>Etapa 1, todas las ROMs:</b> se carga la ROM en el intérprete (<code>nesshot</code>) y se corren 900 cuadros con entradas guionadas
(Start, A, derecha…). «Corre» significa que cargó, no se colgó y la captura no es de un solo color; no garantiza que el juego se vea bien
(las capturas de la tabla deben revisarse a ojo).</p>
<p><b>Etapa 2, una selección por mapper:</b> traza guionada con <code>nestrace</code>, <code>nesrecomp</code>, compilar el proyecto generado,
<code>difftest</code> con las semillas 1 y 2 (1500 cuadros, mando aleatorio más entradas fijas) comparando CPU, RAM, PPU, APU, mapper, imagen y
audio contra el intérprete; después una segunda ronda recompilando con las instrucciones que cayeron al intérprete. «Recompilado» = sin diferencias
en las dos rondas. La columna «Ciclos recompilados» es el peor caso de las semillas de la última ronda.</p>
<h2>Por mapper</h2>
<div class="wrap"><table><thead><tr><th>Mapper</th><th>Nombre</th><th>ROMs</th><th>Soportado</th><th>Corren (etapa 1)</th><th>Recompiladas ok (etapa 2)</th></tr></thead>
<tbody>{"".join(filas_m)}</tbody></table></div>
<h2>Por juego</h2>
<p>{chips} <input id="q" placeholder="Buscar juego o mapper…"></p>
<div class="wrap"><table id="t"><thead><tr><th></th><th>Juego</th><th>Mapper</th><th>PRG/CHR KB</th><th>Características</th><th>Estado</th><th>Código descubierto</th><th>Ciclos recompilados</th><th>Velocidad</th><th>Detalle</th></tr></thead>
<tbody>{"".join(filas_j)}</tbody></table></div>
<script>
let f=null;const rows=[...document.querySelectorAll('#t tbody tr')],q=document.getElementById('q');
function ap(){{const s=q.value.toLowerCase();rows.forEach(r=>{{r.hidden=(f&&r.dataset.estado!==f)||(s&&!(r.textContent.toLowerCase().includes(s)))}})}}
document.querySelectorAll('.chip').forEach(c=>c.onclick=()=>{{f=f===c.dataset.f?null:c.dataset.f;document.querySelectorAll('.chip').forEach(x=>x.classList.toggle('on',x.dataset.f===f));ap()}});
q.oninput=ap;
</script></main></body></html>
"""
    (D / "index.html").write_text(doc, encoding="utf-8")
    print(f"escrito {D / 'index.html'} ({tot} ROMs)")


main()
