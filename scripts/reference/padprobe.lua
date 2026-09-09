-- SPDX-License-Identifier: MIT
-- Dump the button names BizHawk exposes for the loaded core, and whether a
-- joypad.set() actually takes. Written because reference captures were holding
-- buttons that never reached the emulated pad: Bound High sat on its warning
-- screen for 2400 frames with Start "held".
local OUT = os.getenv("VBREF_OUT") or [[C:\Users\Public\vbpad.txt]]
local f = io.open(OUT, "w")

local frame = 0
event.onframeend(function()
  frame = frame + 1
  if frame == 5 then
    f:write("-- joypad.get() keys --\n")
    for k, v in pairs(joypad.get()) do f:write(tostring(k), " = ", tostring(v), "\n") end
    f:write("-- getimmediate() keys --\n")
    local ok, im = pcall(joypad.getimmediate)
    if ok and im then
      for k, v in pairs(im) do f:write(tostring(k), " = ", tostring(v), "\n") end
    else
      f:write("getimmediate unavailable\n")
    end
  end
  -- try both forms and report what reads back
  if frame > 10 then
    joypad.set({ Start = true })
    joypad.set({ Start = true }, 1)
    joypad.set({ ["P1 Start"] = true })
  end
  if frame == 30 then
    f:write("-- after set, joypad.get() --\n")
    for k, v in pairs(joypad.get()) do f:write(tostring(k), " = ", tostring(v), "\n") end
    f:close()
    client.exit()
  end
end)
