import { defineConfig } from "vite";
import tailwindcss from "@tailwindcss/vite";

export default defineConfig({
  base: "./",
  plugins: [tailwindcss()],
  build: {
    sourcemap: process.env.WEBVIEW_GUI_UI_SOURCEMAPS === "1",
    assetsInlineLimit: 0,
  },
});
