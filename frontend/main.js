const { app, BrowserWindow, ipcMain } = require("electron");
const path = require("path");
const fs = require("fs");
const { spawn, exec } = require("child_process");

// Only use electron-reload in development
if (process.env.NODE_ENV === "development") {
  require("electron-reload")(__dirname, {
    electron: path.join(__dirname, "node_modules", ".bin", "electron"),
    hardResetMethod: "exit",
  });
}

let mainWindow;
let activeChildProcesses = []; // Track active child processes

// Function to find the backend executable
function getBackendPath() {
  // Define possible paths for the backend executable
  const possiblePaths = [
    path.join(__dirname, "..", "backend", "Release", "winhider.exe"),
    path.join(app.getAppPath(), "..", "backend", "Release", "winhider.exe"),
    path.join(app.getAppPath(), "..", "backend", "Release", "winhider.exe"),
  ];

  // Find the first valid path
  for (const p of possiblePaths) {
    if (fs.existsSync(p)) {
      return p;
    }
  }

  // If no valid path is found, return the expected path anyway
  // The error handling will be done in the renderer
  return path.join(__dirname, "..", "backend", "Release", "winhider.exe");
}

// Kill all tracked child processes
function terminateAllChildProcesses() {
  console.log(`Terminating ${activeChildProcesses.length} child processes...`);

  // On Windows, we need to kill all processes in our process group
  if (process.platform === "win32") {
    // Force kill all processes with our parent PID
    const pid = process.pid;
    exec(`taskkill /F /T /PID ${pid}`, (error, stdout, stderr) => {
      if (error) {
        console.error(`Error terminating processes: ${error.message}`);
        return;
      }
      console.log(`Process cleanup stdout: ${stdout}`);
      console.log(`Process cleanup stderr: ${stderr}`);
    });
  } else {
    // For non-Windows platforms
    activeChildProcesses.forEach((process) => {
      try {
        process.kill();
      } catch (err) {
        console.error(`Failed to kill process: ${err}`);
      }
    });
  }

  activeChildProcesses = [];
}

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 450,
    height: 480,
    webPreferences: {
      nodeIntegration: true,
      contextIsolation: false,
    },
    frame: false,
    transparent: true,
    resizable: false,
    backgroundColor: "#00000000", // Transparent background
  });

  mainWindow.loadFile("public/index.html");

  // Handle window dragging
  ipcMain.on("window-drag", (event, { mouseX, mouseY }) => {
    if (typeof mouseX === "number" && typeof mouseY === "number") {
      const position = mainWindow.getPosition();
      const x = position[0] + mouseX;
      const y = position[1] + mouseY;
      mainWindow.setPosition(x, y);
    }
  });

  ipcMain.on("window-close", () => {
    terminateAllChildProcesses();
    mainWindow.close();
  });

  ipcMain.on("window-minimize", () => {
    mainWindow.minimize();
  });

  // Track child processes created from the renderer
  ipcMain.on("register-child-process", (event, pid) => {
    if (pid && !activeChildProcesses.includes(pid)) {
      activeChildProcesses.push(pid);
      console.log(`Registered child process with PID: ${pid}`);
    }
  });

  // Add handler for getting backend path
  ipcMain.on("get-backend-path", (event) => {
    // Send the path to the backend executable
    event.returnValue = getBackendPath();
  });

  // Open the DevTools
  //mainWindow.webContents.openDevTools();

  // Handle window close event
  mainWindow.on("closed", () => {
    terminateAllChildProcesses();
    mainWindow = null;
  });
}

app.whenReady().then(() => {
  createWindow();

  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on("window-all-closed", () => {
  terminateAllChildProcesses();
  if (process.platform !== "darwin") app.quit();
});

// Ensure cleanup before app exits
app.on("before-quit", () => {
  terminateAllChildProcesses();
});
