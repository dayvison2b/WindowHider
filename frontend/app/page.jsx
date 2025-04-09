import React from "react";
import ReactDOM from "react-dom/client";
import WindowHider from "./components/window-hider";

function Home() {
  return <WindowHider />;
}

// Render the app
const root = ReactDOM.createRoot(document.getElementById("root"));
root.render(<Home />);
