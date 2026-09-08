-- Dump the reference's WRAM at a chosen frame, for diffing against ours.
--
-- Knowing a game loads character data by frame N on hardware says WHEN we
-- diverge; it does not say where. Diffing the 64 KB of WRAM at that frame does:
-- the first block that differs is the first thing the game computed that we did
-- not, and it points at the code that computed it.
--
--   VBREF_FRAME=10 VBREF_OUT=<path> EmuHawk.exe <rom> --lua=dumpwram.lua

local FRAME = tonumber(os.getenv("VBREF_FRAME") or "10")
local OUT = os.getenv("VBREF_OUT") or [[C:\Users\nedch\AppData\Local\Temp\vbwram.bin]]

local frame = 0
event.onframeend(function()
  frame = frame + 1
  if frame == FRAME then
    local f = io.open(OUT, "wb")
    if f then
      -- 64 KB, the whole of the VB's main RAM. The domain is WRAM:
      -- DRAM is 0x20000 bytes and is the VIP's BGMap bank, not the CPU's
      -- work RAM. Reading DRAM's first 64 KB returns the empty low BGMap
      -- segments, which is what made Jack Bros' RAM look untouched on
      -- hardware and sent a whole line of enquiry the wrong way.
      local t = {}
      for i = 0, 0xFFFF do
        t[#t + 1] = string.char(memory.read_u8(i, "WRAM"))
        if #t == 4096 then f:write(table.concat(t)); t = {} end
      end
      if #t > 0 then f:write(table.concat(t)) end
      f:close()
    end
    client.exit()
  end
end)
