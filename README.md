![WiiStatic](icon.png)

# WiiStatic

TV static simulator for your Nintendo Wii

---

Do you have one of those fancy rectangular televisions, but miss the static noise from that old heavy piece of shit?
This homebrew application replicates the static from an old television on your Wii.

## Functionality

Running the program from the Homebrew Channel (or similar loader) will instantly bless your eyes and ears with 
this groundbreaking audiovisual experience using the default settings.

You can use the + or - buttons to increase/lower the volume, or press A to turn it off entirely. You can press B
to enable a CRT-like band that goes from top to bottom, if you're into that; this is disabled by default. Finally,
if you've decided that you've had enough of this life, you can press the Home button to return to the Homebrew Channel.

That's pretty much all this program does as of now, nothing less and nothing more.

## Downloads

See GitHub releases tab. We automatically build binaries using GitHub Actions.

## Compiling

Compiling WiiStatic requires DevkitPro, CMake and a C++20 compiler. Only standard libraries and libraries included with a standard
DevkitPro installation are used.

1. Clone the repository and change working directory into it
2. Set up a CMake build directory with `mkdir build; cd build; cmake .. -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Wii.cmake`
3. Build the program using `cmake --build .`
4. Profit, hopefully.

## License

MIT License

Copyright (c) 2026 Jacob Nilsson / Forwarder Factory

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
