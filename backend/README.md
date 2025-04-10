# WindowHider

A Windows utility that prevents windows from being captured by screen recording tools. It uses a combination of direct window manipulation and DLL injection to achieve this.

## Features

- Hide windows from screen capture tools
- Show previously hidden windows
- List all visible windows
- Automatic process name detection for UWP apps
- System process protection (prevents hiding critical Windows processes)
- Detailed debug logging

## Requirements

- Windows 10 or later
- Visual Studio 2022 with C++ development tools
- CMake 3.20 or later
- Administrator privileges (for DLL injection)

## Building

1. Clone the repository
2. go to build directory and run `cmake ..`
3. run `cmake --build .`
4. run `winhider.exe`

## Usage

The tool provides several commands:

- `list`: Show all visible windows
- `hide -i <index>`: Hide a window by its index
- `show -i <index>`: Show a previously hidden window
- `show-all`: Show all hidden windows
- `hidden`: List currently hidden windows

Example:

```
winhider.exe list
winhider.exe hide -i 2
winhider.exe show -i 1
```

## How It Works

1. The tool first attempts to hide windows using `SetWindowDisplayAffinity`
2. If that fails, it injects a DLL (`payload.dll`) into the target process
3. The DLL sets the window display affinity to prevent capture
4. Hidden windows are tracked in memory
5. Windows can be restored using the same methods

## Security Notes

- System processes (like SystemSettings.exe) cannot be hidden for security reasons
- The tool includes safeguards to prevent hiding critical Windows components

## License

MIT License - See LICENSE file for details
