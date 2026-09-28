cutils = require ("common-utils")

SimpleEventHook {
  name = "device/find-detected-profile",
  after = "device/find-preferred-profile",
  before = "device/find-best-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-profile" },
    },
  },
  execute = function (event)
    if event:get_data ("selected-profile") then
      return
    end

    local device = event:get_subject ()
    local detected = {}
    for r in device:iterate_params ("EnumRoute") do
      local route = cutils.parseParam (r, "EnumRoute")
      if route and route.direction == "Output" and route.available == "yes" and route.profiles then
        for _, index in ipairs (route.profiles) do
          detected [tonumber (index)] = true
        end
      end
    end

    local best = nil
    for p in device:iterate_params ("EnumProfile") do
      local profile = cutils.parseParam (p, "EnumProfile")
      if profile and detected [tonumber (profile.index)] and profile.available == "yes" and
          (best == nil or profile.priority > best.priority) then
        best = profile
      end
    end

    if best then
      event:set_data ("selected-profile", best)
    end
  end
}:register ()

SimpleEventHook {
  name = "device/select-profile-on-enumroute-changed",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "device-params-changed" },
      Constraint { "event.subject.param-id", "=", "EnumRoute" },
      Constraint { "device.api", "=", "alsa" },
    },
  },
  execute = function (event)
    event:get_source ():call ("push-event", "select-profile", event:get_subject (), nil)
  end
}:register ()
