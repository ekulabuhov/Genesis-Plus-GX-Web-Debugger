/**
 * @typedef {Object} Breakpoint
 * @prop {boolean} [execute]
 * @prop {boolean} [read]
 * @prop {boolean} [write]
 * @prop {string} address - hexadecimal string with 0x "0x200"
 * @prop {boolean?} enabled
 * @prop {'rom' | 'vram' | 'cram'} type
 * @prop {string} [value_equal]
 * @prop {string} [comment]
 **/

const defaultBreakpoint = { edit: true, enabled: true, type: "rom" };

export class BreakpointsService {
  /** @type {Breakpoint[]} */
  #breakpoints;
  #onChangeHandler = () => {};

  /** @type {Breakpoint[]} */
  get breakpoints() {
    if (!this.#breakpoints) {
      this.#breakpoints = JSON.parse(localStorage.getItem("breakpoints")) || [
        defaultBreakpoint,
      ];
    }
    return this.#breakpoints;
  }

  set breakpoints(value) {
    if (value.length === 0) {
      value.push(defaultBreakpoint);
    }
    this.#breakpoints = value;
    localStorage.setItem("breakpoints", JSON.stringify(value));
    this.#onChangeHandler();
  }

  /**
   * @param {() => void} handler
   */
  onChange(handler) {
    this.#onChangeHandler = handler;
  }

  /**
   * Adds breakpoint if it doesn't exist already
   * @param {Breakpoint} newBP
   */
  addBreakpoint(newBP) {
    if (!this.breakpoints.some((bp) => bp.address === newBP.address)) {
      this.breakpoints = this.breakpoints.concat([newBP]);
    }
  }
}
