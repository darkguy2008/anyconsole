import { mount } from 'anyconsole';
import { app } from '../switch/app.js';

mount(app({
  background: '#ebebeb', sidebar: '#e0e0e0', surface: '#ffffff', text: '#2d2d2d', muted: '#7a7a7a', line: '#c8c8c8',
  glow: '#00c3e3aa', scrim: '#00000080', panel: '#f2f2f2b3', tileLightness: 0.55,
}));
