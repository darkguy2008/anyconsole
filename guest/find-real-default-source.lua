SimpleEventHook {
  name = "anyconsole/find-real-default-source",
  before = { "default-nodes/find-selected-default-node",
             "default-nodes/find-best-default-node",
             "default-nodes/find-stored-default-node" },
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-default-node" },
      Constraint { "default-node.type", "=", "audio.source" },
    },
  },
  execute = function (event)
    local available = event:get_data ("available-nodes")
    local real = {}
    for _, node in ipairs (available and available:parse () or {}) do
      if node ["node.virtual"] ~= "true" and (node ["media.class"] == "Audio/Source" or node ["media.class"] == "Audio/Duplex") then
        table.insert (real, Json.Object (node))
      end
    end
    event:set_data ("available-nodes", Json.Array (real))
  end
}:register ()
