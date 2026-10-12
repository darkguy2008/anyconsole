import { h, render } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';
import htm from 'htm';
import * as native from 'native';

class Node {
  constructor(handle) {
    this.handle = handle;
    this.parentNode = null;
    this.childNodes = [];
  }
  get firstChild() { return this.childNodes[0] ?? null; }
  get nextSibling() {
    const siblings = this.parentNode?.childNodes;
    return siblings ? siblings[siblings.indexOf(this) + 1] ?? null : null;
  }
  insertBefore(child, anchor) {
    child.remove();
    const at = anchor ? this.childNodes.indexOf(anchor) : this.childNodes.length;
    this.childNodes.splice(at, 0, child);
    child.parentNode = this;
    native.insert(this.handle, child.handle, at);
    return child;
  }
  appendChild(child) { return this.insertBefore(child, null); }
  removeChild(child) {
    this.childNodes.splice(this.childNodes.indexOf(child), 1);
    child.parentNode = null;
    native.remove(child.handle);
    return child;
  }
  remove() { this.parentNode?.removeChild(this); }
}

class Element extends Node {
  constructor(type, handle = native.node(type)) {
    super(handle);
    this.nodeType = 1;
    this.localName = type;
    this.attributes = [];
    this.style = new Proxy({}, {
      set: (style, name, value) => {
        style[name] = value;
        native.set(handle, name, value === '' ? undefined : value);
        return true;
      },
    });
  }
  set transition(motions) { native.transition(this.handle, motions); }
  set from(values) { native.from(this.handle, values); }
  setAttribute(name, value) { native.set(this.handle, name, value); }
  removeAttribute(name) { native.set(this.handle, name, undefined); }
  addEventListener() {}
  removeEventListener() {}
}

class Text extends Node {
  constructor(data) {
    super(native.node(''));
    this.nodeType = 3;
    this.data = data;
  }
  get data() { return this.text; }
  set data(text) {
    this.text = String(text);
    native.text(this.handle, this.text);
  }
}

globalThis.document = {
  createElementNS: (namespace, type) => new Element(type),
  createElement: type => new Element(type),
  createTextNode: data => new Text(data),
};
globalThis.setTimeout = (callback, milliseconds, ...values) => native.timer(() => callback(...values), milliseconds ?? 0);
globalThis.clearTimeout = timer => native.cancel(timer);
globalThis.queueMicrotask = callback => Promise.resolve().then(callback);

let current = null;
let waiting = null;
let screen = null;
const watchers = new Set();
const handlers = new Set();
native.listen(
  json => {
    current = JSON.parse(json);
    if (waiting) render(h(waiting), screen);
    waiting = null;
    for (const watcher of watchers) watcher(current);
  },
  name => [...handlers].some(handler => handler(name, current) === true),
);

export const html = htm.bind(h);
export const select = native.select;
export const play = native.play;
export const fill = { position: 'absolute', left: 0, top: 0, right: 0, bottom: 0 };

export function time(date) {
  return `${String(date.getHours()).padStart(2, '0')}:${String(date.getMinutes()).padStart(2, '0')}`;
}

export function planes(state) {
  return Object.fromEntries(state.views.map(view => [view.plane, view]));
}

export function rewindCells(rewind) {
  return [{ source: rewind.current, label: 'Save' }, ...rewind.screens.map(source => ({ source }))];
}

export function useConsole() {
  const [state, setState] = useState(current);
  useEffect(() => {
    watchers.add(setState);
    setState(current);
    return () => watchers.delete(setState);
  }, []);
  return state;
}

export function useCommand(handler) {
  useEffect(() => {
    handlers.add(handler);
    return () => handlers.delete(handler);
  }, [handler]);
}

export function useClock() {
  const [now, setNow] = useState(new Date());
  useEffect(() => {
    const timer = setTimeout(() => setNow(new Date()), (60 - now.getSeconds()) * 1000 - now.getMilliseconds());
    return () => clearTimeout(timer);
  }, [now]);
  return now;
}

export function useLatest(value) {
  const kept = useRef(value);
  if (value != null) kept.current = value;
  return kept.current;
}

export function mount(App) {
  screen = new Element('root', native.root());
  if (current) render(h(App), screen);
  else waiting = App;
}
