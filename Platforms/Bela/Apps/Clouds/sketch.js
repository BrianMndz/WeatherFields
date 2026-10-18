// The Clouds panel for Bela, served at http://bela.local/gui while the app
// runs. It talks to the board's generic GuiLink (Platforms/Bela/Host/
// GuiLink.h): it sends parameter changes by slot and ID, and shows the values
// the board publishes, so changes the engine makes itself (a quality change
// releasing freeze) appear here too.
//
// Ported from CloudsEngine's panel (Apps/Bela/sketch.js, e7738d9), whose
// drawing ports the JUCE editor's to the canvas's 2D context, in the same
// reference coordinates: a panel of 416 x 440 units, scaled to fit the
// window.
//
// Button B and the LEDs follow the JUCE editor, which follows the module's
// Ui (eurorack/clouds/ui.cc): pressing B shows which parameter BLEND sets,
// pressing it again selects the next; turning BLEND shows all four values;
// holding B for a second shows the audio quality, holding it for four the
// playback mode, where presses select the next; otherwise the LEDs show the
// level. Button C has no function, as in the JUCE editor.

// Positions from the JUCE editor's layout namespace.
const layout = {
    width: 416,
    height: 440,
    freeze: { x: 56, y: 82.5, r: 20 },
    ledX: [141.5, 187.5, 231.5, 276],
    ledY: 82.5,
    ledRadius: 11,
    qualityIconY: 61,
    blendIconY: 107.5,
    iconRadius: 7.5,
    blendButton: { x: 327.5, y: 82.5, r: 11 },
    saveButton: { x: 370, y: 82.5, r: 11 },
    position: { x: 92.5, y: 175, r: 41 },
    size: { x: 210, y: 175, r: 41 },
    pitch: { x: 327.5, y: 175, r: 41 },
    largeLabelY: 231,
    inputGain: { x: 67.5, y: 292.5, r: 29 },
    density: { x: 161, y: 292.5, r: 29 },
    texture: { x: 256, y: 292.5, r: 29 },
    blend: { x: 350, y: 291, r: 29 },
    smallLabelY: 339,
    dividerY: 361.5,
    bottomRowY: 392,
    trigger: { x: 370, y: 392, r: 11 },
    screwY: [14, 421],
};

// Colours from PanelControls.h, as [r, g, b].
const colours = {
    background: rgb(0x2a2a2d),
    panel: rgb(0xe4e3e4),
    panelShade: rgb(0xd2d1d3),
    ornament: rgb(0xc9c6ca),
    ink: rgb(0x1a1a1c),
    red: rgb(0xa52f57),
    redCap: rgb(0x7d2441),
    teal: rgb(0x3a817e),
    tealCap: rgb(0x2f6e6b),
    whiteCap: rgb(0xeeeeee),
    skirt: rgb(0x1f2022),
    ledGreen: rgb(0xb5dc5e),
    white: [255, 255, 255],
    black: [0, 0, 0],
};

// The JUCE editor's font on macOS is Lucida Grande. A JUCE font's height is
// its ascent plus descent: 0.967 + 0.211 of the CSS font size for Lucida
// Grande. JUCE centres that box, so the baseline sits half the difference
// between ascent and descent below the centre.
const kFontFamily = '"Lucida Grande", "Lucida Sans Unicode", "Lucida Sans", Verdana, sans-serif';
const kFontAscent = 0.967;
const kFontDescent = 0.211;
const kFontSizePerHeight = 1 / (kFontAscent + kFontDescent);

// JUCE's rotary sliders: from 1.2 pi to 2.8 pi, clockwise from twelve
// o'clock; a 250-pixel drag covers the range.
const kRotaryStart = Math.PI * 1.2;
const kRotaryEnd = Math.PI * 2.8;
const kDragPixels = 250;

// The JUCE window's default scale. Its controls are laid out in whole pixels
// and drawn one pixel inside their bounds, 0.8 units at this scale.
const kInset = 1 / 1.25;


// Clouds in its solo rig: slot 0, and the parameter IDs of
// wf::clouds::Engine (Engines/Clouds/include/wf/clouds/Engine.h), which are
// stable forever.
const kSlot = 0;
const kParam = {
    position: 0, size: 1, pitch: 2, density: 3, texture: 4,
    dryWet: 5, stereoSpread: 6, feedback: 7, reverb: 8, inputGain: 9,
    freeze: 10, trigger: 11, gate: 12, quality: 13, playbackMode: 14,
};

// The knobs' parameters. Ranges and defaults match the engine's table, and
// the decimals the value bubble shows match the JUCE editor (2 unless given).
const controls = {
    inputGain: { min: -18, max: 6, def: 0, suffix: ' dB', decimals: 1 },
    position: { min: 0, max: 1, def: 0.5 },
    size: { min: 0, max: 1, def: 0.5 },
    pitch: { min: -24, max: 24, def: 0, suffix: ' st' },
    density: { min: 0, max: 1, def: 0.75 },
    texture: { min: 0, max: 1, def: 0.5 },
    dryWet: { min: 0, max: 1, def: 0.5 },
    stereoSpread: { min: 0, max: 1, def: 0 },
    feedback: { min: 0, max: 1, def: 0 },
    reverb: { min: 0, max: 1, def: 0 },
};
for (const key of Object.keys(controls)) {
    Object.assign(controls[key], {
        id: kParam[key],
        value: controls[key].def,
        dirty: false,   // changed here, not yet sent
        editedAt: -Infinity,
        board: null,    // the latest value the board reported
    });
}

// The parameters BLEND can set, in the module's order, which is also the
// order of the LEDs and of the icons under them. Which one is selected lives
// on this page only.
const blendControls = [controls.dryWet, controls.stereoSpread, controls.feedback, controls.reverb];
let blendIndex = 0;

