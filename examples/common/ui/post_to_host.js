export function postToHost(data, targetOrigin = "*") {
  if (window.parent === window) {
    window.postMessage(data, targetOrigin);
    return;
  }

  window.parent.postMessage(data, targetOrigin);
}
