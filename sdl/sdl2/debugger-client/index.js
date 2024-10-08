/** 
 * @typedef {Partial<{
 *  pc: number;
 *  prev_pc: number;
 *  sr: number;
 *  c: boolean;
 *  v: boolean;
 *  z: boolean;
 *  n: boolean;
 *  x: boolean;
 * }>} regs 
 * 
 * @typedef {{
 *  start_address: number, 
 *  end_address: number, 
 *  name?: string, 
 *  references: string[],
 *  comment: string,
 * }} func
 * */

import { AsmViewerComponent } from "./asm-viewer/asm-viewer.component.js";
import { BreakpointsComponent } from "./breakpoints/breakpoints.component.js";
import { FunctionListComponent } from "./function-list/function-list.component.js";
import { MemoryViewerComponent } from "./memory-viewer/memory-viewer.component.js";
import { MenuComponent } from "./menu/menu.component.js";
import { MenuService } from "./menu/menu.service.js";
import { PlaneViewerComponent } from "./plane-viewer/plane-viewer.component.js";
import { RegisterViewerComponent } from "./register-viewer/register-viewer.component.js";
import { TileViewerComponent } from "./tile-viewer/tile-viewer.component.js";
import { PaneComponent } from "./tabs/pane.component.js";
import { TabsComponent } from "./tabs/tabs.component.js";
import { WsService } from "./ws.service.js";
import { Ym2612Component } from "./ym2612/ym2612.component.js";
import { SpriteViewerComponent } from "./sprite-viewer/sprite-viewer.component.js";
import { BreakpointsService } from "./breakpoints/breakpoints.service.js";

const appModule = angular.module("app", []);
appModule.component("memoryViewer", MemoryViewerComponent);
appModule.component("registerViewer", RegisterViewerComponent);
appModule.component("asmViewer", AsmViewerComponent);
appModule.component("tileViewer", TileViewerComponent);
appModule.component("breakpoints", BreakpointsComponent);
appModule.component("appMenu", MenuComponent);
appModule.service("menuService", MenuService);
appModule.service("breakpointsService", BreakpointsService);
appModule.component("myTabs", TabsComponent);
appModule.component("myPane", PaneComponent);
appModule.component("ym2612", Ym2612Component);
appModule.component("functionList", FunctionListComponent);
appModule.component("planeViewer", PlaneViewerComponent);
appModule.component("spriteViewer", SpriteViewerComponent);

appModule.controller(
  "RegController",
  class RegController {
    btConnValue = "Connect!";
    connected = false;
    userMessage = "";
    /** @type {regs} */
    regs;
    asm = [];
    $scope;
    _breakInInterrupts;
    isRunning = false;

    constructor($scope) {
      this.$scope = $scope;
    }
        
    get breakInInterrupts() {
      if (!this._breakInInterrupts) {
        this._breakInInterrupts = localStorage.getItem("breakInInterrupts") === "true"
      }
      return this._breakInInterrupts;
    }

    set breakInInterrupts(value) {
      this._breakInInterrupts = value;
      localStorage.setItem("breakInInterrupts", value);
    }

    /* Establish connection. */
    doConnect(addr) {
      /* Register events. */
      WsService.on('open', () => {
        this.connected = true;
        this.btConnValue = "Disconnect!";
        console.log("Connection opened");

        WsService.send("regs");
        this.onBreakInInterruptsChange();
        WsService.syncBreakpoints();
      });

      /* Deals with messages. */
      WsService.on('message', (response) => {
        if (response.type === "regs") {
          this.isRunning = false;
          this.regs = response.data;
        }

        this.$scope.$apply();
      });

      /* Close events. */
      WsService.on('close', (event) => {
        this.btConnValue = "Connect!";
        console.log(
          "Connection closed: wasClean: " +
            event.wasClean +
            ", evCode: " +
            event.code
        );
        this.connected = false;
      });

      WsService.doConnect(addr);
    }

    /* Connect button. */
    onConnectClick() {
      if (this.connected == false) {
        var txt = document.getElementById("txtServer").value;
        this.doConnect(txt);
      } else {
        WsService.close();
        this.connected = false;
        this.btConnValue = "Connect!";
      }
    }

    onSendClick() {
      WsService.send(this.userMessage);
      this.userMessage = "";
    }

    /**
     * @param {string} address
     * @param {string} type
     */
    async viewMemory(address, type) {
      await WsService.tabsController.selectByName("Memory");
      WsService.memoryViewer.showMemoryLocation(address, type);
    }

    onBreakInInterruptsChange() {
      WsService.send(`${this.breakInInterrupts ? 'enable' : 'disable'} break_in_interrupts`)
    }

    onRunClick() {
      this.isRunning = true;
      WsService.send('run');
    }
  }
);

document.onkeydown = function (e) {
  // Step Over
  if (e.key === "F10") {
    e.preventDefault();
    WsService.send(`step_over`);
    return;
  }

  // Step Into
  if (e.key === "F11") {
    e.preventDefault();
    WsService.send(`step`);
    return;
  }

  if (e.key === 'g' && e.metaKey) {
    const response = prompt("Asm Viewer: go to where?");
    WsService.asmViewer.showAsm(response);
  }

  // Ctrl + Left Arrow or Ctrl + Minus navigates asm viewer to the previous location
  if ((e.key === '-' || e.key === 'ArrowLeft') && e.ctrlKey && e.target.nodeName !== 'INPUT') {
    e.preventDefault();
    WsService.asmViewer.goBack();
  }
};
