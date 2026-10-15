/^Pad: DS2 Config Finished - P[0-9]+\/S1 - AL: (On|Off) - AB: (Locked|Usable) - VS: .* - VL: .* - RB: .*$/ {
    publish(substr($6, 2, index($6, "/") - 2), tolower($9), tolower($12))
}

/^OSD \[PadAnalogButtonChange[0-9]+\]: Analog light is now (on|off) for port [0-9]+ \/ slot 1$/ {
    publish($10, $7, "usable")
}
