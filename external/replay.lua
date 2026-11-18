-- Run from the repository root: luajit external/replay.lua
-- Deterministic host fixture for the original CaveBot runner. No game session required.
local rows = {}
local focus
local list = {}
function list:getChildCount() return #rows end
function list:getFocusedChild() return focus end
function list:getFirstChild() return rows[1] end
function list:getChildByIndex(i) return rows[i] end
function list:getChildren() return rows end
function list:getChildIndex(child)
  for i, row in ipairs(rows) do
    if row == child then return i end
  end
  return 0
end
function list:focusChild(child) focus = child end
function list:destroyChildren()
  rows = {}
  focus = nil
end

local panel = {listPanel = {list = list}, showEditor = {}, showConfig = {}}
local tick, timer, activate
local trace = {}
local function noop() end
local env = setmetatable({
  UI = {
    Config = function() return {} end,
    createWidget = function(name)
      assert(name == "CaveBotPanel", name)
      return panel
    end
  },
  modules = {game_bot = {contentsPanel = {config = {
    getCurrentOption = function() return {text = "replay"} end
  }}}},
  now = 0,
  warn = error
}, {__index = _G})

env.macro = function(interval, callback)
  assert(not timer, "fixture supports one unnamed macro")
  tick = callback
  timer = {interval = math.max(50, interval), enabled = true}
  timer.setOn = function(value) timer.enabled = value ~= false end
  timer.setOff = function() timer.enabled = false end
  return timer
end
env.Config = {setup = function(_, _, _, callback)
  activate = callback
  callback("replay", false, nil)
  return {isOn = function() return timer.enabled end}
end}
env.CaveBot = {
  Extensions = {}, Config = {setup = noop, onConfigChange = noop},
  Recorder = {isOn = function() return false end},
  resetWalking = noop, doWalking = function() return false end,
  addAction = function(action, value)
    rows[#rows + 1] = {action = action, value = value}
    focus = focus or rows[1]
  end,
  Actions = {observe = {callback = function(value, retries)
    trace[#trace + 1] = value .. "/" .. retries
    if value == "B" and retries == 0 then return "retry" end
    return true
  end}}
}

local path = "cavebot/cavebot.lua"
local chunk
if setfenv then
  chunk = assert(loadfile(path))
  setfenv(chunk, env)
else
  chunk = assert(loadfile(path, "t", env))
end
chunk()
assert(not timer.enabled)
activate("route", true, {{"observe", "A"}, {"observe", "B"}})
for i = 1, 4 do
  env.now = i * timer.interval
  assert(timer.enabled)
  tick()
end
assert(table.concat(trace, ",") == "A/0,B/0,B/1,A/0")
print("Original CaveBot replay passed: " .. table.concat(trace, ", "))
