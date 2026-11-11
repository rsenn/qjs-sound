/* maps the browser's pointer, wheel and key events onto the DOM-free input module. */

import { app } from './state.js';
import { cancelAll, keyDown, keyUp, pointerDown, pointerMove, pointerUp, wheel } from './input.js';

/* attachDomInput(cv): `cv` is the canvas that receives pointers; keys come from the window.
 *
 * ```js
 * attachDomInput(document.getElementById('cv'));
 * ```
 */
export function attachDomInput(cv) {
  const pt = e => ({ id: e.pointerId, x: e.clientX, y: e.clientY });
  const typing = e => e.target.tagName === 'INPUT';

  cv.addEventListener('pointerdown', e => {
    if (app.ctx) cv.setPointerCapture(e.pointerId);
    pointerDown(pt(e));
  });
  cv.addEventListener('pointermove', e => {
    const cursor = pointerMove(pt(e));
    if (cursor !== undefined) cv.style.cursor = cursor;
  });
  cv.addEventListener('pointerup', e => pointerUp(pt(e)));
  cv.addEventListener('pointercancel', e => pointerUp(pt(e)));
  cv.addEventListener('contextmenu', e => e.preventDefault());
  cv.addEventListener('wheel', e => {
    if (wheel({ x: e.clientX, y: e.clientY, dy: e.deltaY })) e.preventDefault();
  }, { passive: false });

  addEventListener('blur', cancelAll);
  addEventListener('keydown', e => {
    if (typing(e)) return;
    if (keyDown({ code: e.code, repeat: e.repeat, mod: e.ctrlKey || e.metaKey || e.altKey })) e.preventDefault();
  });
  /* preventing keyup too stops a focused button from also firing its own click on space */
  addEventListener('keyup', e => {
    if (typing(e)) return;
    if (e.code === 'Space') e.preventDefault();
    keyUp({ code: e.code });
  });
}
