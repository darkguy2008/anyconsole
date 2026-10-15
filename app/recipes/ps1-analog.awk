/^\[INFO\] \[Environ\] SET_MESSAGE: Controller [0-9]+ switched to (analog|digital) mode\.$/ {
    publish($5, $8 == "analog" ? "on" : "off", "usable")
}

/^\[INFO\] \[Environ\] SET_MESSAGE: Controller [0-9]+ is locked to (analog|digital) mode by the game\.$/ {
    publish($5, $9 == "analog" ? "on" : "off", "locked")
}
