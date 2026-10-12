-- Lo carga tools/oracles/mesen.py shot: mantiene botones en rangos de cuadros (como mesen_ramdump.lua) y, al
-- terminar el cuadro ORACLE_FRAMES, guarda la captura en ORACLE_OUT (PNG).
-- Mesen dispara inputPolled una vez por cuadro, en la línea 241, y lo que se pase a setInput vale hasta la siguiente:
-- los botones cambian al empezar el vblank. nesshot hace lo mismo con --entradas-241 (tools/compat/vsmesen.py).
local frames = tonumber(os.getenv("ORACLE_FRAMES"))
local holds = {}
for name, a, b in (os.getenv("ORACLE_HOLD") or ""):gmatch("(%a+):(%d+)%-(%d+)") do
  holds[#holds + 1] = { name = name, from = tonumber(a), to = tonumber(b) }
end
local frame = 0
emu.addEventCallback(function()
  local pad = {}
  for _, h in ipairs(holds) do
    if frame >= h.from and frame <= h.to then pad[h.name] = true end
  end
  emu.setInput(pad, 0)
end, emu.eventType.inputPolled)
emu.addEventCallback(function()
  frame = frame + 1
  if frame == frames then
    local f = io.open(os.getenv("ORACLE_OUT"), "wb")
    f:write(emu.takeScreenshot())
    f:close()
    emu.stop(0)
  end
end, emu.eventType.endFrame)
