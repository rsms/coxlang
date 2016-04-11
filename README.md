# Co

Programming language inspired by [Go](https://golang.org/) that compiles to [WASM](https://webassembly.github.io/).

> This is a very early project.
> If you happen to stumble upon this, you should probably move along somewhere else.


## Building

Initial configuration:
```sh
CXX=clang python configure.py --debug
```

To build and run:
```sh
ninja && lldb -bo r build/bin/cox misc/in0.co
```

## MIT license

3rd party software that has been intergrated into this project's source:

- UTF8CPP: License can be found in src/utf8/LICENSE

Unless otherwise stated, this software and associated documentation files are subject to the following license:

Copyright (c) 2016 Rasmus Andersson <http://rsms.me/>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
