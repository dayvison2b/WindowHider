# WindowHider

A modern Windows application that allows you to hide windows from screen capture without requiring administrator privileges. Perfect for privacy-conscious users who want to keep certain windows hidden during screen sharing or recording.

## How It Works

WindowHider uses a clever DLL injection technique to hide windows from screen capture:

1. When you want to hide a window, WindowHider injects a small DLL into the target window's process
2. The DLL uses Windows' `SetWindowDisplayAffinity` API to mark the window as excluded from capture
3. When you want to show the window again, the DLL is ejected and the window becomes visible in captures

This approach is reliable because:

- It works without requiring administrator privileges
- It's more stable and doesn't affect window functionality
- It's harder to detect by screen capture software
- It works with most modern Windows applications

## Features

- Hide any window from screen capture
- Show hidden windows with a single command
- Interactive shell for ease of use
- Case-insensitive window title matching
- Command-line interface for scripting
- No administrator privileges required
- Works with most Windows applications

## Building

### Prerequisites

- CMake 3.10 or higher
- Visual Studio 2019 or newer

### Building from Source

```bash
cmake -B build -S .
cmake --build build --config Release
```

The executable and DLL will be in the `dist` directory.

## Project Structure

The project consists of these main components:

1. **Core Library**

   - `src/window_manager.cpp` - Window management
   - `src/injector.cpp` - DLL injection
   - `src/payload/payload.cpp` - The DLL injected into target processes

2. **Command-line Interface**
   - `src/main.cpp` - Command-line interface with interactive mode

## Command-line Usage

WindowHider has two modes:

### Interactive Mode

Simply run `winhider` without any arguments to enter interactive mode:

```
winhider
```

Then use these commands:

```
list                  - List all visible windows
hidden                - List all hidden windows
hide -i <index>       - Hide a window by index
show -i <index>       - Show a previously hidden window by index
show-all              - Show all hidden windows
help                  - Show this help information
exit                  - Exit the program
```

### Single Command Mode

```
winhider list                  - List all visible windows
winhider hidden                - List all hidden windows
winhider hide -i <index>       - Hide a window by index
winhider show -i <index>       - Show a hidden window by index
winhider show-all              - Show all hidden windows
winhider help                  - Show help information
```

## Security Considerations

- WindowHider only modifies the display affinity of windows
- No sensitive data is collected or transmitted
- The source code is open for inspection
- No internet connection required

## License

MIT License - See LICENSE file for details