// The audio quality (stereo 16-bit, mono 16-bit, stereo mu-law, mono mu-law)
// and the playback mode (granular, stretch, looping delay, spectral), in the
// module's order. A new quality, or entering or leaving spectral, erases the
// recording and releases freeze, in the engine.
const kNumQualities = 4;
const kNumPlaybackModes = 4;
const kSpectral = 3;
const quality = { index: 0, editedAt: -Infinity, board: null };
const playbackMode = { index: 0, editedAt: -Infinity, board: null };

// The knobs on the panel, with the JUCE editor's caps. BLEND sets whichever
// parameter button B selected; like the JUCE editor, and unlike the module's
// knob, which cannot move, it shows that parameter's value.
const knobs = [
    { circle: layout.position, cap: colours.redCap, large: true, control: controls.position },
    { circle: layout.size, cap: colours.tealCap, large: true, control: controls.size },
    { circle: layout.pitch, cap: colours.whiteCap, large: true, control: controls.pitch },
    { circle: layout.inputGain, cap: colours.redCap, control: controls.inputGain },
    { circle: layout.density, cap: colours.redCap, control: controls.density },
    { circle: layout.texture, cap: colours.tealCap, control: controls.texture },
    { circle: layout.blend, cap: colours.whiteCap, blend: true },
];
const blendKnob = knobs.find((knob) => knob.blend);

function controlOf(knob) {
    return knob.blend ? blendControls[blendIndex] : knob.control;
}

const freezeButton = { circle: layout.freeze, on: false, editedAt: -Infinity, board: null };
const blendButton = { circle: layout.blendButton };
const saveButton = { circle: layout.saveButton };
const trigButton = { circle: layout.trigger };
const buttons = [freezeButton, blendButton, saveButton, trigButton];

// JUCE's default double-click time: a second press on the same knob within
// it returns the knob to its default.
const kDoubleClickMilliseconds = 400;

// The module's Ui: a press of B shorter than a second is a short press, one
// held for four reaches the playback mode display; the LEDs return to the
// meter three seconds after the last press of B or turn of BLEND.
const kLongPressMilliseconds = 1000;
const kVeryLongPressMilliseconds = 4000;
const kLedTimeoutMilliseconds = 3000;

// The board sends updates about 30 times a second; without one for this
// long, it has stopped, and the meter goes dark.
const kMeterTimeoutMilliseconds = 250;

// A value this page changed shows the page's own for this long, since updates
// sent before the board applied the change still carry the old value. The
// board's latest report is kept and shown once the hold ends: the board sends
// a value only when it changes, so a correction that arrives during the hold
// is never sent again.
const kEchoHoldMilliseconds = 500;

let canvasElement = null;
let scaleFactor = 1;
let originX = 0;
let originY = 0;
let panelLayer = null;    // everything that does not change, drawn on resize
let pointer = null;       // { x, y } in canvas pixels: the mouse, or the tracked touch
let activeTouch = null;   // identifier of the touch driving the panel
let dragged = null;       // { knob, startX, startY, startValue }
let pressed = null;       // the button held down
let pressTime = 0;        // when it was pressed
// B's long and very long presses, as JUCE's PanelPushButton: each fires
// once, one and four seconds into the press; dragging off B cancels them,
// and a press that fired the long one is not a short press.
let longPressArmed = false;
let longPressFired = false;
let veryLongPressFired = false;
let hovered = null;       // the knob or button under the mouse; touches do not hover
let lastPress = { control: null, time: 0 };   // see pressAt()
let trigDown = false;
let helloId = 0;          // the id of the latest wf-hello
let synced = false;       // the board's state has arrived
let inputLevel = 0;       // the latest levels from the board, linear peaks
let outputLevel = 0;
let levelTime = 0;        // when they arrived

// What the LEDs show, as in the JUCE editor: 'meter' (the level),
// 'blending' (the selected parameter, green), 'blendMeter' (all four
// values), 'quality' (the selected quality, red) or 'mode' (the selected
// playback mode, pulsing orange).
let ledMode = 'meter';
let lastLedActivity = 0;

//==============================================================================
// p5.js entry points

function setup() {
    canvasElement = createCanvas(windowWidth, windowHeight).elt;
    frameRate(30);
    layoutPanel();

    Bela.control.registerCallback('cloudsPanel', onBoardMessage);
    // If the socket is not open yet, this is dropped; the board's
    // "connection" message then asks again.
    sendHello();

    // p5.js does not handle touchcancel. A cancelled touch, losing focus or
    // leaving the page ends the press without a click and releases TRIG.
    window.addEventListener('touchcancel', (event) => {
        if (findTouch(event.changedTouches))
            cancelPointer();
    });
    window.addEventListener('blur', cancelPointer);
    window.addEventListener('pagehide', cancelPointer);
}

function windowResized() {
    resizeCanvas(windowWidth, windowHeight);
    layoutPanel();
}

function draw() {
    sendChanges();
    reconcile();

    const ctx = drawingContext;
    ctx.drawImage(panelLayer, 0, 0, width, height);

    ctx.save();
    ctx.translate(originX, originY);
    ctx.scale(scaleFactor, scaleFactor);
    for (const knob of knobs)
        drawKnob(ctx, knob.circle, fractionOf(controlOf(knob)), knob.cap, knob.large);
    drawFreezeButton(ctx);
    for (const button of [blendButton, saveButton, trigButton])
        drawPushButton(ctx, button);
    updateLedMode();
    ledLights().forEach(([red, green], i) =>
        drawLed(ctx, { x: layout.ledX[i], y: layout.ledY, r: layout.ledRadius }, red, green));
    drawFooter(ctx);
    drawBubble(ctx);
    ctx.restore();
}

//==============================================================================
// The board

function sendParam(id, value) {
    Bela.control.send({ event: 'wf-param', slot: kSlot, id: id, value: value });
}

