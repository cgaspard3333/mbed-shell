#include "shell.h"
#include "USBSerial.h"
#include <mbed.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static mbed::FileHandle *file_handle = nullptr;
static bool disabled = false;

/**
 * Global variables shell
 */

static char shell_buffer[SHELL_BUFFER_SIZE];

static bool shell_last_ok = false;
static unsigned int shell_last_pos = 0;
static unsigned int shell_pos = 0;

static const struct shell_command *shell_commands[SHELL_MAX_COMMANDS];

static unsigned int shell_command_count = 0;

static bool shell_echo_mode = true;

/**
 * Registers a command
 */
void shell_register(const struct shell_command *command) {
  shell_commands[shell_command_count++] = command;
}

static void displayHelp(bool parameter) {
  char buffer[256];
  unsigned int i;

  if (parameter) {
    shell_print("Available parameters:");
  } else {
    shell_print("Available commands:");
  }
  shell_println();

  for (i = 0; i < shell_command_count; i++) {
    const struct shell_command *command = shell_commands[i];

    if (command->parameter != parameter) {
      continue;
    }

    int namesize = strlen(command->name);
    int descsize = strlen(command->description);
    int typesize =
        (command->parameter_type == NULL) ? 0 : strlen(command->parameter_type);

    memcpy(buffer, command->name, namesize);
    buffer[namesize++] = ':';
    buffer[namesize++] = '\r';
    buffer[namesize++] = '\n';
    buffer[namesize++] = '\t';
    memcpy(buffer + namesize, command->description, descsize);
    if (typesize) {
      buffer[namesize + descsize++] = ' ';
      buffer[namesize + descsize++] = '(';
      memcpy(buffer + namesize + descsize, command->parameter_type, typesize);
      buffer[namesize + descsize + typesize++] = ')';
    }
    buffer[namesize + descsize + typesize++] = '\r';
    buffer[namesize + descsize + typesize++] = '\n';
    shell_stream()->write(buffer, namesize + descsize + typesize);
  }
}

/**
 * Internal helping command
 */
SHELL_COMMAND(help, "Displays the help about commands") { displayHelp(false); }

void shell_params_show() {
  unsigned int i;

  for (i = 0; i < shell_command_count; i++) {
    const struct shell_command *command = shell_commands[i];

    if (command->parameter) {
      command->command(0, NULL);
    }
  }
}

/**
 * Display available parameters
 */
SHELL_COMMAND(params,
              "Displays the available parameters. Usage: params [show]") {
  if (argc && strcmp(argv[0], "show") == 0) {
    shell_params_show();
  } else {
    displayHelp(true);
  }
}

/**
 * Switch echo mode
 */
SHELL_COMMAND(echo, "Switch echo mode. Usage echo [on|off]") {
  if ((argc == 1 && !strcmp("on", argv[0]))) {
    shell_echo_mode = true;
    shell_print("Echo enabled");
    shell_println();
  }
  else if ((argc == 1 && !strcmp("off", argv[0]))) {
    shell_echo_mode = false;
    shell_print("Echo disable");
    shell_println();
  }
  else{
    shell_print("Usage : echo [on|off]");
    shell_println();
  }
}

/**
 * Write the shell prompt
 */
void shell_prompt() { shell_print(SHELL_PROMPT); }

const struct shell_command *
shell_find_command(char *command_name, unsigned int command_name_length) {
  unsigned int i;

  for (i = 0; i < shell_command_count; i++) {
    const struct shell_command *command = shell_commands[i];

    if (strlen(command->name) == command_name_length &&
        strncmp(shell_buffer, command->name, command_name_length) == 0) {
      return command;
    }
  }

  return NULL;
}

/***
 * Executes the given command with given parameters
 */
