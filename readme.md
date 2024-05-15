# Build

```shell
# config build.sh
PLATFORM={your_platform}

# build
./build.sh
```

# Test

```shell
cd tests/{choose_one_example}

# config makefile
CROSS_COMPILE={your_cross_compile}

# build
make

# run
./test xxx
```