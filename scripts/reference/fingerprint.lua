-- Where is the reference executing, and what has it put in WRAM?
--
-- vbrecomp runs Jack Bros in a six-subroutine loop around 0x07F006CA-0x07F0091C
-- and never gets character data into VRAM; the reference has CHR by frame 60.
-- Sampling PC per frame says whether the reference is in that same loop (so we
-- diverge inside it) or somewhere else entirely (so we took a wrong branch
-- earlier). The WRAM fingerprint says the same thing from the data side.

local OUT = [[C:\Users\nedch\AppData\Local\Temp\vbpc_out.txt]]
local f = io.open(OUT, "w")
local function log(s) if f then f:write(s .. "\n"); f:flush() end end

log("registers: " .. table.concat((function()
  local t = {}
  for k, _ in pairs(emu.getregisters()) do t[#t + 1] = k end
  table.sort(t); return t
end)(), ", "))

local frame = 0
event.onframeend(function()
  frame = frame + 1
  if frame == 1 or frame % 30 == 0 then
    local r = emu.getregisters()
    local pc = r["PC"] or r["pc"] or -1
    -- A cheap fingerprint of the first 16 KB of WRAM: how much is non-zero.
    local nz = 0
    for i = 0, 0x4000 - 1, 8 do
      if memory.read_u8(i, "DRAM") ~= 0 then nz = nz + 1 end
    end
    local chr = 0
    for i = 0, 0x8000 - 1, 16 do
      if memory.read_u8(i, "CHR RAM") ~= 0 then chr = chr + 1 end
    end
    log(string.format("frame %4d  PC=%08X  wram[0..16K) nz=%d  chr nz=%d",
                      frame, pc, nz, chr))
  end
  if frame >= 240 then
    log("done"); if f then f:close() end; client.exit()
  end
end)
