from myhdl import *

def rise_ice(clk, D1, D2, D3, D4, D5):

    divider = Signal(intbv(0, 0, 1200000))
    ready = Signal(bool(0))
    rot = Signal(intbv(0)[4:0])

    divider.val[:] = 99

    @always(clk.posedge)
    def toggle_led():
        if ready:
            if divider == divider.max - 1:
                divider.next = 0
                rot.next = concat(rot[3:0], rot[3])
            else:
                divider.next = divider + 1
        else:
            ready.next = 1
            rot.next = '0001'
            divider.next = 0

    @always_comb
    def assign():
        D1.next = rot[0]
        D2.next = rot[1]
        D3.next = rot[2]
        D4.next = rot[3]
        D5.next = 1

    # Return a reference to the Blinky logic.
    return toggle_led, assign

# Define the connections to Blinky.
clk = Signal(bool(0))
D1 = Signal(bool(0))
D2 = Signal(bool(0))
D3 = Signal(bool(0))
D4 = Signal(bool(0))
D5 = Signal(bool(0))

# Output Verilog code for Blinky.
toVerilog(rise_ice, clk, D1, D2, D3, D4, D5)

output = open('rise_ice.v', 'r').read()
open('rise_ice.v', 'w').write(output.replace('reg ready;', 'reg ready = 0;'))
