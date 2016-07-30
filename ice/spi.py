import myhdl
from myhdl import *

"""
tx_bit - 
"""
def spi_master(tx_bit, tx_valid, tx_byte, tx_clk, tx_rst):
    index = Signal(intbv(0, min=0, max=8))
    st = enum('IDLE', 'START', 'DATA')
    state = Signal(st.IDLE)

    @always_seq(tx_clk.negedge, reset=tx_rst)
    def fsm():
        if state == st.IDLE:
            tx_bit.next = 1
            if tx_valid: # a pulse
                state.next = st.DATA
                index.next = 6
                tx_bit.next = tx_byte[7]
        elif state == st.DATA:
            tx_bit.next = tx_byte[index]
            if index == 0:
                state.next = st.IDLE
            else:
                index.next = index - 1

    return fsm


# case 0: cs_low(); break;
# case 1: spi_queue_write(EWCRU); break;
# case 2: spi_queue_write(addy); break;
# case 3: enc424j600_push16LE( value ); break;
# case 4: cs_high(); state = 0; return;


def tb(spi_master):

    tx_bit = Signal(bool(1))
    tx_valid = Signal(bool(0))
    tx_byte = Signal(intbv(0)[8:])
    tx_clk = Signal(bool(1))
    # tx_rst = Signal(bool(1))
    tx_rst = ResetSignal(1, active=0, async=True)

    uart_tx_inst = spi_master(tx_bit, tx_valid, tx_byte, tx_clk, tx_rst)

    # toVerilog(uart_tx, tx_bit, tx_valid, tx_byte, tx_clk, tx_rst)

    @always(delay(10))
    def clk_gen():
        tx_clk.next = (not tx_valid) or (not tx_clk)

    @instance
    def stimulus():
        tx_rst.next = 1
        yield delay(100)
        tx_rst.next = 0
        yield delay(100)
        tx_rst.next = 1
        yield delay(100)
        for v in (0x00, 0xff, 0x55, 0xaa):
            tx_byte.next = v
            tx_valid.next = 1
            yield tx_clk.negedge
            for i in range(0, 8):
                yield tx_clk.negedge
            tx_valid.next = 0
            yield delay(100)
        raise StopSimulation

    return clk_gen, stimulus, uart_tx_inst


sim = Simulation(traceSignals(tb, spi_master))

sim.run()