// Each hello has a random id, which the board echoes in its reply. The board
// sends every reply to every page, so the id picks out the one answering
// this page's latest hello. Changes waiting for the next frame go first.
function sendHello() {
    sendChanges();
    helloId = 1 + Math.floor(Math.random() * 1e9);
    Bela.control.send({ event: 'wf-hello', id: helloId });
}

function recentlyEdited(holder) {
    return performance.now() - holder.editedAt < kEchoHoldMilliseconds;
}

// Records one [slot, id, value] from the board as the latest report for its
// control; reconcile() shows it. TRIG's gate is not shown: the page's button
// decides it (see onBoardMessage()).
function applyBoardValue(entry) {
    if (!Array.isArray(entry) || entry.length !== 3 || entry[0] !== kSlot || typeof entry[2] !== 'number')
        return;
    const [, id, value] = entry;
    const control = Object.values(controls).find((c) => c.id === id);
    if (control)
        control.board = value;
    else if (id === kParam.freeze)
        freezeButton.board = value;
    else if (id === kParam.quality)
        quality.board = value;
    else if (id === kParam.playbackMode)
        playbackMode.board = value;
}

// Every frame: each control shows the board's latest report unless this page
// changed it within kEchoHoldMilliseconds, has a change not yet sent, or is
// dragging its knob.
function reconcile() {
    for (const control of Object.values(controls)) {
        const held = dragged && controlOf(dragged.knob) === control;
        if (control.board !== null && !held && !control.dirty && !recentlyEdited(control))
            control.value = control.board;
    }
    if (freezeButton.board !== null && !recentlyEdited(freezeButton))
        freezeButton.on = freezeButton.board !== 0;
    if (quality.board !== null && !recentlyEdited(quality))
        quality.index = Math.round(quality.board);
    if (playbackMode.board !== null && !recentlyEdited(playbackMode))
        playbackMode.index = Math.round(playbackMode.board);
}

function onBoardMessage(data) {
    if (data.event === 'connection') {
        // A new connection, also after the board restarts or the network
        // drops. A TRIG release made while disconnected was lost, so the
        // gate is sent again as the button is now; then the board's state is
        // asked for. (With several pages open, this also releases a gate
        // another page holds.)
        sendParam(kParam.gate, trigDown ? 1 : 0);
        sendHello();
    } else if (data.event === 'wf-state' && data.id === helloId && Array.isArray(data.params)) {
        data.params.forEach(applyBoardValue);
        synced = true;
    } else if (data.event === 'wf-update') {
        if (typeof data.input === 'number' && typeof data.output === 'number') {
            inputLevel = data.input;
            outputLevel = data.output;
            levelTime = performance.now();
        }
        if (Array.isArray(data.params))
            data.params.forEach(applyBoardValue);
    }
}

// Sends at most one message per changed knob per frame.
function sendChanges() {
    for (const control of Object.values(controls)) {
        if (control.dirty) {
            sendParam(control.id, control.value);
            control.dirty = false;
        }
    }
}

function setKnob(knob, value) {
    const control = controlOf(knob);
    value = Math.min(Math.max(value, control.min), control.max);
    if (value !== control.value) {
        control.value = value;
        control.dirty = true;
        control.editedAt = performance.now();
        // Turning BLEND shows all four values (the module's
        // UI_MODE_BLEND_METER), but leaves the quality and mode displays
        // alone.
        if (knob === blendKnob && ledMode !== 'quality' && ledMode !== 'mode')
            showLeds('blendMeter');
    }
}

function toggleFreeze() {
    freezeButton.on = !freezeButton.on;
    freezeButton.editedAt = performance.now();
    sendParam(kParam.freeze, freezeButton.on ? 1 : 0);
}

// Button B's short press, as in the module's Ui::OnSwitchReleased(): the
// first press shows which parameter BLEND sets, presses while it is shown
// select the next, and BLEND then shows the new parameter's value. In the
// quality and mode displays, presses select the next one.
function onBlendButtonShortPress() {
    if (ledMode === 'quality') {
        selectNextQuality();
        showLeds('quality');
    } else if (ledMode === 'mode') {
        selectNextPlaybackMode();
        showLeds('mode');
    } else {
        if (ledMode === 'blending') {
            blendIndex = (blendIndex + 1) % blendControls.length;
            forgetDoubleClick();
        }
        showLeds('blending');
    }
}

// B's long press switches between the quality display and the meter; held
// on to four seconds, it reaches the mode display.
function onBlendButtonLongPress() {
    showLeds(ledMode === 'quality' ? 'meter' : 'quality');
}

// The engine releases freeze with the change; the page shows it at once.
function releaseFreezeHere() {
    freezeButton.on = false;
    freezeButton.editedAt = performance.now();
}

function selectNextQuality() {
    quality.index = (quality.index + 1) % kNumQualities;
    quality.editedAt = performance.now();
    releaseFreezeHere();
    sendParam(kParam.quality, quality.index);
}

function selectNextPlaybackMode() {
    const previous = playbackMode.index;
    playbackMode.index = (previous + 1) % kNumPlaybackModes;
    playbackMode.editedAt = performance.now();
    if (previous === kSpectral || playbackMode.index === kSpectral)
        releaseFreezeHere();
    sendParam(kParam.playbackMode, playbackMode.index);
}

// Fires B's long and very long presses once their time has passed, if still
// armed.
function checkLongPress() {
    if (pressed !== blendButton || !longPressArmed)
        return;
    const held = performance.now() - pressTime;
    if (!longPressFired && held >= kLongPressMilliseconds) {
        longPressFired = true;
        onBlendButtonLongPress();
    }
    if (longPressFired && !veryLongPressFired && held >= kVeryLongPressMilliseconds) {
        veryLongPressFired = true;
        showLeds('mode');
    }
}

// After the parameter BLEND sets changes, the next click on BLEND is a first
// click: it is on a different parameter from the one clicked before.
function forgetDoubleClick() {
    lastPress = { control: null, time: 0 };
}

