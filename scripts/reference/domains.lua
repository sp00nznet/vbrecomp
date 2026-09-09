-- SPDX-License-Identifier: MIT
-- List the core's memory domains, their sizes, and how much of each is
-- non-zero at a chosen frame.
--
-- The whole reference comparison rests on reading the right domain. A domain
-- that reads back all zeros while the game is visibly running means the name
-- is wrong, not that the game uses no memory -- and a wrong domain produces
-- confident, wrong conclusions.
local FRAME = tonumber(os.getenv("VBREF_FRAME") or "90")
local OUT = os.getenv("VBREF_OUT") or [[C:\Users\nedch\AppData\Local\Temp\vbdomains.txt]]
local f = io.open(OUT, "w")
local frame = 0
event.onframeend(function()
  frame = frame + 1
  if frame ~= FRAME then return end
  for name, size in pairs(memory.getmemorydomainlist and {} or {}) do end
  local list = memory.getmemorydomainlist()
  for i = 0, #list do
    local d = list[i]
    if d then
      local sz = memory.getmemorydomainsize(d)
      local nz, step = 0, math.max(1, math.floor(sz / 4096))
      for a = 0, sz - 1, step do
        if memory.read_u8(a, d) ~= 0 then nz = nz + 1 end
      end
      f:write(string.format("%-16s size=0x%X sampled_nonzero=%d\n", d, sz, nz))
    end
  end
  f:close()
  client.exit()
end)
