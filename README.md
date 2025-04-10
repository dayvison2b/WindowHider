# WindowHider

A desktop application that allows you to manage window visibility on Windows systems. Built with Electron for the frontend and a custom C++ backend.

## Features

- List all running windows
- Toggle window visibility (show/hide)
- Modern, transparent UI
- Easy to use interface
- System tray integration

## Prerequisites

- Node.js (v16 or higher)
- npm or yarn
- Visual Studio (for building the backend)
- Windows OS

## Installation

1. Clone the repository:

```bash
git clone https://github.com/yourusername/WindowHider.git
cd WindowHider
```

2. Install frontend dependencies:

```bash
cd frontend
npm install
```

3. Build the backend:

- Open the `backend` folder in Visual Studio
- Build the solution in Release mode
- The output will be in `backend/Release/`

4. Run in development mode:

```bash
npm run dev
```

5. Build for production:

```bash
npm run build
```

The installer will be created in the `frontend/dist` directory.

## Project Structure

```
WindowHider/
├── frontend/           # Electron frontend
│   ├── app/           # React components
│   ├── public/        # Static assets
│   └── main.js        # Electron main process
└── backend/           # C++ backend
    ├── src/          # Source files
    └── Release/      # Compiled executables
```

## Development

- Frontend: React + Electron
- Backend: C++ with Windows API
- UI: Tailwind CSS + Radix UI

### Available Scripts

- `npm run dev`: Start development server
- `npm run build`: Build production version
- `npm run pack`: Create unpacked build

## License

MIT License - See LICENSE file for details