bool shell_execute(char *command_name, unsigned int command_name_length,
                   unsigned int argc, char **argv) {
  unsigned int i;
  const struct shell_command *command;

  // Try to find and execute the command
  command = shell_find_command(command_name, command_name_length);
  if (command != NULL) {
    command->command(argc, argv);
  }

  // If it fails, try to parse the command as an allocation (a=b)
  if (command == NULL) {
    for (i = 0; i < command_name_length; i++) {
      if (command_name[i] == '=') {
        command_name[i] = '\0';
        command_name_length = strlen(command_name);
        command = shell_find_command(command_name, command_name_length);

        if (command && command->parameter) {
          argv[0] = command_name + i + 1;
          argv[1] = NULL;
          argc = 1;
          command->command(argc, argv);
        } else {
          command = NULL;
        }

        if (!command) {
          shell_print("Unknown parameter: ");
          file_handle->write(command_name, command_name_length);
          shell_println();
          return false;
        }
      }
    }
  }

  // If it fails again, display the "unknown command" message
  if (command == NULL) {
    shell_print("Unknown command: ");
    file_handle->write(command_name, command_name_length);
    shell_println();
    return false;
  }

  return true;
}

/***
 * Process the receive buffer to parse the command and executes it
 */
void shell_process() {
  char *saveptr;
  unsigned int command_name_length;

  unsigned int argc = 0;
  char *argv[SHELL_MAX_ARGUMENTS + 1];

  shell_println();

  strtok_r(shell_buffer, " ", &saveptr);
  while ((argv[argc] = strtok_r(NULL, " ", &saveptr)) != NULL &&
         argc < SHELL_MAX_ARGUMENTS) {
    *(argv[argc] - 1) = '\0';
    argc++;
  }

  if (saveptr != NULL) {
    *(saveptr - 1) = ' ';
  }

  command_name_length = strlen(shell_buffer);

  if (command_name_length > 0) {
    shell_last_ok =
        shell_execute(shell_buffer, command_name_length, argc, argv);
  } else {
    shell_last_ok = false;
  }

  shell_last_pos = shell_pos;
  shell_pos = 0;
  shell_prompt();
}

USBSerial *usbSerial = nullptr;

void shell_task() {
  while (true) {
    shell_tick();
  }
}

void shell_usb_task() {
  usbSerial = new USBSerial();
  file_handle = usbSerial;
  shell_prompt();

  while (true) {
    if (usbSerial->available()) {
      shell_tick();
    } else {
      ThisThread::sleep_for(5ms);
    }
  }
}

bool shell_available() {
  if (usbSerial != nullptr) {
    return usbSerial->available() > 0;
  } else {
    return file_handle->readable();
  }
}

Thread shell_thread(osPriorityLow);

/**
 * Save the Serial object globaly
 */
void shell_init(mbed::FileHandle *file_handle_) {
  file_handle = file_handle_;
  shell_prompt();

  // Starting thread priority
  shell_thread.start(shell_task);
}

void shell_init_usb() { shell_thread.start(shell_usb_task); }

mbed::FileHandle *shell_stream() { return file_handle; }

USBSerial *shell_usb_stream() { return usbSerial; }

void shell_reset() {
  shell_pos = 0;
  shell_last_pos = 0;
  shell_buffer[0] = '\0';
  shell_last_ok = false;
  shell_prompt();
}

/**
 * Stops the shell
 */
void shell_disable() { disabled = true; }

void shell_enable() {
  shell_last_ok = false;
  disabled = false;
}

/**
 * Ticking the shell, this will cause lookup for characters
 * and eventually a call to the process function on new lines
 */
void shell_tick() {
  if (disabled || file_handle == nullptr) {
    return;
  }

  char c;
  uint8_t input;

  while ((usbSerial != nullptr && usbSerial->available()) ||
         (usbSerial == nullptr && file_handle->readable())) {
    file_handle->read(&input, 1);
    c = (char)input;
    if (c == '\0' || c == 0xff) {
      continue;
    }

    // Return key
    if (c == '\r' || c == '\n') {
      if (shell_pos == 0 && shell_last_ok) {
        // If the user pressed no keys, restore the last
        // command and run it again
        unsigned int i;
        for (i = 0; i < shell_last_pos; i++) {
          if (shell_buffer[i] == '\0') {
            shell_buffer[i] = ' ';
          }
        }
        shell_pos = shell_last_pos;
      }
      shell_buffer[shell_pos] = '\0';
      shell_process();
      // Back key
    } else if (c == '\x7f') {
      if (shell_pos > 0) {
        shell_pos--;
        shell_print("\x8 \x8");
      }
      // Special key
    } else if (c == '\x1b') {
      uint8_t dummy[2];
      file_handle->read(dummy, 2);
      // Others
    } else {
      shell_buffer[shell_pos] = c;
      if (shell_echo_mode) {
        shell_print(c);
      }

      if (shell_pos < SHELL_BUFFER_SIZE - 1) {
        shell_pos++;
      }
    }
  }
}