function showLeds(mode) {
    ledMode = mode;
    lastLedActivity = performance.now();
}

// Back to the meter three seconds after the last activity, but not while B
// is held, as in the JUCE editor. Called every frame, which also times B's
// long presses.
function updateLedMode() {
    checkLongPress();
    if (ledMode !== 'meter' && pressed !== blendButton
        && performance.now() - lastLedActivity > kLedTimeoutMilliseconds)
        ledMode = 'meter';
}

// [red, green] for each LED, from 0 to 1.
function ledLights() {
    switch (ledMode) {
        case 'blending':
            return blendControls.map((control, i) => [0, i === blendIndex ? 1 : 0]);
        case 'blendMeter':
            return blendControls.map((control) => intensityLight(control.value));
        case 'quality':
            return blendControls.map((control, i) => [i === quality.index ? 1 : 0, 0]);
        case 'mode': {
            const now = performance.now();
            return blendControls.map((control, i) => i === playbackMode.index ? modeLight(now) : [0, 0]);
        }
        default: {
            // As the module meters: the input after IN GAIN, or the output
            // while frozen.
            const fresh = performance.now() - levelTime < kMeterTimeoutMilliseconds;
            const level = freezeButton.on ? outputLevel
                                          : inputLevel * Math.pow(10, controls.inputGain.value / 20);
            return levelLights(fresh ? level : 0);
        }
    }
}

// The module's input meter (Leds::PaintBar with lut_db in the firmware), as
// the JUCE editor's showLevel(): each LED covers 6 dB and fades in across it.
// LEDs 1 and 2 light green from -24 to -12 dBFS, LED 3 yellow up to -6 dBFS,
// LED 4 red up to full scale.
function levelLights(peak) {
    // Steps of 6.02 dB (one octave of amplitude) above -24 dBFS.
    const level = peak > 0 ? Math.log2(peak) + 4 : -1;
    return blendControls.map((control, i) => {
        const fill = Math.min(Math.max(level - i, 0), 1);
        return [i >= 2 ? fill : 0, i <= 2 ? fill : 0];
    });
}

// The module's colour scale for a value from 0 to 1 (Leds::set_intensity in
// eurorack/clouds/drivers/leds.h): off, green, yellow, then red.
function intensityLight(value) {
    if (value < 0.5)
        return [0, value * 2];
    if (value < 0.75)
        return [(value - 0.5) * 4, 1];
    return [1, 1 - (value - 0.75) * 4];
}

// The playback mode display's pulse (Ui::PaintLeds in the firmware): a
// triangle wave over 512 ms, squared, from 0 to 254. The selected LED moves
// between yellow (fade 0) and orange (fade 254).
function modeLight(milliseconds) {
    let fade = (Math.floor(milliseconds) >> 1) & 255;
    fade = fade <= 127 ? (fade << 1) & 255 : (255 - (fade << 1)) & 255;
    const squared = (fade * fade) >> 8;
    return [(128 + (squared >> 1)) / 255, (255 - (squared >> 1)) / 255];
}

// TRIG: a press fires the trigger, and the button held is the TRIG input's
// gate, which the spectral mode uses.
function setTrig(down) {
    if (trigDown !== down) {
        trigDown = down;
        if (down)
            Bela.control.send({ event: 'wf-trigger', slot: kSlot, id: kParam.trigger });
        sendParam(kParam.gate, down ? 1 : 0);
    }
}

//==============================================================================
// Pointer handling. The mouse and one touch at a time drive the same press,
// move, release and cancel functions. Touches have their own handlers because
// p5.js 1.0.0 passes touchstart to mousePressed() only in browsers whose user
// agent mentions Safari, which leaves out Firefox for Android.

function layoutPanel() {
    scaleFactor = Math.min(width / layout.width, height / layout.height);
    originX = (width - layout.width * scaleFactor) / 2;
    originY = (height - layout.height * scaleFactor) / 2;
    drawPanelLayer();
}

function isUnderPointer(circle) {
    if (!pointer)
        return false;
    const x = (pointer.x - originX) / scaleFactor;
    const y = (pointer.y - originY) / scaleFactor;
    return Math.hypot(x - circle.x, y - circle.y) <= circle.r;
}

function controlUnderPointer() {
    return knobs.find((knob) => isUnderPointer(knob.circle))
        || buttons.find((button) => isUnderPointer(button.circle))
        || null;
}

// A press on a knob within kDoubleClickMilliseconds of a press on the same
// knob is a double-click. As in JUCE, any other press in between, on a
// button or on the empty panel, starts the count again, and so does a change
// of the parameter BLEND sets (forgetDoubleClick()).
function pressAt(x, y) {
    pointer = { x: x, y: y };
    const control = controlUnderPointer();
    const now = performance.now();
    const isDoubleClick = knobs.includes(control) && lastPress.control === control
                          && now - lastPress.time < kDoubleClickMilliseconds;
    lastPress = isDoubleClick ? { control: null, time: 0 } : { control: control, time: now };

    if (knobs.includes(control)) {
        if (isDoubleClick)
            setKnob(control, controlOf(control).def);
        dragged = { knob: control, startX: x, startY: y, startValue: controlOf(control).value };
    } else if (control) {
        pressed = control;
        pressTime = performance.now();
        longPressArmed = control === blendButton;
        longPressFired = false;
        veryLongPressFired = false;
        // Like the JUCE editor's TRIG, which fires on mouse-down.
        if (control === trigButton)
            setTrig(true);
    }
}

function moveTo(x, y) {
    pointer = { x: x, y: y };
    if (pressed === blendButton && !isUnderPointer(blendButton.circle))
        longPressArmed = false;
    if (dragged) {
        const control = controlOf(dragged.knob);
        // Right or up increases, as with RotaryHorizontalVerticalDrag.
        const pixels = (x - dragged.startX) - (y - dragged.startY);
        setKnob(dragged.knob, dragged.startValue + pixels / kDragPixels * (control.max - control.min));
    }
}

