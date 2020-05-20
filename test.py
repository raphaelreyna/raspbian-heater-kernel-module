temp = open("heatcoil.temp","r")
status = open("heatcoil.status","r")
heat_turn_on = open("heatcoil.heat_on","w")
heat_turn_off = open("heatcoil.heat_off","w")

def get_temp():
    raw = temp.readline()
    return (float(raw)*0.25)*(9.0/5.0) + 32

def get_status():
    stat = status.read(1)
    if stat == '0':
        print("Coil is off\n")
    if stat == '1':
        print("Coil is on\n")
    return stat

def heat_on():
    heat_turn_on.write('1')
    heat_turn_on.flush()

def heat_off():
    heat_turn_off.write('0')
    heat_turn_off.flush()

def close():
    temp.close()
    heat_turn_on.close()
    heat_turn_off.close()
    status.close()
