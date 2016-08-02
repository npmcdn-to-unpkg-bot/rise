import os
import myhdl
from myhdl import *

SPEED = 12000000

"""
tx_bit - spi_tx
tx_valid - signal to start transmission
tx_byte - byte
tx_clk - spi clock
tx_rst - reset seq
"""
def spi_master(tx_valid, tx_byte, tx_clk, tx_rst, spi_clk, spi_bit, spi_ready, spi_rx, spi_rx_value):
    index = Signal(intbv(0, min=0, max=8))
    read_index = Signal(intbv(0, min=0, max=8))
    st = enum('IDLE', 'DATA', 'FINISH', 'FINISH2')
    state = Signal(st.IDLE)
    internal_clk = Signal(bool(0))

    divider = 4
    i = Signal(intbv(0, min=0, max=divider))

    @always_comb
    def spi_clk_gen():
        if state == st.IDLE or state == st.FINISH2:
            spi_clk.next = bool(0)
        else:
            spi_clk.next = internal_clk

    @always_seq(internal_clk.posedge, reset=tx_rst)
    def spi_read():
        if state == st.DATA:
            spi_rx_value.next[read_index] = spi_rx

    @always_seq(tx_clk.negedge, reset=tx_rst)
    def clk_gen():
        if i == divider - 1:
            internal_clk.next = not internal_clk
            i.next = 0
        else:
            i.next = i + 1

    @always_seq(internal_clk.negedge, reset=tx_rst)
    def fsm():
        if state == st.IDLE:
            if tx_valid: # a pulse
                state.next = st.DATA
                index.next = 6
                read_index.next = 7
                spi_bit.next = tx_byte[7]
                spi_ready.next = bool(0)
            else:
                spi_bit.next = 0
        elif state == st.DATA:
            spi_bit.next = tx_byte[index]
            if index == 0:
                state.next = st.FINISH
                read_index.next = 0
            else:
                index.next = index - 1
                read_index.next = read_index - 1
        elif state == st.FINISH:
            state.next = st.FINISH2
        elif state == st.FINISH2:
            spi_ready.next = bool(1)
            state.next = st.IDLE

    return fsm, clk_gen, spi_clk_gen, spi_read

# FIFO for SPI and FIFO for return chars

# case 0: cs_low(); break;
# case 1: spi_queue_write(EWCRU); break;
# case 2: spi_queue_write(addy); break;
# case 3: enc424j600_push16LE( value ); break;
# case 4: cs_high(); state = 0; return;

ERCRU = 0x20    # Read control register unbanked
EWCRU = 0x22    # Write control register unbanked
EEUDASTL = (0x16 | 0x00)

