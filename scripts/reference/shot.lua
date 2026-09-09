-- SPDX-License-Identifier: MIT
--
-- Capture one reference screenshot from BizHawk's Mednafen Virtual Boy core, at
-- a chosen frame, under the same input our headless runs inject.
--
-- Everything else in this directory compares NUMBERS -- how much of WRAM is
-- non-zero, which pages differ. That was the right tool while games were blank.
-- Now that most of them draw, the question is whether what they draw is RIGHT,
-- and for that the only honest oracle is the reference emulator's own picture.
--
--   VBREF_FRAME=400 VBREF_OUT=C:\...\ref.400.png \
--       EmuHawk.exe "rom.vb" --lua=scripts/reference/shot.lua
--
-- ONE frame per launch, deliberately. client.screenshot() is deferred, so
-- taking several shots in a single run writes the final framebuffer to every
-- path -- four byte-identical files that look like a settled screen and are
-- not. shotdiff.py launches this once per frame point instead.
--
-- Holds the same 0x303C mask the driver injects (A|B|L|R|Start|Select), in the
-- same bursts, so a game gated on a button press gets past it here too.

local OUT   = os.getenv("VBREF_OUT")   or [[C:\Users\Public\vbref.png]]
local UNTIL = tonumber(os.getenv("VBREF_FRAME") or "400")

local HELD = { A = true, B = true, Start = true, Select = true, L = true, R = true }

local frame = 0
event.onframeend(function()
  frame = frame + 1
  -- press in bursts, exactly as games/driver/main.c does
  if frame > 60 and (frame % 120) < 40 then joypad.set(HELD) end
  if frame >= UNTIL then
    client.screenshot(OUT)
    client.exit()
  end
end)
