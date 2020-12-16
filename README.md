# Rhoban Shell for mbed

This package provides a shell that can be used with mbed and USB Serial emulator.

## Setting up

Add the following to your `platformio.ini`:

```
lib_deps =
    https://github.com/rhoban/mbed-shell.git
```

## Initialize the shell

You can simply include:

```c
#include "shell.h"
```

And then initialize it in your `main()`:

```c
shell_init_usb();
```

The shell will then run on a background task with lowest priority.

## Using commands

You can declare commands:

```c
SHELL_COMMAND(hello, "Say hello")
{
    if (argc == 0) {
        shell_println("Hello!");
    } else {
        shell_print("Hello ");
        shell_print(argv[0]);
        shell_println("!");
    }
}
```

## Using parameters

You can declare parameters with following macros:

```c
SHELL_PARAMETER_INT(i, "An int", 5);
SHELL_PARAMETER_FLOAT(f, "An float", 12.5);
SHELL_PARAMETER_INT(b, "A bool", false);
```

Those parameters can be used as regular read/write variables in the code, and read or assigned in the terminal.

## Endless command

It is possible to create an endless command by checking for availability of data with `shell_available()`:

```c
SHELL_COMMAND(count, "Count")
{
    int k = 0;
    while (!shell_available()) {
        k += 1;
        shell_println(k);
        ThisThread::sleep_for(1000);
    }
}
```

This will display numbers until you press any key.