function releaseAt(x, y) {
    pointer = { x: x, y: y };
    // FREEZE toggles on release over it, like a JUCE button's click. A
    // release of B is a long press if a second has passed and the frame loop
    // has not fired it yet; otherwise, over B, a short press unless the long
    // press fired.
    if (pressed === freezeButton && isUnderPointer(freezeButton.circle))
        toggleFreeze();
    if (pressed === blendButton) {
        checkLongPress();
        if (!longPressFired && isUnderPointer(blendButton.circle))
            onBlendButtonShortPress();
    }
    cancelPointer();
}

// Ends a press without a click.
function cancelPointer() {
    setTrig(false);
    dragged = null;
    pressed = null;
    activeTouch = null;
}

// Returning false prevents the browser's default: no scrolling, text
// selection, or mouse events emulated after a touch.
function mousePressed() {
    if (activeTouch === null)
        pressAt(mouseX, mouseY);
    hovered = controlUnderPointer();
    return false;
}

function mouseMoved() {
    pointer = { x: mouseX, y: mouseY };
    hovered = controlUnderPointer();
}

function mouseDragged() {
    if (activeTouch === null)
        moveTo(mouseX, mouseY);
    return false;
}

function mouseReleased() {
    if (activeTouch === null) {
        releaseAt(mouseX, mouseY);
        hovered = controlUnderPointer();
    }
    return false;
}

// The touch with the tracked identifier in `touches`, if there.
function findTouch(touches) {
    for (const touch of touches) {
        if (touch.identifier === activeTouch)
            return touch;
    }
    return null;
}

function canvasPoint(touch) {
    const bounds = canvasElement.getBoundingClientRect();
    return [touch.clientX - bounds.left, touch.clientY - bounds.top];
}

// Further fingers are ignored while one is down.
function touchStarted(event) {
    if (activeTouch === null && !dragged && !pressed && event.changedTouches) {
        const touch = event.changedTouches[0];
        activeTouch = touch.identifier;
        hovered = null;
        pressAt(...canvasPoint(touch));
    }
    return false;
}

function touchMoved(event) {
    const touch = event.changedTouches && findTouch(event.changedTouches);
    if (touch)
        moveTo(...canvasPoint(touch));
    return false;
}

function touchEnded(event) {
    const touch = event.changedTouches && findTouch(event.changedTouches);
    if (touch)
        releaseAt(...canvasPoint(touch));
    return false;
}

//==============================================================================
// Colours: JUCE's Colour operations on [r, g, b] arrays.

function rgb(value) {
    return [(value >> 16) & 255, (value >> 8) & 255, value & 255];
}

function css(colour, alpha = 1) {
    return `rgba(${colour[0]}, ${colour[1]}, ${colour[2]}, ${alpha})`;
}

function brighter(colour, amount) {
    const k = 1 / (1 + amount);
    return colour.map((v) => Math.floor(255 - k * (255 - v)));
}

function darker(colour, amount) {
    const k = 1 / (1 + amount);
    return colour.map((v) => Math.floor(k * v));
}

function interpolated(from, to, t) {
    return from.map((v, i) => Math.round(v + (to[i] - v) * t));
}

// Keeps hue and brightness (HSB), as Colour::withSaturation() does.
function withSaturation(colour, saturation) {
    const [r, g, b] = colour.map((v) => v / 255);
    const max = Math.max(r, g, b);
    const min = Math.min(r, g, b);
    let hue = 0;
    if (max > min) {
        if (max === r) hue = ((g - b) / (max - min) + 6) % 6;
        else if (max === g) hue = (b - r) / (max - min) + 2;
        else hue = (r - g) / (max - min) + 4;
    }
    const chroma = max * saturation;
    const x = chroma * (1 - Math.abs((hue % 2) - 1));
    const sector = Math.floor(hue) % 6;
    const parts = [[chroma, x, 0], [x, chroma, 0], [0, chroma, x], [0, x, chroma], [x, 0, chroma], [chroma, 0, x]][sector];
    return parts.map((v) => Math.round((v + max - chroma) * 255));
}

//==============================================================================
// Canvas helpers in JUCE's terms. Angles are clockwise from twelve o'clock.

function pointOnCircle(x, y, radius, angle) {
    return [x + radius * Math.sin(angle), y - radius * Math.cos(angle)];
}

function circlePath(ctx, x, y, radius) {
    ctx.beginPath();
    ctx.arc(x, y, radius, 0, Math.PI * 2);
}

function fillCircle(ctx, x, y, radius, style) {
    circlePath(ctx, x, y, radius);
    ctx.fillStyle = style;
    ctx.fill();
}

function strokeCircle(ctx, x, y, radius, lineWidth, style) {
    circlePath(ctx, x, y, radius);
    ctx.lineWidth = lineWidth;
    ctx.strokeStyle = style;
    ctx.stroke();
}

function drawLine(ctx, x1, y1, x2, y2, lineWidth, style) {
    ctx.beginPath();
    ctx.moveTo(x1, y1);
    ctx.lineTo(x2, y2);
    ctx.lineWidth = lineWidth;
    ctx.lineCap = 'butt';
    ctx.strokeStyle = style;
    ctx.stroke();
}

function linearGradient(ctx, from, x1, y1, to, x2, y2) {
    const gradient = ctx.createLinearGradient(x1, y1, x2, y2);
    gradient.addColorStop(0, css(from));
    gradient.addColorStop(1, css(to));
    return gradient;
}

// A JUCE radial gradient: `inner` at the centre, `outer` at (x2, y2).
function radialGradient(ctx, inner, x, y, outer, x2, y2) {
    const gradient = ctx.createRadialGradient(x, y, 0, x, y, Math.hypot(x2 - x, y2 - y));
    gradient.addColorStop(0, css(inner));
    gradient.addColorStop(1, css(outer));
    return gradient;
}

