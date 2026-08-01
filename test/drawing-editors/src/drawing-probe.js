function drawing_host() {
  const host = document.getElementById("drawing-host");
  const slot = document.getElementById("drawing-slot");
  const state = document.getElementById("drawing-state");
  const capabilities = document.getElementById("drawing-capabilities");
  if (!host || !slot || !state || !capabilities) {
    throw new Error("drawing fixture requires drawing-slot, drawing-host, drawing-state, and drawing-capabilities");
  }
  return { host, slot, state, capabilities };
}

function set_json(element, value) {
  element.textContent = JSON.stringify(value);
}

function publish_capabilities(capabilities, library) {
  const report = {
    library,
    svgNamespace: "http://www.w3.org/2000/svg",
    createElementNS: typeof document.createElementNS === "function",
    svgElement: typeof globalThis.SVGElement !== "undefined",
    svgSvgElement: typeof globalThis.SVGSVGElement !== "undefined",
    svgGraphicsElement: typeof globalThis.SVGGraphicsElement !== "undefined",
    legacyCommandsPresent: typeof document.execCommand === "function" ||
      typeof document.queryCommandSupported === "function"
  };
  set_json(capabilities, report);
  capabilities.setAttribute("data-library", library);
}

export function install_drawing_probe({ library, create, destroy, serialize }) {
  const { host, slot, state, capabilities } = drawing_host();
  let drawing = null;

  publish_capabilities(capabilities, library);

  function publish() {
    const snapshot = drawing ? serialize(drawing) : { library, destroyed: true };
    snapshot.library = library;
    snapshot.svgCount = host.querySelectorAll("svg").length;
    snapshot.documentSvgCount = document.querySelectorAll("#drawing-host svg").length;
    snapshot.canvasCount = host.querySelectorAll("canvas").length;
    set_json(state, snapshot);
    state.setAttribute("data-svg-count", String(snapshot.svgCount));
    state.setAttribute("data-document-svg-count", String(snapshot.documentSvgCount));
    state.setAttribute("data-canvas-count", String(snapshot.canvasCount));
  }

  function create_drawing() {
    // Backbone's View.remove(), used by JointJS Paper, removes its configured
    // root element. Keep a neutral page-owned slot and reattach that root
    // before a documented destroy/recreate lifecycle starts another editor.
    if (document.getElementById("drawing-host") !== host) slot.appendChild(host);
    drawing = create(host, publish);
    publish();
    state.setAttribute("data-ready", "true");
    state.removeAttribute("data-error");
  }

  function destroy_drawing() {
    if (drawing) destroy(drawing);
    drawing = null;
    host.textContent = "";
    publish();
    state.setAttribute("data-destroyed", "true");
    state.removeAttribute("data-ready");
  }

  function recreate_drawing() {
    destroy_drawing();
    state.removeAttribute("data-destroyed");
    create_drawing();
    state.setAttribute("data-recreated", "true");
  }

  document.getElementById("destroy").addEventListener("click", () => {
    try { destroy_drawing(); } catch (error) { report_failure(error); }
  });
  document.getElementById("recreate").addEventListener("click", () => {
    try { recreate_drawing(); } catch (error) { report_failure(error); }
  });
  for (const button of document.querySelectorAll("button")) {
    const action = button.getAttribute("data-drawing-action");
    if (!action) continue;
    button.addEventListener("click", () => {
      try {
        if (!drawing || !drawing.actions || typeof drawing.actions[action] !== "function") {
          throw new Error(`drawing action is unavailable: ${action} (drawing=${!!drawing}, actions=${!!(drawing && drawing.actions)}, handler=${drawing && drawing.actions ? typeof drawing.actions[action] : "none"})`);
        }
        drawing.actions[action]();
        publish();
      } catch (error) {
        report_failure(error);
      }
    });
  }

  function report_failure(error) {
    drawing = null;
    const message = error && error.message ? error.message : String(error);
    state.setAttribute("data-error", message);
    state.removeAttribute("data-ready");
    state.textContent = JSON.stringify({ library, error: message });
  }

  try { create_drawing(); } catch (error) { report_failure(error); }
  globalThis.__drawingProbe = {
    destroy: destroy_drawing,
    recreate: recreate_drawing,
    read() { return state.textContent; }
  };
}
