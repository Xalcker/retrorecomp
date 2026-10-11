#!/usr/bin/env python3
"""Genera docs/compat/index.html a partir de docs/compat/results.json (lo escribe run.py).
Uso: python3 tools/compat/report.py"""
import datetime, html, json, pathlib, subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
D = ROOT / "docs" / "compat"


def estado(r):
    """Etiqueta global de un juego, de mejor a peor"""
    if r.get("interprete") == "error":
        return "no carga"
    e2 = r.get("etapa2")
    if e2:
        return {"ok": "recompilado", "diferencia": "diferencia", "falla": "falla"}[e2["resultado"]]
    return "intérprete" if r.get("interprete") == "ok" else "pantalla uniforme"


def parecido(r, nucleo, cuadro="1200"):
    o = (r.get("oraculos") or {}).get(cuadro) or {}
    v = o.get(f"genrecomp-{nucleo}")
    return v if isinstance(v, dict) else None


def main():
    datos = json.load(open(D / "results.json"))
    rs = datos["roms"]
    for r in rs:
        r["estado"] = estado(r)
    try:
        ver = subprocess.run(["git", "describe", "--tags", "--always", "--dirty"], cwd=ROOT, capture_output=True,
                             text=True).stdout.strip()
    except Exception:
        ver = "?"
    tot = len(rs)
    cargan = sum(r.get("interprete") != "error" for r in rs)
    e2 = [r for r in rs if r.get("etapa2")]
    e2ok = [r for r in e2 if r["etapa2"]["resultado"] == "ok"]
    ciclos = [min(d["ciclos_recompilados_pct"] for d in r["etapa2"]["fases"][-1]["semillas"] if d.get("ciclos_recompilados_pct") is not None)
              for r in e2ok]
    vel = [r["etapa2"]["velocidad_x"] for r in e2ok if r["etapa2"].get("velocidad_x")]
    medias = {}
    for n in ("gpgx", "blastem"):
        ps = [parecido(r, n, c) for r in rs for c in ("600", "1200") if parecido(r, n, c)]
        if ps:
            medias[n] = (100 * sum(p["ram"] for p in ps) / len(ps), 100 * sum(p["img"] or 0 for p in ps) / len(ps), len(ps))

    filas = []
    for r in sorted(rs, key=lambda r: r["juego"].lower()):
        e = r.get("etapa2") or {}
        cob = cic = vl = ""
        if e.get("fases"):
            f = e["fases"][-1]
            cob = f'{f.get("instrucciones") or 0:,} instr. · {f.get("pct_rom") or 0:.1f}% ROM'.replace(",", " ")
            s = [d for d in f.get("semillas", []) if d.get("ciclos_recompilados_pct") is not None]
            if s:
                cic = f'{min(d["ciclos_recompilados_pct"] for d in s):.1f}%'
            if e.get("velocidad_x"):
                vl = f'{e["velocidad_x"]:.0f}× <span class="mut">({e.get("velocidad_interprete_x") or 0:.0f}×)</span>'
        ora = []
        for n in ("gpgx", "blastem"):
            p = parecido(r, n)
            ora.append(f'{100 * p["ram"]:.1f} / {100 * (p["img"] or 0):.0f}' if p else "—")
        feats = []
        if r.get("ssf2"):
            feats.append("mapper SSF2")
        if r.get("sram") and r["sram"] != "no":
            feats.append("SRAM")
        if "6" in (r.get("perifericos") or ""):
            feats.append("6 botones")
        img = f'<img loading="lazy" src="thumbs/{r["thumb"]}" width="80" height="56" alt="">' if r.get("thumb") else ""
        det = html.escape(e.get("detalle", "") if e else r.get("detalle", ""))
        filas.append(
            f'<tr data-estado="{r["estado"]}"><td>{img}</td><td>{html.escape(r["juego"])}</td>'
            f'<td>{r.get("kb", "")}</td><td>{", ".join(feats)}</td>'
            f'<td><span class="b {r["estado"].replace(" ", "-")}">{r["estado"]}</span></td>'
            f'<td>{cob}</td><td>{cic}</td><td>{vl}</td><td>{ora[0]}</td><td>{ora[1]}</td><td class="det">{det}</td></tr>')

    estados = {}
    for r in rs:
        estados[r["estado"]] = estados.get(r["estado"], 0) + 1
    chips = "".join(f'<button class="chip" data-f="{k}">{k} <b>{v}</b></button>' for k, v in sorted(estados.items(), key=lambda kv: -kv[1]))
    ora_txt = " · ".join(f'{ {"gpgx": "Genesis Plus GX", "blastem": "BlastEm"}[n]}: RAM {m[0]:.2f}%, imagen {m[1]:.1f}%'
                         for n, m in medias.items()) or "sin datos (run.py --oraculos)"
    hoy = datos.get("fecha") or datetime.date.today().isoformat()
    doc = f"""<!doctype html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Compatibilidad GENRecomp</title>
<style>
:root{{--bg:#fff;--fg:#1c1e21;--mut:#666;--line:#ddd;--card:#f6f7f9;--ok:#1a7f37;--mid:#9a6700;--bad:#cf222e}}
@media (prefers-color-scheme:dark){{:root{{--bg:#14161a;--fg:#e6e8eb;--mut:#9aa1ab;--line:#2d3139;--card:#1c2026;--ok:#3fb950;--mid:#d29922;--bad:#f85149}}}}
body{{margin:0;padding:24px 16px;background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif}}
main{{max-width:1280px;margin:auto}} h1{{margin:0 0 4px}} h2{{margin-top:36px}}
.mut{{color:var(--mut)}} .cards{{display:flex;gap:12px;flex-wrap:wrap;margin:16px 0}}
.card{{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:12px 16px;min-width:140px}}
.card b{{display:block;font-size:26px}}
table{{border-collapse:collapse;width:100%}} th,td{{padding:6px 8px;border-bottom:1px solid var(--line);text-align:left;vertical-align:middle}}
th{{position:sticky;top:0;background:var(--bg);font-size:13px}} td.det{{font-size:12px;color:var(--mut);max-width:260px;overflow-wrap:anywhere}}
.wrap{{overflow-x:auto}} img{{image-rendering:pixelated;border-radius:3px;display:block}}
.b{{padding:2px 8px;border-radius:10px;font-size:12px;border:1px solid currentColor;white-space:nowrap}}
.recompilado{{color:var(--ok)}} .intérprete{{color:var(--mid)}} .diferencia,.falla,.no-carga,.pantalla-uniforme{{color:var(--bad)}}
.chip{{border:1px solid var(--line);background:var(--card);color:var(--fg);border-radius:14px;padding:4px 12px;margin:0 6px 6px 0;cursor:pointer;font:inherit}}
.chip.on{{outline:2px solid var(--fg)}} input{{padding:6px 10px;border:1px solid var(--line);border-radius:6px;background:var(--card);color:var(--fg);font:inherit;width:240px;max-width:100%}}
</style></head><body><main>
<h1>Compatibilidad de GENRecomp</h1>
<div class="mut">{tot} ROMs · versión {html.escape(ver)} · {hoy}</div>
<div class="cards">
<div class="card"><b>{tot}</b>ROMs probadas</div>
<div class="card"><b>{cargan}</b>corren en el intérprete</div>
<div class="card"><b>{len(e2ok)} / {len(e2)}</b>recompiladas sin diferencias</div>
<div class="card"><b>{(sum(ciclos) / len(ciclos)) if ciclos else 0:.1f}%</b>ciclos en código recompilado (media)</div>
<div class="card"><b>{(sum(vel) / len(vel)) if vel else 0:.0f}×</b>velocidad recompilada (media)</div>
</div>
<p>Parecido medio con los oráculos (cuadros 600 y 1200): {html.escape(ora_txt)}.</p>
<h2>Cómo se probó</h2>
<p><b>Etapa 1:</b> la ROM corre en el intérprete (<code>genshot</code>) con entradas guionadas y se toman capturas a los cuadros 450, 900 y
1500; la miniatura es la más colorida. «Corre» significa que cargó, no se colgó y la captura no es de un solo color; no garantiza que el
juego se vea bien (las miniaturas se revisan a ojo).</p>
<p><b>Etapa 2:</b> <code>genrecomp</code> sin traza, compilar el proyecto generado, <code>difftest</code> con las semillas 1 y 2 (1800 cuadros, mando
aleatorio más entradas fijas) comparando 68000, RAM, VDP, Z80, YM2612, PSG, imagen y audio contra el intérprete; después una segunda ronda
recompilando con lo que cayó al intérprete. «Recompilado» = sin diferencias en las dos rondas. «Ciclos» es el peor caso de la última ronda;
la velocidad es la de <code>bench</code> (entre paréntesis, la del intérprete).</p>
<p><b>Oráculos:</b> la RAM del 68000 y la imagen del cuadro 1200 (con las entradas de la etapa 1) contra Genesis Plus GX y BlastEm
(<code>tools/oracles/</code>): porcentaje de bytes de RAM iguales / de pixeles iguales. Los dos emuladores tampoco coinciden siempre entre sí:
las diferencias que quedan son de temporización fina, que en un juego con entradas guionadas cambia en qué momento cae cada botón.</p>
<h2>Por juego</h2>
<p>{chips} <input id="q" placeholder="Buscar juego…"></p>
<div class="wrap"><table id="t"><thead><tr><th></th><th>Juego</th><th>KB</th><th>Características</th><th>Estado</th><th>Código descubierto</th>
<th>Ciclos recompilados</th><th>Velocidad</th><th>Genesis Plus GX<br><span class="mut">RAM / imagen %</span></th><th>BlastEm<br><span class="mut">RAM / imagen %</span></th><th>Detalle</th></tr></thead>
<tbody>{"".join(filas)}</tbody></table></div>
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
