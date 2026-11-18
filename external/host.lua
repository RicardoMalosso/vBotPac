-- Loaded in the game_bot module scope of the separate UI process.
local snapshotPath = nil
externalSnapshot = {}
local function updateSnapshot()
  if not snapshotPath then return end
  local ok, data = pcall(function()
    return json.decode(g_resources.readFileContents(snapshotPath))
  end)
  externalSnapshot = ok and data or {error = "Snapshot unavailable"}
end

function openExternalProfile()
  updateSnapshot()
  g_window.setMinimumSize({width = 400, height = 400})
  g_window.resize({width = 420, height = 580})
  local root = g_ui.getRootWidget()
  for _, child in ipairs(root:getChildren()) do child:hide() end
  botWindow:setParent(root)
  botWindow:breakAnchors()
  botWindow:fill("parent")
  botWindow:show()
  botWindow:raise()
  local panel = contentsPanel
  panel.config:clearOptions()
  panel.config:addOption("vBotPac")
  panel.enableButton:setEnabled(snapshotPath~=nil)
  panel.enableButton:setOn(snapshotPath~=nil)
  panel.editConfig:setEnabled(false)
  panel.botTabs:setOn(true)
  local errors = {}
  local function message(kind, text)
    g_logger.info("[vBotPac] " .. kind .. ": " .. text)
    if kind == "error" then table.insert(errors, text) end
  end
  local ok, result = pcall(executeBot, "vBotPac", {}, panel.botTabs, message,
    function() end, function() end, {})
  panel.statusLabel:setOn(true)
  if not ok or #errors > 0 then
    local failure = tostring(ok and errors[1] or result)
    panel.statusLabel:setText("Startup failed: " .. failure)
    g_logger.error("[vBotPac] startup failed: " .. failure)
    return
  end
  local selected = panel.botTabs:getCurrentTab()
  for _, tab in ipairs(panel.botTabs:getTabs()) do
    panel.botTabs:selectTab(tab)
    assert(panel.botTabs:getCurrentTab() == tab, "Could not select " .. tab:getText())
    g_logger.info("[vBotPac] tab " .. tab:getText())
  end
  panel.botTabs:selectTab(selected)
  if snapshotPath then
    local function refresh()
      updateSnapshot()
      local state = externalSnapshot
      if state.error or not state.position then
        panel.statusLabel:setText("Broker disconnected: " .. tostring(state.error))
        return
      end
      local pos = state.position
      panel.statusLabel:setText(string.format("%s - PID %d - (%d, %d, %d)",
        state.name, state.pid, pos.x, pos.y, pos.z))
      scheduleEvent(refresh, 1000)
    end
    refresh()
  else
    panel.statusLabel:setText("Profile loaded - Offline")
  end
  if snapshotPath and externalRemote then
    local function run()
      if externalRemote.failed or externalSnapshot.error then panel.enableButton:setOn(false);return end
      if panel.enableButton:isOn() then
        local ok,err=pcall(function()externalRemote.poll(result.callbacks);result.script()end)
        if not ok then panel.enableButton:setOn(false);panel.statusLabel:setText(tostring(err));g_logger.error(tostring(err));return end
      end
      scheduleEvent(run,50)
    end
    scheduleEvent(run,50)
  end
  g_logger.info("[vBotPac] profile loaded; tabs=" .. #panel.botTabs:getTabs())
end
