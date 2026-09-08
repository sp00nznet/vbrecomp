-- Reference probe: what does real hardware do that our recompilation does not?
--
-- vbrecomp reports Jack Bros writing 8,192 character bytes all zero, because the
-- WRAM buffer at 0x05003800 it copies into VRAM is never filled. This runs the
-- same ROM on BizHawk's Mednafen VB core and reports, per sampled frame, how
-- much of that buffer and of CHR VRAM is non-zero -- so "the game fills it at
-- frame N" and "the game never fills it either" are told apart.

local OUT = [[C:\Users\nedch\AppData\Local\Temp\vbprobe_out.txt]]
local f = io.open(OUT, "w")

local function log(s)
  if f then f:write(s .. "\n"); f:flush() end
end

log("domains: " .. table.concat(memory.getmemorydomainlist(), ", "))

-- This core names them DRAM (the 64 KB of main RAM the VB calls WRAM) and
-- "CHR RAM" (the character/tile half of VIP memory).
local wram, vram
for _, d in ipairs(memory.getmemorydomainlist()) do
  if d == "DRAM" then wram = d end
  if d == "CHR RAM" then vram = d end
end
log("using wram=" .. tostring(wram) .. " vram=" .. tostring(vram))

-- Count non-zero bytes in a region, sampling every `step` bytes to stay quick.
local function nonzero(domain, base, len, step)
  if not domain then return -1 end
  local n = 0
  for i = 0, len - 1, step do
    if memory.read_u8(base + i, domain) ~= 0 then n = n + 1 end
  end
  return n
end

local frame = 0
event.onframeend(function()
  frame = frame + 1
  if frame % 60 == 0 or frame == 1 then
    -- 0x3800 is the buffer vbrecomp sees empty; 0x0000-0x2000 of VRAM is CHR.
    local buf = nonzero(wram, 0x3800, 0x1C00, 4)
    local chr = nonzero(vram, 0x0000, 0x8000, 16)
    log(string.format("frame %4d  wram[0x3800..0x5400) nonzero=%d  chr nonzero=%d",
                      frame, buf, chr))
  end
  if frame >= 600 then
    log("done")
    if f then f:close() end
    client.exit()
  end
end)
