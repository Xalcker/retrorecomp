-- Lo carga tools/oracles/mesen.py: igual que ramdump, mantiene botones en rangos de cuadros, corre ORACLE_FRAMES
-- cuadros y escribe los 2 KB de RAM en ORACLE_OUT (una línea "DIRECCION VALOR" por byte).
-- ORACLE_HOLD: "boton:desde-hasta boton:desde-hasta ...". El cuadro i es el que corre con el control ya puesto,
-- contando desde 0 como en ramdump.
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
    local t = {}
    for a = 0, 0x7FF do t[#t + 1] = string.format("%04X %02X", a, emu.read(a, emu.memType.nesInternalRam)) end
    local f = io.open(os.getenv("ORACLE_OUT"), "w")
    f:write(table.concat(t, "\n"), "\n")
    f:close()
    emu.stop(0)
  end
end, emu.eventType.endFrame)
