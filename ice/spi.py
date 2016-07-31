import myhdl
from myhdl import *

"""
tx_bit - spi_tx
tx_valid - signal to start transmission
tx_byte - byte
tx_clk - spi clock
tx_rst - reset seq
"""
def spi_master(tx_valid, tx_byte, tx_clk, tx_rst, spi_clk, spi_bit, spi_ready):
    index = Signal(intbv(0, min=0, max=8))
    st = enum('IDLE', 'DATA', 'FINISH', 'FINISH2')
    state = Signal(st.IDLE)
    internal_clk = Signal(bool(0))

    divider = 5
    i = Signal(intbv(0, min=0, max=divider))

    @always_comb
    def spi_clk_gen():
        if state == st.IDLE or state == st.FINISH2:
            spi_clk.next = bool(0)
        else:
            spi_clk.next = internal_clk

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
                spi_bit.next = tx_byte[7]
                spi_ready.next = bool(0)
            else:
                spi_bit.next = 1
        elif state == st.DATA:
            spi_bit.next = tx_byte[index]
            if index == 0:
                state.next = st.FINISH
            else:
                index.next = index - 1
        elif state == st.FINISH:
            state.next = st.FINISH2
        elif state == st.FINISH2:
            spi_ready.next = bool(1)
            state.next = st.IDLE

    return fsm, clk_gen, spi_clk_gen

# FIFO for SPI and FIFO for return chars

# case 0: cs_low(); break;
# case 1: spi_queue_write(EWCRU); break;
# case 2: spi_queue_write(addy); break;
# case 3: enc424j600_push16LE( value ); break;
# case 4: cs_high(); state = 0; return;

def chatterblock(tx_clk, tx_rst):
    spi_bit = Signal(bool(1))
    tx_valid = Signal(bool(0))
    tx_byte = Signal(intbv(0)[8:])
    spi_clk = Signal(bool(0))
    # tx_rst = Signal(bool(1))
    spi_ready = Signal(bool(1))

    index = Signal(intbv(0, min=0, max=5))
    st = enum('ACTIVE', 'TRANSMIT', 'IDLE')
    state = Signal(st.ACTIVE)

    spi = spi_master(tx_valid, tx_byte, tx_clk, tx_rst, spi_clk, spi_bit, spi_ready)

    output = [0x00, 0xff, 0x55, 0xaa]

    @always_seq(tx_clk.negedge, reset=tx_rst)
    def fsm():
        if state == st.ACTIVE:
            if index == 0:
                tx_byte.next = 0x00
            elif index == 1:
                tx_byte.next = 0xff
            elif index == 2:
                tx_byte.next = 0x55
            elif index == 3:
                tx_byte.next = 0xaa

            if index < 4:
                tx_valid.next = 1
                index.next = index + 1
                state.next = st.TRANSMIT
            else:
                state.next = st.IDLE
        elif state == st.TRANSMIT:
            if spi_ready and not tx_valid:
                state.next = st.ACTIVE
            elif spi_clk:
                tx_valid.next = 0

    return fsm, spi

def generate():
    tx_clk = Signal(bool(1))
    tx_rst = ResetSignal(1, active=0, async=True)
    toVerilog(chatterblock, tx_clk, tx_rst)

def tb():
    tx_clk = Signal(bool(1))
    tx_rst = ResetSignal(1, active=0, async=True)

    uart_tx_inst = chatterblock(tx_clk, tx_rst)

    # toVerilog(chatterblock, tx_clk, tx_rst)

    @always(delay(1))
    def clk_gen():
        tx_clk.next = not tx_clk

    @instance
    def stimulus():
        tx_rst.next = 1
        yield delay(100)
        tx_rst.next = 0
        yield delay(100)
        tx_rst.next = 1
        yield delay(1200)
        # for v in (0x00, 0xff, 0x55, 0xaa):
        #     tx_byte.next = v
        #     tx_valid.next = 1
        #     yield spi_clk.posedge
        #     tx_valid.next = 0
        #     yield spi_ready.posedge
        #     # yield delay(100)
        raise StopSimulation

    return clk_gen, stimulus, uart_tx_inst


sim = Simulation(traceSignals(tb))
sim.run()

generate()