float shell_atof(char *str) {
  int sign = (str[0] == '-') ? -1 : 1;
  float f = atoi(str);
  char *savePtr;
  strtok_r(str, ".", &savePtr);
  char *floatPart = strtok_r(NULL, ".", &savePtr);
  if (floatPart != NULL) {
    float divide = 1;
    for (char *tmp = floatPart; *tmp != '\0'; tmp++) {
      divide *= 10;
    }
    f += sign * atoi(floatPart) / divide;
  }

  return f;
}

void shell_println() { shell_print("\r\n"); }

void shell_print_bool(bool value) {
  if (value) {
    shell_print("true");
  } else {
    shell_print("false");
  }
}

void shell_print(const char *s) { file_handle->write(s, strlen(s)); }

void shell_print(char c) { file_handle->write(&c, 1); }

void shell_print(unsigned long long n, uint8_t base) {
  unsigned char buf[CHAR_BIT * sizeof(long long)];
  unsigned long i = 0;

  if (n == 0) {
    shell_print('0');
    return;
  }

  while (n > 0) {
    buf[i++] = n % base;
    n /= base;
  }

  for (; i > 0; i--) {
    shell_print(
        (char)(buf[i - 1] < 10 ? '0' + buf[i - 1] : 'A' + buf[i - 1] - 10));
  }
}

void shell_print(long long n, uint8_t base) {
  if (n < 0) {
    shell_print('-');
    n = -n;
  }
  shell_print((unsigned long long)n, base);
}

void shell_print(int n, uint8_t base) {
  if (n < 0) {
    shell_print('-');
    n = -n;
  }
  shell_print((unsigned long long)n, base);
}

void shell_print(unsigned int n, uint8_t base) {
  shell_print((unsigned long long)n, base);
}

#define LARGE_DOUBLE_TRESHOLD (9.1e18)

void shell_print(double number, int digits) {
  // Hackish fail-fast behavior for large-magnitude doubles
  if (abs(number) >= LARGE_DOUBLE_TRESHOLD) {
    if (number < 0.0) {
      shell_print('-');
    }
    shell_print("<large double>");
    return;
  }

  // Handle negative numbers
  if (number < 0.0) {
    shell_print('-');
    number = -number;
  }

  // Simplistic rounding strategy so that e.g. print(1.999, 2)
  // prints as "2.00"
  double rounding = 0.5;
  for (uint8_t i = 0; i < digits; i++) {
    rounding /= 10.0;
  }
  number += rounding;

  // Extract the integer part of the number and print it
  long long int_part = (long long)number;
  double remainder = number - int_part;
  shell_print(int_part);

  // Print the decimal point, but only if there are digits beyond
  if (digits > 0) {
    shell_print('.');
  }

  // Extract digits from the remainder one at a time
  while (digits-- > 0) {
    remainder *= 10.0;
    int to_print = (int)remainder;
    shell_print(to_print);
    remainder -= to_print;
  }
}

void shell_print(bool b) { shell_print_bool(b); }

void shell_println(const char *s) {
  shell_print(s);
  shell_println();
}

void shell_println(char c) {
  shell_print(c);
  shell_println();
}

void shell_println(unsigned long long n, uint8_t base) {
  shell_print(n, base);
  shell_println();
}

void shell_println(long long n, uint8_t base) {
  shell_print(n, base);
  shell_println();
}

void shell_println(int n, uint8_t base) {
  shell_print(n, base);
  shell_println();
}

void shell_println(unsigned int n, uint8_t base) {
  shell_print(n, base);
  shell_println();
}

void shell_println(double d, int digits) {
  shell_print(d, digits);
  shell_println();
}

void shell_println(bool b) {
  shell_print(b);
  shell_println();
}

void shell_printf(char *format, ...) {
  std::va_list args;
  va_start(args, format);
  int str_size = vsnprintf(NULL, 0, format, args);
  va_end(args);
  char buffer[str_size + 1];
  va_start(args, format);
  vsnprintf(buffer, str_size + 1, format, args);
  va_end(args);

  file_handle->write(buffer, str_size);
}