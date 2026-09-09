-- SPDX-License-Identifier: MIT
-- Does this game draw anything on REAL hardware in the window we sample?
--
-- vbrecomp calls 29 games blank. Some of that is ours to fix; some may be games
-- that legitimately show nothing in 600 frames without the right input. Only the
-- reference can tell those apart, and knowing which is which decides where the
-- remaining work actually is.
--
-- Reports the first frame at which the framebuffer is non-blank, and how much
-- character data has been loaded, then exits.

local OUT = os.getenv("VBREF_OUT") or [[C:\Users\nedch\AppData\Local\Temp\vbdraw_out.txt]]
local f = io.open(OUT, "w")
local function log(s) if f then f:write(s .. "\n"); f:flush() end end

local function nonzero(domain, len, step)
  local n = 0
  for i = 0, len - 1, step do
    if memory.read_u8(i, domain) ~= 0 then n = n + 1 end
  end
  return n
end

local frame, firstDraw, firstChr = 0, -1, -1
event.onframeend(function()
  frame = frame + 1
  if frame % 10 == 0 then
    if firstChr < 0 and nonzero("CHR RAM", 0x8000, 64) > 0 then firstChr = frame end
    if firstDraw < 0 and nonzero("Framebuffer", 0x6000, 64) > 0 then firstDraw = frame end
  end
  if frame >= 600 then
    log(string.format("firstDraw=%d firstChr=%d chrFinal=%d fbFinal=%d",
                      firstDraw, firstChr,
                      nonzero("CHR RAM", 0x8000, 16),
                      nonzero("Framebuffer", 0x6000, 16)))
    if f then f:close() end
    client.exit()
  end
end)
