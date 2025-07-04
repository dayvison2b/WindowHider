const path = require("path");

module.exports = (env, argv) => ({
  mode: argv.mode || "development",
  entry: "./app/page.jsx",
  output: {
    filename: "bundle.js",
    path: path.resolve(__dirname, "public/js"),
  },
  module: {
    rules: [
      {
        test: /\.(js|jsx|ts|tsx)$/,
        exclude: /node_modules/,
        use: {
          loader: "babel-loader",
        },
      },
    ],
  },
  resolve: {
    extensions: [".js", ".jsx", ".ts", ".tsx"],
    alias: {
      "@": path.resolve(__dirname, "app"),
    },
  },
  devtool: argv.mode === "production" ? false : "source-map",
});