// Sets the font by its JUCE height.
function setFont(ctx, height, bold) {
    ctx.font = `${bold ? 'bold ' : ''}${height * kFontSizePerHeight}px ${kFontFamily}`;
}

function drawText(ctx, text, x, y, colour, height, bold, align = 'center') {
    setFont(ctx, height, bold);
    ctx.fillStyle = css(colour);
    ctx.textAlign = align;
    ctx.textBaseline = 'alphabetic';
    ctx.fillText(text, x, y + height * kFontSizePerHeight * (kFontAscent - kFontDescent) / 2);
}

function roundedRect(ctx, x, y, w, h, radius) {
    ctx.beginPath();
    ctx.moveTo(x + radius, y);
    ctx.arcTo(x + w, y, x + w, y + h, radius);
    ctx.arcTo(x + w, y + h, x, y + h, radius);
    ctx.arcTo(x, y + h, x, y, radius);
    ctx.arcTo(x, y, x + w, y, radius);
    ctx.closePath();
}

//==============================================================================
// Controls, from PanelControls.cpp

function fractionOf(control) {
    return (control.value - control.min) / (control.max - control.min);
}

// A black skirt with a white pointer line and a coloured, slightly domed cap.
function drawKnob(ctx, circle, fraction, capColour, large) {
    const { x, y } = circle;
    const radius = circle.r - kInset;
    const capRatio = large ? 0.70 : 0.60;

    const skirt = radius * 0.97;
    fillCircle(ctx, x, y + radius * 0.03, skirt + radius * 0.02, css(colours.black, 0.18));
    fillCircle(ctx, x, y, skirt,
               linearGradient(ctx, brighter(colours.skirt, 0.5), x - skirt, y - skirt,
                              darker(colours.skirt, 0.6), x + skirt, y + skirt));

    const angle = kRotaryStart + fraction * (kRotaryEnd - kRotaryStart);
    const start = pointOnCircle(x, y, radius * (capRatio - 0.05), angle);
    const end = pointOnCircle(x, y, radius * 0.9, angle);
    drawLine(ctx, start[0], start[1], end[0], end[1], radius * 0.09, css(colours.white));

    const cap = radius * capRatio;
    fillCircle(ctx, x, y, cap,
               linearGradient(ctx, brighter(capColour, 0.25), x - cap, y - cap,
                              darker(capColour, 0.25), x + cap, y + cap));
    strokeCircle(ctx, x, y, cap, radius * 0.03, css(colours.black, 0.35));
}

// The illuminated FREEZE button: a pale green lens that glows when on.
function drawFreezeButton(ctx) {
    const { x, y } = freezeButton.circle;
    const radius = freezeButton.circle.r - kInset;
    const on = freezeButton.on;

    fillCircle(ctx, x, y, radius, css(colours.skirt));

    const lens = radius * 0.85;
    const rim = on ? colours.ledGreen : darker(withSaturation(colours.ledGreen, 0.25), 0.1);
    const core = on ? rgb(0xf4fbd8) : brighter(rim, 0.2);
    fillCircle(ctx, x, y, lens, radialGradient(ctx, core, x, y, rim, x - lens, y - lens));

    const down = pressed === freezeButton && isUnderPointer(freezeButton.circle);
    if (down || hovered === freezeButton)
        fillCircle(ctx, x, y, lens, css(colours.white, down ? 0.25 : 0.12));
}

// The small black push buttons: B, C and TRIG.
function drawPushButton(ctx, button) {
    const { x, y } = button.circle;
    const radius = button.circle.r - kInset;
    fillCircle(ctx, x, y, radius, css(rgb(0x8d8c8e)));

    const cap = radius * 0.88;
    let top = rgb(0x4a4b4d);
    if (hovered === button)
        top = brighter(top, 0.2);
    const down = pressed === button && isUnderPointer(button.circle);
    fillCircle(ctx, x, y, cap,
               linearGradient(ctx, down ? darker(top, 0.4) : top, x - cap, y - cap,
                              rgb(0x121314), x + cap, y + cap));
}

// A bicolour LED: red and green elements from 0 to 1, yellow when both are on.
function drawLed(ctx, circle, red, green) {
    const { x, y } = circle;
    const radius = circle.r - kInset;
    fillCircle(ctx, x, y, radius, css(rgb(0xa9a8a2)));

    // The lit colour carries only the hue; intensity sets how far the lens
    // moves towards it.
    const intensity = Math.max(red, green);
    const r = intensity > 0 ? red / intensity : 0;
    const g = intensity > 0 ? green / intensity : 0;
    const lit = [Math.min(1, r + g * 0.55), Math.min(1, g * 0.9 + r * 0.2), 0.15].map((v) => Math.round(v * 255));
    const lens = radius * 0.8;
    const rimColour = interpolated(rgb(0x8f9a78), lit, intensity);
    const coreColour = interpolated(rimColour, brighter(lit, 0.6), intensity);
    fillCircle(ctx, x, y, lens, radialGradient(ctx, coreColour, x, y, rimColour, x - lens, y - lens));
}

//==============================================================================
// The panel, from the JUCE editor's paint()

function drawLabel(ctx, text, x, y, colour = colours.ink) {
    drawText(ctx, text, x, y, colour, 11, true);
}

function drawPill(ctx, text, x, y, colour) {
    setFont(ctx, 11, true);
    const w = ctx.measureText(text).width + 12;
    roundedRect(ctx, x - w / 2, y - 7.5, w, 15, 3);
    ctx.fillStyle = css(colour);
    ctx.fill();
    drawText(ctx, text, x, y, colours.white, 11, true);
}

function drawScrewHole(ctx, x, y) {
    fillCircle(ctx, x, y, 6, css(rgb(0x3b3c3e)));
    strokeCircle(ctx, x, y, 6.5, 1, css(colours.white, 0.5));
}

