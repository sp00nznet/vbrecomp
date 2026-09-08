-- drawcheck.lua, but holding buttons the way the corpus survey does.
--
-- Two games (Capitan Sevilla 3D, Null Pointer Mockup) load character data on
-- hardware and still show an empty framebuffer after 600 frames with NO input.
-- That is the same signature as the Snowball Wars demos, which are genuinely
-- blank -- but it is also what a game waiting on "press start" looks like. The
-- survey holds 0x303C; without doing the same here the comparison is not like
-- for like, and "is this ours or is it waiting for a player" stays unanswered.
local OUT = os.getenv("VBREF_OUT") or [[C:\Users\nedch\AppData\Local\Temp\vbdraw_in.txt]]
local f = io.open(OUT, "w")

local function nonzero(domain, len, step)
  local n = 0
  for i = 0, len - 1, step do
    if memory.read_u8(i, domain) ~= 0 then n = n + 1 end
  end
  return n
end

-- 0x303C is A|B|L|R|Start|Select -- the mask the survey injects.
local HELD = { A = true, B = true, Start = true, Select = true, L = true, R = true }

local frame, firstDraw, firstChr = 0, -1, -1
event.onframeend(function()
  frame = frame + 1
  -- press in bursts, exactly as games/driver/main.c does
  if frame > 60 and (frame % 120) < 40 then joypad.set(HELD) end
  if frame % 10 == 0 then
    if firstChr < 0 and nonzero("CHR RAM", 0x8000, 64) > 0 then firstChr = frame end
    if firstDraw < 0 and nonzero("Framebuffer", 0x6000, 64) > 0 then firstDraw = frame end
  end
  if frame >= 600 then
    f:write(string.format("firstDraw=%d firstChr=%d chrFinal=%d fbFinal=%d\n",
                          firstDraw, firstChr,
                          nonzero("CHR RAM", 0x8000, 16),
                          nonzero("Framebuffer", 0x6000, 16)))
    f:close()
    client.exit()
  end
end)
