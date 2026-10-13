import { mount } from 'anyconsole';
import { app } from '../switch/app.js';

mount(app({
  background: '#2d2d2d', sidebar: '#262626', surface: '#454545', text: '#ffffff', muted: '#a8a8a8',
  glow: '#00c3e3aa', scrim: '#000000b3', panel: '#2d2d2db3', tileLightness: 0.38,
}));
