-- Loaded in the game_bot module scope of the separate UI process.
function openExternalProfile()
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
  panel.enableButton:setEnabled(false)
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
  panel.statusLabel:setText("Profile loaded - Offline")
  g_logger.info("[vBotPac] profile loaded; tabs=" .. #panel.botTabs:getTabs())
end
