function publish(port, mode, lock,    partial, each) {
    if (modes[port] == mode && locks[port] == lock) return
    modes[port] = mode
    locks[port] = lock
    partial = ENVIRON["ANALOG_STATE"] ".partial"
    for (each in modes) print each, modes[each], locks[each] > partial
    close(partial)
    system("mv " partial " " ENVIRON["ANALOG_STATE"])
}

{
    print
    fflush()
}
