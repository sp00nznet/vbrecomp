-- SPDX-License-Identifier: MIT
-- Dump the reference's CHR RAM at a chosen frame.
--
-- Knowing a game loads character data on hardware but not under vbrecomp says
-- WHEN we diverge. Dumping the actual tile bytes says WHAT the game loaded --
-- and searching the ROM for those bytes says WHERE it copied them from, which
-- is the address to look for in the recompiled code.
--
--   VBREF_FRAME=90 VBREF_OUT=<path> EmuHawk.exe <rom> --lua=dumpchr.lua

local FRAME = tonumber(os.getenv("VBREF_FRAME") or "90")
local OUT = os.getenv("VBREF_OUT") or [[C:\Users\nedch\AppData\Local\Temp\vbchr.bin]]

local frame = 0
event.onframeend(function()
  frame = frame + 1
  if frame == FRAME then
    local f = io.open(OUT, "wb")
    if f then
      local t = {}
      for i = 0, 0x7FFF do           -- 32 KB: all four CHR segments
        t[#t + 1] = string.char(memory.read_u8(i, "CHR RAM"))
        if #t == 4096 then f:write(table.concat(t)); t = {} end
      end
      if #t > 0 then f:write(table.concat(t)) end
      f:close()
    end
    client.exit()
  end
end)
