const JS_MAX_TOUCHES = 3;

class WeBIGeoHacks {

  constructor(webgpuCanvas, logWrapper, logElement) {
    this.logWrapper = logWrapper;
    this.logElement = logElement;
    this.webgpuCanvas = webgpuCanvas;

    logWrapper.querySelector('#butclearlog').onclick = () => this.clearLog();
    logWrapper.querySelector('#buthidelog').onclick = () => this.hideLog();
    logWrapper.querySelector('#buttouchemulator').onclick = () => {
      // import touch-emulator.js
      const script = document.createElement('script');
      script.src = 'touch-emulator.js';
      document.head.appendChild(script);
      script.onload = () => { TouchEmulator(); }
      logWrapper.querySelector('#buttouchemulator').style.display = 'none';
    };

    window.addEventListener('touchstart', (event) => this.handleTouchEvent(event), { passive: false });
    window.addEventListener('touchmove', (event) => this.handleTouchEvent(event), { passive: false });
    window.addEventListener('touchend', (event) => this.handleTouchEvent(event), { passive: false });
    window.addEventListener('keydown', (event) => this.handleKeydownEvent(event));
    window.addEventListener('resize', (event) => this.handleResizeEvent(event));

    webgpuCanvas.addEventListener('mousedown', (event) => this.handleMousedownEvent(event));
    // Prevent right-click context menu
    webgpuCanvas.addEventListener('contextmenu', (event) => { event.preventDefault(); event.stopPropagation(); });
    this.hideLog();
  }

  async handleTouchEvent(e) {
    if (!this.eminstance) return;
    const disabledJsTouch = { clientX: -1, clientY: -1, identifier: -1 };

    const changedJsTouches = Array(JS_MAX_TOUCHES).fill(disabledJsTouch);
    for (let i = 0; i < Math.min(e.changedTouches.length, JS_MAX_TOUCHES); i++) {
      const touch = e.changedTouches[i];
      changedJsTouches[i] = { clientX: touch.clientX, clientY: touch.clientY, identifier: touch.identifier };
    }

    const activeJsTouches = Array(JS_MAX_TOUCHES).fill(disabledJsTouch);
    for (let i = 0; i < Math.min(e.touches.length, JS_MAX_TOUCHES); i++) {
      const touch = e.touches[i];
      activeJsTouches[i] = { clientX: touch.clientX, clientY: touch.clientY, identifier: touch.identifier };
    }

    await this.eminstance.ccall("global_touch_event", null,
      Array(19).fill("number"),
      [
        ...changedJsTouches.flatMap(touch => [touch.clientX, touch.clientY, touch.identifier]),
        ...activeJsTouches.flatMap(touch => [touch.clientX, touch.clientY, touch.identifier]),
        e.type === 'touchstart' ? 0 : e.type === 'touchmove' ? 1 : e.type === "touchend" ? 2 : 3
      ],
      { async: true });
  }

  async handleKeydownEvent(event) {
    // Toggle display of the log by pressing F10
    if (event.key === 'Dead') this.toggleLog();
  }

  async handleResizeEvent(event) {
    if (!this.eminstance) return;
    await this.eminstance.ccall("global_canvas_size_changed", null, ["number", "number"], [window.innerWidth, window.innerHeight], { async: true });
  }

  async handleMousedownEvent(event) {
    const jsButtonToEmButton = { 0: 0, 1: 2, 2: 1, 3: 3, 4: 4 };
    this.logWrapper.classList.add('noselect');
    await this.eminstance.ccall("global_mouse_button_event", null,
      ["number", "number", "number", "number", "number"],
      [jsButtonToEmButton[event.button], 1, 0, event.clientX, event.clientY],
      { async: true });
    document.onmousemove = async (event) => {
      await this.eminstance.ccall("global_mouse_position_event", null,
        ["number", "number", "number"],
        [jsButtonToEmButton[event.button], event.clientX, event.clientY],
        { async: true });
    };
    document.onmouseup = async () => {
      await this.eminstance.ccall("global_mouse_button_event", null,
        ["number", "number", "number", "number", "number"],
        [jsButtonToEmButton[event.button], 0, 0, event.clientX, event.clientY],
        { async: true });
      document.onmousemove = null;
      if (this.webgpuCanvas.releaseCapture) { this.webgpuCanvas.releaseCapture(); }
      this.logWrapper.classList.remove('noselect');
    };
    if (this.webgpuCanvas.setCapture) { this.webgpuCanvas.setCapture(); }
  }

  async setEminstance(eminstance) {
    this.eminstance = eminstance;
    this.debugBuild = eminstance.hasOwnProperty("getCallStack") || eminstance.hasOwnProperty("getCallstack");

    // Set the canvas to be the same size as the screen upon initialization
    await eminstance.ccall("global_canvas_size_changed", null, ["number", "number"], [window.innerWidth, window.innerHeight], { async: true });
  }

  async checkWebGPU() {
    this.webgpuAvailable = true;
    this.webgpuTimingsAvailable = false;

    if (navigator.gpu === undefined) {
      this.webgpuAvailable = false;
      return;
    }

    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) {
      this.webgpuAvailable = false;
      return;
    }
    const device = await adapter.requestDevice({ requiredFeatures: ['timestamp-query'] });
    if (!device) {
      this.webgpuAvailable = false;
      return;
    }

    if (!device.features.has('timestamp-query'))
      return;

    const commandEncoder = device.createCommandEncoder();
    if (typeof commandEncoder.writeTimestamp === 'undefined')
      return;
    this.webgpuTimingsAvailable = true;
  }

  log(text) {
    if (this.debug) console.log(text);
    if (this.logElement) {
      let htmlText = this.prepareAnsiLogString(text + '\n');
      this.logElement.innerHTML += htmlText;
      this.logElement.scrollTop = this.logElement.scrollHeight; // focus on bottom
    }
  }

  showLog() { this.logWrapper.style.display = 'block'; }
  hideLog() { this.logWrapper.style.display = 'none'; }

  toggleLog() {
    if (this.logWrapper.style.display === 'none') this.showLog();
    else this.hideLog();
  }
  clearLog() { this.logElement.innerHTML = ''; }

  prepareAnsiLogString(text) {
    const replacementMap = {
      '&': '&amp;',
      '<': '&lt;',
      '>': '&gt;',
      '\n': '<br>',
      ' ': '&nbsp;',
      '\x1b[30m': '<font color="black">',
      '\x1b[31m': '<font color="#ed4e4c">',
      '\x1b[32m': '<font color="green">',
      '\x1b[33m': '<font color="#d2c057">',
      '\x1b[34m': '<font color="#2774f0">',
      '\x1b[35m': '<font color="magenta">',
      '\x1b[36m': '<font color="#12b5cb">',
      '\x1b[37m': '<font color="white">',
      '\x1b[0m': '</font>' // Reset code to close the font tag
    };

    for (const char in replacementMap) {
      if (replacementMap.hasOwnProperty(char)) {
        const replacement = replacementMap[char];
        text = text.split(char).join(replacement);
      }
    }

    return text;
  }

}
