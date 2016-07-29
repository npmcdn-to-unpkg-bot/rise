# rise

```
nc -u 169.254.0.144 4111
```

Keep sending messages until you get a response.

Then you are GOLDEN

## Instructions

```
brew tap osx-cross/avr
brew install avr-libc
```

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
