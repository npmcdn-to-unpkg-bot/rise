# rise

```
nc -u 169.254.0.144 4111
```

Keep sending messages until you get a response.

Then you are GOLDEN

Arduino hookup:

* PMOD1: A5
* PMOD2: A3
* PMOD3: A4
* PMOD4: A2
* PMOD5: GND
* PMOD6: 3.3V
* PMOD7: (floating)

## Instructions

```
brew tap osx-cross/avr
brew install avr-libc
```

Probably `sudo kextunload -b com.apple.driver.AppleUSBFTDI

## TODO

* Start SPI module in verilog code
* Write test for it
* Verify against a Tessel
* Add pmod output pins
* Get it to talk to enc424 pmodule
* Build up code + testbenches

## Credits

<https://github.com/cfelton/rhea> for SPI driver ideas

<https://github.com/cnlohr/avrcraft.git> for original enc424 driver code

## License

MIT
