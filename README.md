# brewc

A minimal compiled language and toolchain written in C++17.

## Requirements

- A C++17 compiler (Clang 10+, GCC 9+, or MSVC 2019+)
- CMake 3.16 or newer

## Build

```sh
cmake -B build
cmake --build build
```

## Run

```sh
./build/brewc
```

## Project Structure

```
brewc/
├── include/        Public headers
├── src/            Source files
├── tests/          Unit tests
├── docs/           Language specification
└── CMakeLists.txt  Build configuration
```

## License

Released under the MIT License. See [LICENSE](LICENSE) for details.