// A simplified version of the ornamental rings behind the large knobs.
function drawOrnament(ctx, circle) {
    for (const radius of [50, 56, 70])
        strokeCircle(ctx, circle.x, circle.y, radius, 0.8, css(colours.ornament));
    for (let i = 0; i < 48; ++i) {
        const dot = pointOnCircle(circle.x, circle.y, 63, Math.PI * 2 * i / 48);
        fillCircle(ctx, dot[0], dot[1], 1.2, css(colours.ornament));
    }
}

// White glyphs on the icons beside the LEDs.
function drawGlyph(ctx, glyph, x, y) {
    const white = css(colours.white);
    ctx.fillStyle = white;
    ctx.strokeStyle = white;
    const ring = (cx, radius) => strokeCircle(ctx, cx, y, radius, 1.2, white);

    switch (glyph) {
        case 'doubleRing':
            ring(x - 1.6, 2.6);
            ring(x + 1.6, 2.6);
            break;

        case 'ring':
            ring(x, 3);
            break;

        case 'ringWithMarks':
            ring(x, 2.8);
            for (let i = 0; i < 4; ++i) {
                const mark = pointOnCircle(x, y, 3.6, Math.PI / 2 * i);
                fillCircle(ctx, mark[0], mark[1], 0.9, white);
            }
            break;

        case 'droplet':
            ctx.beginPath();
            ctx.moveTo(x, y - 4);
            ctx.quadraticCurveTo(x + 3.2, y + 0.5, x, y + 3.2);
            ctx.quadraticCurveTo(x - 3.2, y + 0.5, x, y - 4);
            ctx.fill();
            break;

        case 'spread':
            // Two arrows from the centre: a 1-unit shaft, heads 3 wide and
            // 2 long, as Path::addArrow draws them.
            for (const direction of [-1, 1]) {
                const tip = x + 4.2 * direction;
                const base = tip - 2 * direction;
                ctx.fillRect(Math.min(x, base), y - 0.5, Math.abs(base - x), 1);
                ctx.beginPath();
                ctx.moveTo(tip, y);
                ctx.lineTo(base, y - 1.5);
                ctx.lineTo(base, y + 1.5);
                ctx.closePath();
                ctx.fill();
            }
            break;

        case 'feedback': {
            ctx.beginPath();
            ctx.arc(x, y, 3.2, 0.5 - Math.PI / 2, Math.PI * 2 - 0.3 - Math.PI / 2);
            ctx.lineWidth = 1.2;
            ctx.stroke();
            const tip = pointOnCircle(x, y, 3.2, 0.5);
            ctx.beginPath();
            ctx.moveTo(tip[0] - 2, tip[1] - 1.6);
            ctx.lineTo(tip[0] + 1.2, tip[1] - 2.2);
            ctx.lineTo(tip[0] + 0.4, tip[1] + 1);
            ctx.closePath();
            ctx.fill();
            break;
        }

        case 'cube':
            ctx.beginPath();
            ctx.moveTo(x, y - 3.8);
            ctx.lineTo(x + 3.4, y - 1.9);
            ctx.lineTo(x + 3.4, y + 1.9);
            ctx.lineTo(x, y + 3.8);
            ctx.lineTo(x - 3.4, y + 1.9);
            ctx.lineTo(x - 3.4, y - 1.9);
            ctx.closePath();
            ctx.moveTo(x - 3.4, y - 1.9);
            ctx.lineTo(x, y);
            ctx.lineTo(x + 3.4, y - 1.9);
            ctx.moveTo(x, y);
            ctx.lineTo(x, y + 3.8);
            ctx.lineWidth = 1;
            ctx.lineJoin = 'miter';
            ctx.stroke();
            break;

        case 'pencil':
            drawLine(ctx, x - 3, y + 3, x + 3, y - 3, 2, white);
            break;
    }
}

function drawIcon(ctx, glyph, x, y, colour) {
    const size = layout.iconRadius * 2;
    if (glyph === 'pencil') {
        roundedRect(ctx, x - size / 2, y - size / 2, size, size, 2);
        ctx.fillStyle = css(colour);
        ctx.fill();
    } else {
        fillCircle(ctx, x, y, layout.iconRadius, css(colour));
    }
    drawGlyph(ctx, glyph, x, y);
}

function drawDottedLine(ctx, x1, y1, x2, y2) {
    const dots = Math.floor(Math.hypot(x2 - x1, y2 - y1) / 3);
    for (let i = 0; i <= dots; ++i) {
        const t = i / Math.max(1, dots);
        fillCircle(ctx, x1 + (x2 - x1) * t, y1 + (y2 - y1) * t, 0.7, css(colours.ink));
    }
}

function drawLogo(ctx) {
    const [lx, ly, size] = [80, 10, 19];
    ctx.strokeStyle = css(colours.ink);
    ctx.lineWidth = 1.6;
    ctx.strokeRect(lx, ly, size, size);
    ctx.lineWidth = 1.1;
    for (let row = 0; row < 3; ++row) {
        const y = ly + 5 + 4.5 * row;
        ctx.beginPath();
        ctx.moveTo(lx + 3, y);
        for (let i = 0; i < 3; ++i) {
            const x = lx + 3 + 4.4 * i;
            ctx.quadraticCurveTo(x + 2.2, y + (i % 2 === row % 2 ? -2.5 : 2.5), x + 4.4, y);
        }
        ctx.stroke();
    }
}

