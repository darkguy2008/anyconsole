480 as $screen_width | 272 as $screen_height
| if .scale then {ppsspp_internal_resolution: "\($screen_width * .scale)x\($screen_height * .scale)"} else {} end