def chatterblock(clk, tx_rst, LED1, LED2, LED3, MOSI, MISO, CS, SCLK):
    tx_clk = clk

    # PMOD1 = CS
    # PMOD2 = MOSI
    # PMOD3 = MISO
    # PMOD4 = SCLK

    # spi_bit = Signal(bool(1))
    spi_bit = MOSI # MOSI
    # spi_clk = Signal(bool(0))
    spi_rx = MISO
    spi_clk = SCLK

    tx_valid = Signal(bool(0))
    tx_byte = Signal(intbv(0)[8:])

    # spi_rx
    spi_rx_value = Signal(intbv(0)[8:])
    # tx_rst = Signal(bool(1))
    spi_ready = Signal(bool(1))

    index = Signal(intbv(0, min=0, max=5))
    index2 = Signal(intbv(0, min=0, max=256))
    st = enum('ACTIVE', 'TRANSMIT', 'IDLE')
    state = Signal(st.ACTIVE)

    active_enum = enum('INIT_SEND', 'INIT_RECV')
    active_state = Signal(active_enum.INIT_SEND)

    spi = spi_master(tx_valid, tx_byte, tx_clk, tx_rst, spi_clk, spi_bit, spi_ready, spi_rx, spi_rx_value)

    output = [0x00, 0xff, 0x55, 0xaa]

    @always_seq(tx_clk.negedge, reset=tx_rst)
    def fsm():
        if state == st.ACTIVE:
            LED1.next = bool(1)

            if active_state == active_enum.INIT_SEND:
                if index == 0:
                    CS.next = bool(0)
                    tx_byte.next = EWCRU
                elif index == 1:
                    tx_byte.next = EEUDASTL
                elif index == 2:
                    tx_byte.next = 0x34
                elif index == 3:
                    tx_byte.next = 0x12
                    index2.next = 0
                elif index == 4:
                    CS.next = bool(1)
                    if index2 == 256-1:
                        active_state.next = active_enum.INIT_RECV
                        index.next = 0
                    else:
                        index2.next = index2 + 1

            elif active_state == active_enum.INIT_RECV:
                if index == 0:
                    CS.next = bool(0)
                    tx_byte.next = ERCRU
                elif index == 1:
                    tx_byte.next = EEUDASTL
                elif index == 2:
                    tx_byte.next = 0x00
                    # pass
                elif index == 3:
                    if spi_rx_value == 0x34:
                        LED2.next = bool(1)
                    tx_byte.next = 0x00
                elif index == 4:
                    if spi_rx_value == 0x12:
                        LED3.next = bool(1)
                    state.next = st.IDLE
                    CS.next = bool(1)

            if index != 4:
                tx_valid.next = 1
                index.next = index + 1
                state.next = st.TRANSMIT
        elif state == st.TRANSMIT:
            if spi_ready and not tx_valid:
                state.next = st.ACTIVE
            elif not spi_ready:
                tx_valid.next = 0

    return fsm, spi

def rise_ice(clk, LED1, LED2, LED3, LED4, LED5, PMOD1, PMOD2, PMOD3, PMOD4, PMOD7):
    global_ready = ResetSignal(0, active=0, async=True)

    index = Signal(intbv(0, min=0, max=12000000))

    @always(clk.posedge)
    def fn_ready():
        LED5.next = 0

        if index.next == 12000000-1:
            LED4.next = 0
            global_ready.next = 1
        else:
            LED4.next = 1
            index.next = index + 1

    cb = chatterblock(clk, global_ready, LED1, LED2, LED3,
        CS=PMOD1,
        MOSI=PMOD2,
        MISO=PMOD3,
        SCLK=PMOD4)

    return cb, fn_ready

def generate():
    clk = Signal(bool(1))
    D1 = Signal(bool(0))
    D2 = Signal(bool(0))
    D3 = Signal(bool(0))
    D4 = Signal(bool(0))
    D5 = Signal(bool(0))
    PMOD1 = Signal(bool(0))
    PMOD2 = Signal(bool(0))
    PMOD3 = Signal(bool(1))
    PMOD4 = Signal(bool(1))
    PMOD7 = Signal(bool(1))
    toVerilog(rise_ice, clk, D1, D2, D3, D4, D5, PMOD1, PMOD2, PMOD3, PMOD4, PMOD7)

def tb():
    clk = Signal(bool(1))
    D1 = ResetSignal(0, active=0, async=True)
    D2 = Signal(bool(1))
    D3 = Signal(bool(1))
    D4 = Signal(bool(1))
    D5 = Signal(bool(1))
    PMOD1 = Signal(bool(1))
    PMOD2 = Signal(bool(1))
    PMOD3 = Signal(bool(1))
    PMOD4 = Signal(bool(1))
    PMOD7 = Signal(bool(1))

    uart_tx_inst = rise_ice(clk, D1, D2, D3, D4, D5, PMOD1, PMOD2, PMOD3, PMOD4, PMOD7)

    # toVerilog(chatterblock, tx_clk, tx_rst)

    @always(delay(1))
    def clk_gen():
        clk.next = not clk

    @instance
    def stimulus():
        yield delay(6000)
        raise StopSimulation

    return clk_gen, stimulus, uart_tx_inst

try:
    os.remove('tb.vcd')
except:
    pass
sim = Simulation(traceSignals(tb))
sim.run()

generate()
output = open('rise_ice.v', 'r').read()
open('rise_ice.v', 'w').write(output.replace('reg global_ready;', 'reg global_ready = 0;'))
