"""Genera docs/compat/index.html a partir de docs/compat/results.json (lo escribe run.py).
Uso: uv run tools/compat/report.py"""
import datetime, html, json, pathlib, subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
D = ROOT / "docs" / "compat"
ORDEN_MBC = ["ROM", "MBC1", "MBC1M", "MBC2", "MBC3", "MBC5"]
CONSOLA = {"dmg": "Game Boy", "sgb": "Super Game Boy", "cgb": "Game Boy Color"}
ESTADOS = ["recompilado", "intérprete", "diferencia", "falla", "pantalla en blanco", "se cuelga", "no carga", "sin soporte"]


def estado(r):
    """Etiqueta global de un juego, de mejor a peor."""
    if not r.get("soportado"):
        return "sin soporte"
    i = r.get("interprete")
    if i == "error":
        return "no carga"
    rc = r.get("recompilador")
    if rc:
        return {"ok": "recompilado", "diferencia": "diferencia", "falla": "falla"}[rc["resultado"]]
    return {"ok": "intérprete", "se cuelga": "se cuelga"}.get(i, "pantalla en blanco")


def clave_mbc(m):
    return (ORDEN_MBC.index(m) if m in ORDEN_MBC else 99, m)


def e(s):
    return html.escape(str(s))


def main():
    rs = [r for r in json.load(open(D / "results.json", encoding="utf-8")) if "mbc" in r]
    for r in rs:
        r["estado"] = estado(r)
    rs.sort(key=lambda r: r["juego"].lower())
    try:
        ver = subprocess.run(["git", "describe", "--tags", "--always", "--dirty"], cwd=ROOT, capture_output=True,
                             text=True).stdout.strip() or "?"
    except Exception:
        ver = "?"
    tot = len(rs)
    sop = [r for r in rs if r["soportado"]]
    corren = [r for r in sop if r.get("interprete") == "ok"]
    deep = [r for r in rs if r.get("recompilador")]
    deep_ok = [r for r in deep if r["recompilador"]["resultado"] == "ok"]
    vel = sorted(r["recompilador"]["velocidad_x"] for r in deep_ok if r["recompilador"].get("velocidad_x"))
    vel_med = f"{vel[len(vel) // 2]}×" if vel else "—"

    def resumen(clave, orden, nombre):
        grupos = {}
        for r in rs:
            g = grupos.setdefault(clave(r), {"n": 0, "ok": 0, "blanco": 0, "cuelga": 0, "carga": 0, "e2": 0, "e2ok": 0, "sop": r["soportado"]})
            g["n"] += 1
            i = r.get("interprete")
            g["ok"] += i == "ok"
            g["blanco"] += i == "pantalla en blanco"
            g["cuelga"] += i == "se cuelga"
            g["carga"] += i == "error"
            if r.get("recompilador"):
                g["e2"] += 1
                g["e2ok"] += r["recompilador"]["resultado"] == "ok"
        filas = []
        for k in sorted(grupos, key=orden):
            g = grupos[k]
            pct = 100 * g["ok"] / g["n"]
            cls = "none" if not g["sop"] else ("ok" if pct >= 95 else "mid" if pct >= 80 else "bad")
            e2 = f'{g["e2ok"]} / {g["e2"]}' if g["e2"] else "—"
            filas.append(f'<tr class="{cls}"><td>{e(nombre(k))}</td><td class="n">{g["n"]}</td><td>{"sí" if g["sop"] else "no"}</td>'
                         f'<td class="n pct">{g["ok"]} <span class="mut">({pct:.0f}%)</span></td><td class="n">{g["blanco"] or ""}</td>'
                         f'<td class="n">{g["cuelga"] or ""}</td><td class="n">{g["carga"] or ""}</td><td class="n">{e2}</td></tr>')
        return "".join(filas)

    cab = ('<thead><tr><th>{}</th><th class="n">ROMs</th><th>Soportado</th><th class="n">Corren (etapa 1)</th>'
           '<th class="n">En blanco</th><th class="n">Se cuelgan</th><th class="n">No cargan</th>'
           '<th class="n">Recompiladas ok (etapa 2)</th></tr></thead>')
    t_mbc = resumen(lambda r: r["mbc"], clave_mbc, lambda k: k)
    t_con = resumen(lambda r: r["modelo"], lambda k: list(CONSOLA).index(k), lambda k: CONSOLA[k])

    filas_j = []
    for r in rs:
        rc = r.get("recompilador")
        cob = pct = velo = ""
        if rc and rc.get("fases"):
            f = rc["fases"][-1]
            if f.get("pct_rom") is not None:
                cob = f'{f["pct_rom"]:.1f}% · {f.get("funciones", "")} fn'
            s = [d for d in f.get("semillas", []) if d.get("recompilado_pct") is not None]
            if s:
                pct = f'{min(d["recompilado_pct"] for d in s):.2f}%'
            if rc.get("velocidad_x"):
                velo = f'{rc["velocidad_x"]}× <span class="mut">/ {rc.get("velocidad_interp_x") or "?"}×</span>'
        det = (rc or {}).get("detalle") or r.get("detalle", "")
        if not r["soportado"] and r.get("interprete"):
            det = f'intérprete: {r["interprete"]}' + (f" · {det}" if det else "")
        img = f'<img loading="lazy" src="thumbs/{e(r["thumb"])}" width="80" height="72" alt="">' if r.get("thumb") else ""
        feats = []
        if r["cgb"] == "solo":
            feats.append("solo GBC")
        elif r["cgb"] == "compatible":
            feats.append("GBC + GB")
        if r["sgb"]:
            feats.append("SGB")
        if r["bateria"]:
            feats.append("batería")
        if r["rtc"]:
            feats.append("reloj")
        if r["rumble"]:
            feats.append("vibración")
        ram = r["ram_kb"]
        ram_s = f'{ram:g}' if ram else "—"
        filas_j.append(
            f'<tr data-estado="{e(r["estado"])}" data-mbc="{e(r["mbc"])}">'
            f'<td>{img}</td><td>{e(r["juego"])}<div class="mut sm">{e(r["coleccion"])} · {e(CONSOLA[r["modelo"]])}</div></td>'
            f'<td>{e(r["mbc"])}</td><td class="n">{r["rom_kb"]}/{ram_s}</td><td class="sm">{e(", ".join(feats))}</td>'
            f'<td><span class="b {e(r["estado"].replace(" ", "-"))}">{e(r["estado"])}</span></td>'
            f'<td class="n sm">{cob}</td><td class="n">{pct}</td><td class="n">{velo}</td><td class="det">{e(det)}</td></tr>')

    cuenta = {}
    for r in rs:
        cuenta[r["estado"]] = cuenta.get(r["estado"], 0) + 1
    chips = "".join(f'<button class="chip" data-f="{e(k)}"><span class="b {e(k.replace(" ", "-"))}">{e(k)}</span> <b>{cuenta[k]}</b></button>'
                    for k in ESTADOS if k in cuenta)
    mbcs = sorted({r["mbc"] for r in rs}, key=clave_mbc)
    opciones = "".join(f'<option value="{e(m)}">{e(m)}</option>' for m in mbcs)
    hoy = datetime.date.today().isoformat()
    doc = f"""<!doctype html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Compatibilidad GBRecomp</title>
<style>
:root{{--bg:#fff;--fg:#1c1e21;--mut:#666;--line:#ddd;--card:#f6f7f9;--ok:#1a7f37;--mid:#9a6700;--bad:#cf222e;--none:#6e7781}}
@media (prefers-color-scheme:dark){{:root:not([data-theme="light"]){{--bg:#14161a;--fg:#e6e8eb;--mut:#9aa1ab;--line:#2d3139;--card:#1c2026;--ok:#3fb950;--mid:#d29922;--bad:#f85149;--none:#8b949e}}}}
:root[data-theme="dark"]{{--bg:#14161a;--fg:#e6e8eb;--mut:#9aa1ab;--line:#2d3139;--card:#1c2026;--ok:#3fb950;--mid:#d29922;--bad:#f85149;--none:#8b949e}}
body{{margin:0;padding:24px 16px;background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif}}
main{{max-width:1280px;margin:auto}} h1{{margin:0 0 4px}} h2{{margin-top:36px}} code{{font-size:13px}}
.mut{{color:var(--mut)}} .sm{{font-size:12px}} .n{{text-align:right;font-variant-numeric:tabular-nums}}
.cards{{display:flex;gap:12px;flex-wrap:wrap;margin:16px 0}}
.card{{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:12px 16px;min-width:150px}}
.card b{{display:block;font-size:26px}}
table{{border-collapse:collapse;width:100%}} th,td{{padding:6px 8px;border-bottom:1px solid var(--line);text-align:left;vertical-align:middle}}
th{{position:sticky;top:0;background:var(--bg);font-size:13px;z-index:1}} td.det{{font-size:12px;color:var(--mut);max-width:280px;overflow-wrap:anywhere}}
.wrap{{overflow-x:auto}} img{{image-rendering:pixelated;border-radius:3px;display:block}}
tr.ok .pct{{color:var(--ok);font-weight:600}} tr.mid .pct{{color:var(--mid);font-weight:600}} tr.bad .pct{{color:var(--bad);font-weight:600}} tr.none td{{color:var(--none)}}
.b{{padding:2px 8px;border-radius:10px;font-size:12px;border:1px solid currentColor;white-space:nowrap}}
.recompilado{{color:var(--ok)}} .intérprete{{color:var(--mid)}} .sin-soporte{{color:var(--none)}}
.diferencia,.falla,.no-carga,.pantalla-en-blanco,.se-cuelga{{color:var(--bad)}}
.chip{{border:1px solid var(--line);background:var(--card);color:var(--fg);border-radius:14px;padding:4px 10px;margin:0 6px 6px 0;cursor:pointer;font:inherit}}
.chip.on{{outline:2px solid var(--fg)}} .ctl{{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-bottom:8px}}
input,select{{padding:6px 10px;border:1px solid var(--line);border-radius:6px;background:var(--card);color:var(--fg);font:inherit}}
input{{width:260px;max-width:100%}}
</style></head><body><main>
<h1>Compatibilidad de GBRecomp</h1>
<div class="mut">{tot} ROMs de las colecciones No-Intro de Game Boy y Game Boy Color · versión {e(ver)} · {hoy}</div>
<div class="cards">
<div class="card"><b>{tot}</b>ROMs probadas</div>
<div class="card"><b>{len(sop)}</b>con MBC soportado</div>
<div class="card"><b>{len(corren)}</b>corren en el intérprete</div>
<div class="card"><b>{len(deep_ok)} / {len(deep)}</b>recompiladas sin diferencias</div>
<div class="card"><b>{vel_med}</b>velocidad mediana recompilado</div>
</div>
<h2>Cómo se probó</h2>
<p><b>Etapa 1, todas las ROMs:</b> cada ROM corre en el intérprete (<code>gbtest</code>) con la consola que elige el runtime en automático
(Game Boy Color si el cartucho lo admite, si no Super Game Boy si lo admite, si no Game Boy) y entradas guionadas (Start, A, B, flechas),
y se toman capturas en los cuadros 450, 900 y 1500. «Corre» significa que cargó, no ejecutó un opcode ilegal y la captura más colorida no es
de un solo color; no garantiza que el juego se vea bien: las capturas de la tabla hay que revisarlas a ojo.</p>
<p><b>Etapa 2, una selección por MBC y consola:</b> <code>gbrecomp</code> (solo análisis estático), compilar el proyecto generado y correr
<code>difftest</code> con las semillas 1 y 2 (3600 cuadros cada una, un minuto de juego con el mando al azar), que compara el estado completo
(CPU, ciclos, RAM, VRAM, OAM, registros, timer, PPU, RAM del cartucho) contra el intérprete al final de cada cuadro. Si algo cayó al
intérprete en ROM, se recompila con esas direcciones (<code>--uncovered</code> → <code>--trace</code>) y se repite. «Recompilado» = sin
diferencias en todas las rondas. «Recompilado %» es la fracción de instrucciones ejecutadas por código recompilado en el peor caso de la
última ronda (lo que falta suele ser código en RAM). La velocidad es la de <code>difftest --bench</code> (recompilado / intérprete, sin
ventana ni límite de cuadros).</p>
<p class="mut">Para repetirlo: <code>uv run tools/compat/scan.py DIR_GB DIR_GBC</code>, <code>uv run tools/compat/run.py</code> y
<code>uv run tools/compat/report.py</code> (ver el README).</p>
<h2>Por MBC</h2>
<div class="wrap"><table>{cab.format("MBC")}<tbody>{t_mbc}</tbody></table></div>
<h2>Por consola</h2>
<div class="wrap"><table>{cab.format("Consola")}<tbody>{t_con}</tbody></table></div>
<h2>Por juego</h2>
<div class="ctl">{chips}</div>
<div class="ctl"><input id="q" placeholder="Buscar juego…" aria-label="Buscar juego">
<select id="m" aria-label="MBC"><option value="">Todos los MBC</option>{opciones}</select>
<span class="mut" id="cnt"></span></div>
<div class="wrap"><table id="t"><thead><tr><th></th><th>Juego</th><th>MBC</th><th class="n">ROM/RAM KB</th><th>Características</th><th>Estado</th>
<th class="n">Código descubierto</th><th class="n">Recompilado</th><th class="n">Velocidad</th><th>Detalle</th></tr></thead>
<tbody>{"".join(filas_j)}</tbody></table></div>
<script>
let f=null;const rows=[...document.querySelectorAll('#t tbody tr')],q=document.getElementById('q'),m=document.getElementById('m'),cnt=document.getElementById('cnt');
function ap(){{const s=q.value.toLowerCase();let n=0;rows.forEach(r=>{{r.hidden=(f&&r.dataset.estado!==f)||(m.value&&r.dataset.mbc!==m.value)||(s&&!r.textContent.toLowerCase().includes(s));n+=!r.hidden}});cnt.textContent=n+' de '+rows.length}}
document.querySelectorAll('.chip').forEach(c=>c.onclick=()=>{{f=f===c.dataset.f?null:c.dataset.f;document.querySelectorAll('.chip').forEach(x=>x.classList.toggle('on',x.dataset.f===f));ap()}});
q.oninput=ap;m.onchange=ap;ap();
</script></main></body></html>
"""
    (D / "index.html").write_text(doc, encoding="utf-8")
    print(f"escrito {D / 'index.html'} ({tot} ROMs; {len(deep_ok)}/{len(deep)} recompiladas ok)")


main()