// Draws everything that does not change into an offscreen canvas at the
// screen's resolution.
function drawPanelLayer() {
    const density = pixelDensity();
    panelLayer = panelLayer || document.createElement('canvas');
    panelLayer.width = Math.round(width * density);
    panelLayer.height = Math.round(height * density);
    const ctx = panelLayer.getContext('2d');
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.fillStyle = css(colours.background);
    ctx.fillRect(0, 0, panelLayer.width, panelLayer.height);
    ctx.setTransform(density * scaleFactor, 0, 0, density * scaleFactor, density * originX, density * originY);

    // Panel.
    ctx.fillStyle = linearGradient(ctx, colours.panel, 0, 0, colours.panelShade, 0, layout.height);
    ctx.fillRect(0, 0, layout.width, layout.height);
    for (const y of layout.screwY)
        for (const x of [46, 370])
            drawScrewHole(ctx, x, y);

    // Header.
    drawText(ctx, 'Clouds', 100, 21, colours.ink, 24, false, 'left');
    drawLogo(ctx);
    drawText(ctx, 'texture synthesizer', 228, 24, colours.ink, 12, false, 'left');
    ctx.fillStyle = css(colours.ornament);
    ctx.fillRect(14, 34, layout.width - 28, 6);

    for (const circle of [layout.position, layout.size, layout.pitch])
        drawOrnament(ctx, circle);

    // Freeze, LED icons and buttons B and C.
    drawLabel(ctx, 'FREEZE', 99, layout.freeze.y);
    const qualityGlyphs = ['doubleRing', 'ring', 'doubleRing', 'ringWithMarks'];
    const blendGlyphs = ['droplet', 'spread', 'feedback', 'cube'];
    layout.ledX.forEach((x, i) => {
        drawIcon(ctx, qualityGlyphs[i], x, layout.qualityIconY, colours.red);
        drawIcon(ctx, blendGlyphs[i], x, layout.blendIconY, colours.teal);
    });

    const lastIconX = layout.ledX[3] + layout.iconRadius + 2;
    const b = layout.blendButton;
    drawDottedLine(ctx, lastIconX, layout.qualityIconY, b.x, layout.qualityIconY);
    drawDottedLine(ctx, b.x, layout.qualityIconY, b.x, b.y - b.r - 2);
    drawDottedLine(ctx, lastIconX, layout.blendIconY, b.x, layout.blendIconY);
    drawDottedLine(ctx, b.x, layout.blendIconY, b.x, b.y + b.r + 2);
    drawIcon(ctx, 'pencil', layout.saveButton.x, layout.qualityIconY, colours.red);

    // Pitch's centre mark.
    const p = layout.pitch;
    drawLine(ctx, p.x, p.y - p.r - 10, p.x, p.y - p.r - 2, 1.4, css(colours.ink));

    // Knob labels.
    drawPill(ctx, 'POSITION', layout.position.x, layout.largeLabelY, colours.red);
    drawPill(ctx, 'SIZE', layout.size.x, layout.largeLabelY, colours.teal);
    drawLabel(ctx, 'PITCH', layout.pitch.x, layout.largeLabelY);

    drawPill(ctx, 'IN GAIN', layout.inputGain.x, layout.smallLabelY, colours.red);
    drawPill(ctx, 'DENSITY', layout.density.x, layout.smallLabelY, colours.red);
    drawPill(ctx, 'TEXTURE', layout.texture.x, layout.smallLabelY, colours.teal);
    drawLabel(ctx, 'BLEND', layout.blend.x - 4, layout.smallLabelY);
    fillCircle(ctx, layout.blend.x + 26, layout.smallLabelY, 3.5, css(colours.teal));

    // Bottom row: TRIG; the name is drawn with the controls.
    ctx.fillStyle = css(colours.ornament);
    ctx.fillRect(30, layout.dividerY - 1, 360, 2);
    drawLabel(ctx, 'TRIG', layout.trigger.x - 33, layout.trigger.y);
}

// The name, or a warning in its place until the board's state arrives, as
// the JUCE editor shows its buffer warning.
function drawFooter(ctx) {
    if (synced)
        drawText(ctx, 'WeatherFields', layout.width / 2, layout.bottomRowY, colours.ink, 13, false);
    else
        drawText(ctx, 'Waiting for the board...', layout.width / 2, layout.bottomRowY, colours.red, 11, true);
}

// The value bubble shown while a knob is dragged or hovered, as JUCE's slider
// popup draws it: bold 15-pixel white text on a dark rounded box with an
// arrow, above the knob. Its size is in screen pixels.
function drawBubble(ctx) {
    const knob = dragged ? dragged.knob : (knobs.includes(hovered) ? hovered : null);
    if (!knob)
        return;

    const control = controlOf(knob);
    const text = control.value.toFixed(control.decimals || 2) + (control.suffix || '');
    const unit = 1 / scaleFactor;
    setFont(ctx, 15 * unit, true);
    const w = ctx.measureText(text).width + 18 * unit;
    const h = 15 * 1.6 * unit;
    const tipX = knob.circle.x;
    const tipY = knob.circle.y - knob.circle.r;
    const bodyX = Math.min(Math.max(tipX - w / 2, 0), layout.width - w);
    const bodyY = tipY - 10 * unit - h;
    const arrow = Math.min(15 * unit, w * 0.2, h * 0.2);

    ctx.beginPath();
    ctx.moveTo(bodyX + 5 * unit, bodyY);
    ctx.arcTo(bodyX + w, bodyY, bodyX + w, bodyY + h, 5 * unit);
    ctx.arcTo(bodyX + w, bodyY + h, bodyX, bodyY + h, 5 * unit);
    ctx.lineTo(tipX + arrow / 2, bodyY + h);
    ctx.lineTo(tipX, tipY);
    ctx.lineTo(tipX - arrow / 2, bodyY + h);
    ctx.arcTo(bodyX, bodyY + h, bodyX, bodyY, 5 * unit);
    ctx.arcTo(bodyX, bodyY, bodyX + w, bodyY, 5 * unit);
    ctx.closePath();
    ctx.fillStyle = css(rgb(0x263238));
    ctx.fill();
    ctx.lineWidth = unit;
    ctx.strokeStyle = css(rgb(0x8e989b));
    ctx.stroke();

    drawText(ctx, text, bodyX + w / 2, bodyY + h / 2, colours.white, 15 * unit, true);
}
