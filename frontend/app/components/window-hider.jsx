import { useState, useRef, useEffect } from "react";
import {
  Eye,
  EyeOff,
  Minimize2,
  X,
  GripHorizontal,
  RefreshCw,
} from "lucide-react";
import { Badge } from "./ui/badge";
import { Button } from "./ui/button";
import { Card } from "./ui/card";
import { ScrollArea } from "./ui/scroll-area";

const { ipcRenderer } = window.require("electron");
const { exec } = window.require("child_process");

export default function WindowHider() {
  const [processes, setProcesses] = useState([]);
  const [loading, setLoading] = useState(false);
  const [isDragging, setIsDragging] = useState(false);
  const [lastMousePosition, setLastMousePosition] = useState({ x: 0, y: 0 });
  const [pendingToggles, setPendingToggles] = useState({}); // Track processes being toggled
  const cardRef = useRef(null);
  const refreshTimerRef = useRef(null);

  // Debounced refresh function to prevent too many rapid refreshes
  const debouncedRefresh = (delay = 300) => {
    // Clear any existing timer
    if (refreshTimerRef.current) {
      clearTimeout(refreshTimerRef.current);
    }

    // Set a new timer
    refreshTimerRef.current = setTimeout(() => {
      fetchProcesses();
    }, delay);
  };

  const fetchProcesses = () => {
    setLoading(true);

    // Get the path to the backend executable
    const backendPath = ipcRenderer.sendSync("get-backend-path");
    const command = `${backendPath} list`;

    console.log("Executing command:", command);
    const childProcess = exec(command, (error, stdout, stderr) => {
      if (error) {
        console.error(`Error executing command: ${error}`);
        setLoading(false);
        return;
      }

      console.log("Raw output from backend:", stdout);

      // Parse the output from winhider.exe list
      const processData = parseBackendOutput(stdout);
      console.log("Parsed processes:", processData);

      // All processes from 'list' are visible by default
      processData.forEach((p) => {
        p.visible = true;
        p.source = "list";
      });

      // Store processes with a temporary variable
      // We'll merge with hidden windows later
      const visibleProcesses = processData;

      // Now fetch hidden processes before updating state
      fetchHiddenWindowsAndMerge(visibleProcesses);
    });

    // Register this child process with the main process
    if (childProcess && childProcess.pid) {
      ipcRenderer.send("register-child-process", childProcess.pid);
    }
  };

  // New function to fetch hidden windows and merge with visible ones
  const fetchHiddenWindowsAndMerge = (visibleProcesses) => {
    const backendPath = ipcRenderer.sendSync("get-backend-path");
    const command = `${backendPath} hidden`;

    console.log("Executing hidden command:", command);
    const childProcess = exec(command, (error, stdout, stderr) => {
      if (error) {
        console.error(`Error executing hidden command: ${error}`);
        // Even if there's an error, we should still update the UI with visible processes
        setProcesses(visibleProcesses);
        setLoading(false);
        return;
      }

      console.log("Raw hidden output:", stdout);

      // Check if no hidden windows
      if (stdout.includes("No hidden windows found")) {
        console.log("No hidden windows found");
        // Update state with just visible processes
        setProcesses(visibleProcesses);
        setLoading(false);
        return;
      }

      // Parse hidden windows
      const hiddenData = parseBackendOutput(stdout);
      console.log("Parsed hidden processes:", hiddenData);

      // Mark all these as hidden
      hiddenData.forEach((p) => {
        p.visible = false;
        p.source = "hidden";
      });

      // Create final merged list - this is a clean approach:
      // 1. Start with all visible processes from list command
      const mergedProcesses = [...visibleProcesses];

      // 2. For each hidden process, find a matching visible one and mark it hidden, or add it
      hiddenData.forEach((hiddenProcess) => {
        const hiddenName = hiddenProcess.name.trim().toLowerCase();

        // Try to find a match in our visible processes
        const matchIndex = mergedProcesses.findIndex((p) => {
          const visibleName = p.name.trim().toLowerCase();
          return (
            visibleName.includes(hiddenName) || hiddenName.includes(visibleName)
          );
        });

        if (matchIndex !== -1) {
          // Match found - update the visibility status
          console.log(
            `Marking process '${mergedProcesses[matchIndex].name}' as hidden (matched with '${hiddenProcess.name}')`
          );
          mergedProcesses[matchIndex].visible = false;
          mergedProcesses[matchIndex].source = "both"; // Track that it's in both lists
        } else {
          // No match found - add as a new process
          console.log(`Adding new hidden process: ${hiddenProcess.name}`);
          mergedProcesses.push(hiddenProcess);
        }
      });

      console.log("Final merged processes:", mergedProcesses);
      setProcesses(mergedProcesses);
      setLoading(false);
    });

    // Register this child process with the main process
    if (childProcess && childProcess.pid) {
      ipcRenderer.send("register-child-process", childProcess.pid);
    }
  };

  const parseBackendOutput = (output) => {
    // Split output into lines
    const lines = output.trim().split("\n");
    console.log("Split lines:", lines);

    // Check if output indicates "No hidden windows found"
    if (output.includes("No hidden windows found")) {
      return []; // Return empty array for no hidden windows
    }

    // Filter out header lines like "Visible Windows:" that aren't actual windows
    const filteredLines = lines.filter((line) => {
      // Skip header lines that end with colon
      if (line.trim().endsWith(":")) return false;

      // Skip lines that are just empty strings
      if (line.trim() === "") return false;

      // Skip "No hidden windows found" line
      if (line.trim() === "No hidden windows found") return false;

      return true;
    });

    console.log("Filtered lines:", filteredLines);

    // Parse each line to extract window titles and handles
    return filteredLines.map((line, index) => {
      // Clean up the line - remove carriage returns and extra spaces
      const cleanLine = line.replace(/\r/g, "").trim();

      // First, try to match the expected format: [HWND] Window Title or indexed format: 1. Window Title
      const hwndMatch = cleanLine.match(/\[(0x[0-9A-Fa-f]+)\]\s+(.*)/);
      const indexedMatch = cleanLine.match(/^\s*(\d+)\.\s+(.*)$/);

      if (hwndMatch) {
        const hwnd = hwndMatch[1];
        const title = hwndMatch[2].trim();

        console.log(`Parsed window with HWND ${hwnd}: "${title}"`);

        return {
          id: hwnd,
          name: title,
          status: "Running",
          visible: true, // Default to true, will be overridden if needed
          displayName: formatProcessName(title),
        };
      } else if (indexedMatch) {
        // For indexed format (1. Window Name)
        const idx = indexedMatch[1];
        const title = indexedMatch[2].trim();

        console.log(`Parsed indexed window ${idx}: "${title}"`);

        // Generate a unique ID based on index and title
        const hwnd = `window-${idx}`;

        return {
          id: hwnd,
          name: title,
          status: "Running",
          visible: true, // Default to true, will be overridden if needed
          displayName: formatProcessName(title),
        };
      }

      // Fallback if parsing fails - try to clean up any numbered entries manually
      console.log(`Failed to parse line: "${cleanLine}"`);

      // Try one more pattern - sometimes format is "  1. appname.exe"
      const manualMatch = cleanLine.match(/^\s*\d+\.\s+(.+)$/);
      if (manualMatch) {
        const title = manualMatch[1].trim();
        return {
          id: `window-${index}`,
          name: title,
          status: "Running",
          visible: true, // Default to true, will be overridden if needed
          displayName: formatProcessName(title),
        };
      }

      return {
        id: `unknown-${index}`,
        name: cleanLine,
        status: "Running",
        visible: false, // Default to false for unparsed lines
        displayName: formatProcessName(cleanLine),
      };
    });
  };

  // Helper function to format process names for better display
  const formatProcessName = (name) => {
    // Clean up process name for display
    let displayName = name.trim();

    // Remove .exe extension for cleaner display
    if (displayName.toLowerCase().endsWith(".exe")) {
      displayName = displayName.substring(0, displayName.length - 4);
    }

    // Capitalize first letter
    if (displayName.length > 0) {
      displayName = displayName.charAt(0).toUpperCase() + displayName.slice(1);
    }

    return displayName;
  };

  const toggleVisibility = (id) => {
    // Find the process
    const process = processes.find((p) => p.id === id);
    if (!process) {
      console.error(`Process with ID ${id} not found`);
      return;
    }

    // Mark this process as being toggled (optimistic UI update)
    setPendingToggles((prev) => ({ ...prev, [id]: true }));

    // Apply optimistic update to the UI immediately
    setProcesses((prev) =>
      prev.map((p) =>
        p.id === id ? { ...p, visible: !p.visible, isToggling: true } : p
      )
    );

    // Get backend path
    const backendPath = ipcRenderer.sendSync("get-backend-path");

    // Clean process name for better matching
    const cleanProcessName = process.name.trim();
    console.log(`Toggling visibility for: "${cleanProcessName}"`);

    // Choose the appropriate command based on current visibility
    if (process.visible) {
      // We're hiding a visible window - need to find it in the visible list
      const listCmd = `${backendPath} list`;
      console.log("Executing refresh list command:", listCmd);
      setLoading(true);

      const childProcess = exec(listCmd, (error, stdout, stderr) => {
        if (error) {
          console.error(`Error refreshing process list: ${error}`);
          // Revert optimistic update on error
          setProcesses((prev) =>
            prev.map((p) =>
              p.id === id ? { ...p, visible: true, isToggling: false } : p
            )
          );
          setPendingToggles((prev) => {
            const newState = { ...prev };
            delete newState[id];
            return newState;
          });
          setLoading(false);
          return;
        }

        const rawProcessData = parseBackendOutput(stdout);

        // Find the index in the visible list by matching name
        let matchIdx = -1;
        for (let i = 0; i < rawProcessData.length; i++) {
          const p = rawProcessData[i];
          const pName = p.name.trim();

          // Match by name (either contains or is contained)
          if (
            pName.toLowerCase().includes(cleanProcessName.toLowerCase()) ||
            cleanProcessName.toLowerCase().includes(pName.toLowerCase())
          ) {
            matchIdx = i + 1; // Command-line indexing starts at 1
            console.log(
              `Found matching window "${pName}" at index ${matchIdx}`
            );
            break;
          }
        }

        if (matchIdx > 0) {
          // Use the hide with index command
          const hideCommand = `${backendPath} hide -i ${matchIdx}`;
          console.log("Executing hide command:", hideCommand);

          const childProcess = exec(hideCommand, (cmdErr, cmdOut) => {
            if (cmdErr) {
              console.error(`Error hiding window: ${cmdErr}`);
              // Revert optimistic update on error
              setProcesses((prev) =>
                prev.map((p) =>
                  p.id === id ? { ...p, visible: true, isToggling: false } : p
                )
              );
              setPendingToggles((prev) => {
                const newState = { ...prev };
                delete newState[id];
                return newState;
              });
              setLoading(false);
              return;
            }

            console.log("Hide command output:", cmdOut);

            // Clear pending state for this process
            setPendingToggles((prev) => {
              const newState = { ...prev };
              delete newState[id];
              return newState;
            });

            // We'll let the refresh handle UI update
            debouncedRefresh(500);
            setLoading(false);
          });

          // Register this child process with the main process
          if (childProcess && childProcess.pid) {
            ipcRenderer.send("register-child-process", childProcess.pid);
          }
        } else {
          console.error(
            `Could not find window "${cleanProcessName}" in visible list`
          );
          // Revert optimistic update if we couldn't find the window
          setProcesses((prev) =>
            prev.map((p) =>
              p.id === id ? { ...p, visible: true, isToggling: false } : p
            )
          );
          setPendingToggles((prev) => {
            const newState = { ...prev };
            delete newState[id];
            return newState;
          });
          setLoading(false);
        }
      });
    } else {
      // We're showing a hidden window - need to find it in the hidden list
      const hiddenCmd = `${backendPath} hidden`;
      console.log("Executing hidden refresh command:", hiddenCmd);
      setLoading(true);

      const childProcess = exec(hiddenCmd, (error, stdout, stderr) => {
        if (error) {
          console.error(`Error getting hidden windows: ${error}`);
          // Revert optimistic update on error
          setProcesses((prev) =>
            prev.map((p) =>
              p.id === id ? { ...p, visible: false, isToggling: false } : p
            )
          );
          setPendingToggles((prev) => {
            const newState = { ...prev };
            delete newState[id];
            return newState;
          });
          setLoading(false);
          return;
        }

        if (stdout.includes("No hidden windows found")) {
          console.error("No hidden windows found to show");
          // Revert optimistic update if no hidden windows
          setProcesses((prev) =>
            prev.map((p) =>
              p.id === id ? { ...p, visible: false, isToggling: false } : p
            )
          );
          setPendingToggles((prev) => {
            const newState = { ...prev };
            delete newState[id];
            return newState;
          });
          setLoading(false);
          return;
        }

        const hiddenProcessData = parseBackendOutput(stdout);

        // Find the index in the hidden list by matching name
        let matchIdx = -1;
        for (let i = 0; i < hiddenProcessData.length; i++) {
          const p = hiddenProcessData[i];
          const pName = p.name.trim();

          // Match by name (either contains or is contained)
          if (
            pName.toLowerCase().includes(cleanProcessName.toLowerCase()) ||
            cleanProcessName.toLowerCase().includes(pName.toLowerCase())
          ) {
            matchIdx = i + 1; // Command-line indexing starts at 1
            console.log(
              `Found matching hidden window "${pName}" at index ${matchIdx}`
            );
            break;
          }
        }

        if (matchIdx > 0) {
          // Use the show with index command
          const showCommand = `${backendPath} show -i ${matchIdx}`;
          console.log("Executing show command:", showCommand);

          const childProcess = exec(showCommand, (cmdErr, cmdOut) => {
            if (cmdErr) {
              console.error(`Error showing window: ${cmdErr}`);
              // Revert optimistic update on error
              setProcesses((prev) =>
                prev.map((p) =>
                  p.id === id ? { ...p, visible: false, isToggling: false } : p
                )
              );
              setPendingToggles((prev) => {
                const newState = { ...prev };
                delete newState[id];
                return newState;
              });
              setLoading(false);
              return;
            }

            console.log("Show command output:", cmdOut);

            // Clear pending state for this process
            setPendingToggles((prev) => {
              const newState = { ...prev };
              delete newState[id];
              return newState;
            });

            // We'll let the refresh handle UI update
            debouncedRefresh(500);
            setLoading(false);
          });

          // Register this child process with the main process
          if (childProcess && childProcess.pid) {
            ipcRenderer.send("register-child-process", childProcess.pid);
          }
        } else {
          console.error(
            `Could not find window "${cleanProcessName}" in hidden list`
          );
          // Revert optimistic update if we couldn't find the window
          setProcesses((prev) =>
            prev.map((p) =>
              p.id === id ? { ...p, visible: false, isToggling: false } : p
            )
          );
          setPendingToggles((prev) => {
            const newState = { ...prev };
            delete newState[id];
            return newState;
          });
          setLoading(false);
        }
      });
    }
  };

  const toggleAllVisibility = (visible) => {
    // Get backend path
    const backendPath = ipcRenderer.sendSync("get-backend-path");

    console.log(`Toggling all windows to ${visible ? "visible" : "hidden"}`);
    setLoading(true);

    if (visible) {
      // Show all windows - use the correct command name from CLI
      const showAllCommand = `${backendPath} show-all`;
      console.log("Executing show all command:", showAllCommand);

      const childProcess = exec(showAllCommand, (error, stdout, stderr) => {
        if (error) {
          console.error(`Error showing all windows: ${error}`);
          setLoading(false);
          return;
        }

        console.log("Show all command output:", stdout);

        // Refresh the window list to update UI
        debouncedRefresh(500);
        setLoading(false);
      });

      // Register this child process with the main process
      if (childProcess && childProcess.pid) {
        ipcRenderer.send("register-child-process", childProcess.pid);
      }
    } else {
      // Hide all windows - need to use individual commands
      console.log("Hiding all windows individually");

      // Get current list of visible windows first
      const listCmd = `${backendPath} list`;
      const childProcess = exec(listCmd, (listError, listStdout) => {
        if (listError) {
          console.error(
            `Error getting window list for hiding all: ${listError}`
          );
          setLoading(false);
          return;
        }

        // Parse the window list and filter out headers
        const processData = parseBackendOutput(listStdout);

        console.log(`Found ${processData.length} visible windows to hide`);

        // No windows to hide
        if (processData.length === 0) {
          console.log("No visible windows to hide");
          setLoading(false);
          return;
        }

        // Track completed operations to know when we're done
        let completedOps = 0;
        let successfulOps = 0;

        // Hide each window by index
        processData.forEach((process, idx) => {
          const hideCommand = `${backendPath} hide -i ${idx + 1}`;
          console.log(
            `Hiding window individually: ${process.name} with command: ${hideCommand}`
          );

          const childProcess = exec(hideCommand, (err, out) => {
            completedOps++;
            if (err) {
              console.error(`Error hiding window ${process.name}: ${err}`);
            } else {
              console.log(`Successfully hid window ${process.name}: ${out}`);
              successfulOps++;
            }

            // Check if all operations are complete
            if (completedOps === processData.length) {
              console.log(
                `Hide all complete: ${successfulOps}/${processData.length} windows hidden`
              );

              // Refresh the window list to update UI
              debouncedRefresh(500);
              setLoading(false);
            }
          });

          // Register this child process with the main process
          if (childProcess && childProcess.pid) {
            ipcRenderer.send("register-child-process", childProcess.pid);
          }
        });
      });
    }
  };

  // Handle window dragging
  const handleMouseDown = (e) => {
    setIsDragging(true);
    setLastMousePosition({ x: e.clientX, y: e.clientY });
  };

  const handleMouseMove = (e) => {
    if (isDragging) {
      const mouseX = e.clientX - lastMousePosition.x;
      const mouseY = e.clientY - lastMousePosition.y;

      ipcRenderer.send("window-drag", { mouseX, mouseY });
      setLastMousePosition({ x: e.clientX, y: e.clientY });
    }
  };

  const handleMouseUp = () => {
    setIsDragging(false);
  };

  const handleClose = () => {
    ipcRenderer.send("window-close");
  };

  const handleMinimize = () => {
    ipcRenderer.send("window-minimize");
  };

  const handleRefresh = () => {
    // Use our refresh function
    debouncedRefresh(0); // Immediate refresh when manually requested
  };

  useEffect(() => {
    // Fetch data when component mounts
    fetchProcesses();

    // Cleanup timers on unmount
    return () => {
      if (refreshTimerRef.current) {
        clearTimeout(refreshTimerRef.current);
      }
    };
  }, []);

  useEffect(() => {
    if (isDragging) {
      document.addEventListener("mousemove", handleMouseMove);
      document.addEventListener("mouseup", handleMouseUp);
    } else {
      document.removeEventListener("mousemove", handleMouseMove);
      document.removeEventListener("mouseup", handleMouseUp);
    }

    return () => {
      document.removeEventListener("mousemove", handleMouseMove);
      document.removeEventListener("mouseup", handleMouseUp);
    };
  }, [isDragging]);

  return (
    <div className="w-full h-screen flex flex-col">
      <Card
        ref={cardRef}
        className="w-full max-w-md min-w-[450px] h-[480px] border border-gray-800 bg-black/50 backdrop-blur-md flex flex-col"
      >
        <div
          className="flex items-center justify-between cursor-grab active:cursor-grabbing border-b border-gray-800 py-3 px-5"
          onMouseDown={handleMouseDown}
        >
          <div className="flex items-center gap-2.5">
            <GripHorizontal className="h-4 w-4 text-gray-500" />
            <div>
              <h2 className="text-white font-medium text-base">Window Hider</h2>
              <p className="text-gray-400 text-xs">Manage visible processes</p>
            </div>
          </div>
          <div className="flex gap-3">
            <Button
              variant="ghost"
              size="icon"
              className="h-6 w-6 text-gray-400 hover:text-white p-0"
              onClick={handleRefresh}
            >
              <RefreshCw
                className={`h-3.5 w-3.5 ${loading ? "animate-spin" : ""}`}
              />
              <span className="sr-only">Refresh</span>
            </Button>
            <Button
              variant="ghost"
              size="icon"
              className="h-6 w-6 text-gray-400 hover:text-white p-0"
              onClick={handleMinimize}
            >
              <Minimize2 className="h-3.5 w-3.5" />
              <span className="sr-only">Minimize</span>
            </Button>
            <Button
              variant="ghost"
              size="icon"
              className="h-6 w-6 text-gray-400 hover:text-white p-0"
              onClick={handleClose}
            >
              <X className="h-3.5 w-3.5" />
              <span className="sr-only">Close</span>
            </Button>
          </div>
        </div>

        <div className="flex items-center justify-between border-b border-gray-800 px-5 py-4">
          <div className="flex gap-3">
            <Button
              variant="outline"
              size="sm"
              className="h-7 px-3 border-gray-700 bg-gray-900/50 text-xs text-gray-300 hover:bg-gray-800 rounded-md"
              onClick={() => toggleAllVisibility(true)}
              disabled={loading}
            >
              <Eye className="mr-1.5 h-3.5 w-3.5" />
              Show All
            </Button>
            <Button
              variant="outline"
              size="sm"
              className="h-7 px-3 border-gray-700 bg-gray-900/50 text-xs text-gray-300 hover:bg-gray-800 rounded-md"
              onClick={() => toggleAllVisibility(false)}
              disabled={loading}
            >
              <EyeOff className="mr-1.5 h-3.5 w-3.5" />
              Hide All
            </Button>
          </div>
          <Badge
            variant="outline"
            className="border-gray-700 text-gray-300 px-2.5 py-1 text-xs"
          >
            {processes.filter((p) => p.visible).length} Visible
          </Badge>
        </div>

        <div className="flex-1 relative overflow-hidden">
          <ScrollArea className="h-full w-full pr-1" type="always">
            {loading && processes.length === 0 ? (
              <div className="flex items-center justify-center p-6 h-full">
                <p className="text-gray-400">Loading windows...</p>
              </div>
            ) : processes.length === 0 ? (
              <div className="flex items-center justify-center p-6 h-full">
                <p className="text-gray-400">No windows found</p>
              </div>
            ) : (
              processes.map((process) => (
                <div
                  key={process.id}
                  className={`flex items-center justify-between px-5 py-3 border-b border-gray-800/50 last:border-b-0 transition-all duration-200 ${
                    pendingToggles[process.id]
                      ? "bg-gray-800/30"
                      : process.visible
                      ? "bg-gradient-to-r from-gray-800/40 to-gray-900/20 hover:from-gray-800/60 hover:to-gray-900/40"
                      : "hover:bg-gray-800/20"
                  }`}
                >
                  <div className="flex items-center gap-3">
                    <div
                      className={`relative h-2 w-2 rounded-full transition-all duration-300 ${
                        pendingToggles[process.id]
                          ? "bg-blue-500 animate-pulse"
                          : process.visible
                          ? "bg-green-500"
                          : "bg-gray-600"
                      }`}
                    >
                      {process.visible && !pendingToggles[process.id] && (
                        <span className="absolute inset-0 rounded-full bg-green-500 opacity-40 animate-ping-slow"></span>
                      )}
                    </div>
                    <div className="flex flex-col">
                      <span className="font-medium text-white">
                        {process.displayName.length > 25
                          ? `${process.displayName.substring(0, 25)}...`
                          : process.displayName}
                      </span>
                    </div>
                  </div>
                  <div className="flex items-center justify-center space-x-4">
                    <Badge
                      variant={process.visible ? "default" : "secondary"}
                      className={
                        process.visible
                          ? "bg-transparent text-green-400 font-normal px-3 py-0.5 text-xs border border-green-500/20"
                          : "bg-transparent text-gray-500 font-normal px-3 py-0.5 text-xs border border-gray-700/30"
                      }
                    >
                      {process.visible ? "Visible" : "Hidden"}
                    </Badge>

                    {/* Toggle button with loading indicator */}
                    <button
                      onClick={() => toggleVisibility(process.id)}
                      className={`relative inline-flex h-6 w-11 items-center rounded-full border-2 border-transparent p-0.5 transition-colors duration-300 ${
                        pendingToggles[process.id]
                          ? "bg-blue-500" // Blue while toggling
                          : process.visible
                          ? "bg-green-500"
                          : "bg-gray-700"
                      }`}
                      disabled={pendingToggles[process.id] || loading}
                    >
                      <span className="sr-only">Toggle visibility</span>
                      <span
                        className={`inline-block h-4 w-4 transform rounded-full bg-white shadow transition-transform duration-300 ${
                          process.visible ? "translate-x-5" : "translate-x-0"
                        } ${
                          pendingToggles[process.id]
                            ? "opacity-70"
                            : "opacity-100"
                        }`}
                      />
                      {pendingToggles[process.id] && (
                        <span className="absolute inset-0 flex items-center justify-center">
                          <svg
                            className="h-3 w-3 animate-spin text-white"
                            xmlns="http://www.w3.org/2000/svg"
                            fill="none"
                            viewBox="0 0 24 24"
                          >
                            <circle
                              className="opacity-25"
                              cx="12"
                              cy="12"
                              r="10"
                              stroke="currentColor"
                              strokeWidth="4"
                            ></circle>
                            <path
                              className="opacity-75"
                              fill="currentColor"
                              d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"
                            ></path>
                          </svg>
                        </span>
                      )}
                    </button>
                  </div>
                </div>
              ))
            )}
          </ScrollArea>
        </div>
      </Card>
    </div>
  );
}
