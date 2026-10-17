import { faces, fill, hints, html, planes, play, rewindCells, saveCells, select, time, translate, useClock, useCommand, useConsole, useLatest } from 'anyconsole';
import { useCallback, useEffect, useRef, useState } from 'preact/hooks';

const font = '../switch/Nunito.ttf';
const icons = '../switch/MaterialSymbolsRounded.ttf';
const sound = name => play(`../switch/${name}.ogg`);
const accent = '#00c3e3';
const storeColumns = 5;
const pulseMilliseconds = 900;
const artMarginDivisor = 16;
const maxFillRatio = 5 / 4;
const backdropDim = '#000000b3';
const coverBlur = 0.05;
const tileRadius = 6;
const edge = 24;
const barHeight = 72;
const guideSpace = 768;
const moves = { left: -1, right: 1 };
const faceIcons = { cross: 'close', circle: 'circle', square: 'square', triangle: 'change_history' };
const steps = { up: -storeColumns, down: storeColumns, left: -1, right: 1 };
const actionIcons = {
  'open-store': 'storefront', 'open-settings': 'settings', play: 'play_arrow', resume: 'play_arrow', close: 'close', uninstall: 'delete',
  install: 'download', dashboard: 'home', volume: 'volume_up', 'open-outputs': 'speaker', 'open-microphones': 'mic', 'microphone-volume': 'mic',
  'open-controllers': 'sports_esports', reload: 'refresh', restart: 'restart_alt', 'power-off': 'power_settings_new', 'log-out': 'logout',
  rewind: 'history', emulate: 'keyboard', analog: 'joystick', picture: 'tv', 'second-screen': 'picture_in_picture', 'display-mode': 'monitor', 'open-resolutions': 'aspect_ratio', resolution: 'aspect_ratio',
  pair: 'bluetooth_searching', forget: 'link_off', 'confirm-log-out': 'logout', login: 'person', 'save-states': 'save',
};

function hue(text) {
  let hash = 0;
  for (const character of text) hash = (hash * 31 + character.codePointAt(0)) % 360;
  return hash;
}

function hsl(degrees, saturation, lightness) {
  const chroma = (1 - Math.abs(2 * lightness - 1)) * saturation;
  const channel = shift => {
    const k = (shift + degrees / 30) % 12;
    return Math.round(255 * (lightness - chroma / 2 * Math.max(-1, Math.min(k - 3, 9 - k, 1))));
  };
  return `#${[0, 8, 4].map(shift => channel(shift).toString(16).padStart(2, '0')).join('')}`;
}

function Icon({ name, size = 40, color, filled = true }) {
  return html`<text style=${{ fontFamily: icons, fontSize: size, lineHeight: 1, color, fontVariation: `FILL=${Number(filled)}` }}>${name}</text>`;
}

function usePulse(active) {
  const [bright, setBright] = useState(false);
  useEffect(() => {
    if (!active) return undefined;
    const timer = setTimeout(() => setBright(!bright), pulseMilliseconds);
    return () => clearTimeout(timer);
  }, [active, bright]);
  return bright;
}

function Frame({ index, radius, theme, active, inset = -10 }) {
  const bright = usePulse(active);
  return html`
    <view style=${{ position: 'absolute', margin: inset, borderRadius: radius, borderWidth: 6, borderColor: accent,
      shadowColor: theme.glow, shadowBlur: bright ? 28 : 12, opacity: active ? 1 : 0 }}
      track=${index} transition=${{ shadowBlur: pulseMilliseconds, opacity: 150, track: 'spring' }} />`;
}

function fillsSquare(aspect) {
  return aspect && Math.max(aspect, 1 / aspect) <= maxFillRatio;
}

function Tile({ game, size, theme }) {
  const art = game.thumbnailCached;
  const filled = fillsSquare(game.thumbnailAspect);
  const backdrop = art && !filled;
  return html`
    <view style=${{ width: size, height: size, borderRadius: tileRadius, overflow: 'hidden', backgroundColor: hsl(hue(game.title), 0.45, theme.tileLightness),
      padding: 24, justifyContent: 'space-between', color: '#ffffff', shadowColor: '#00000040', shadowBlur: 10, shadowOffsetY: 4 }}>
      <text style=${{ fontSize: size / 8, fontWeight: 800, lineHeight: 1.1, opacity: art ? 0 : 1 }}>${game.title}</text>
      <text style=${{ fontSize: size / 16, fontWeight: 700, opacity: art ? 0 : 0.8 }}>${game.platform.toUpperCase()}</text>
      ${backdrop && html`<image src=${game.thumbnail} style=${{ ...fill, borderRadius: tileRadius, blur: coverBlur }} objectFit="cover" />`}
      ${backdrop && html`<view style=${{ ...fill, borderRadius: tileRadius, backgroundColor: backdropDim }} />`}
      ${game.thumbnail && html`<image src=${game.thumbnail} style=${{ ...fill, borderRadius: tileRadius, margin: filled ? 0 : size / artMarginDivisor }}
        objectFit=${filled ? 'cover' : 'contain'} />`}
    </view>`;
}

function games(state, view) {
  return view.rows.map(row => row.action === 'open-game' && state.games[row.arg]);
}

function TopBar({ state, theme }) {
  const now = useClock();
  return html`
    <view style=${{ flexDirection: 'row', alignItems: 'center', paddingHorizontal: 64, paddingTop: 40, gap: 16 }}>
      ${state.session.players.map(player => html`
        <view key=${player.profile} style=${{ width: 64, height: 64, borderRadius: 32, backgroundColor: hsl(player.profile * 90, 0.6, 0.5),
          alignItems: 'center', justifyContent: 'center', borderWidth: 4, borderColor: theme.surface }}>
          <text style=${{ fontSize: 26, fontWeight: 800, color: '#ffffff' }}>${`P${player.pad}`}</text>
        </view>`)}
      <view style=${{ flexGrow: 1 }} />
      <text style=${{ fontSize: 34, fontWeight: 700 }}>${time(now)}</text>
    </view>`;
}

function Face({ face, theme }) {
  return html`
    <view style=${{ minWidth: 40, height: 40, borderRadius: 20, paddingHorizontal: 6, backgroundColor: theme.text, alignItems: 'center', justifyContent: 'center' }}>
      ${faceIcons[face] ? html`<${Icon} name=${faceIcons[face]} size=${28} color=${theme.background} filled=${false} />`
        : html`<text style=${{ fontSize: 26, fontWeight: 800, color: theme.background }}>${face}</text>`}
    </view>`;
}

function Button({ face, label, theme }) {
  return html`
    <view style=${{ flexDirection: 'row', alignItems: 'center', gap: 12 }}>
      <${Face} face=${face} theme=${theme} />
      <text style=${{ fontSize: 28, fontWeight: 700 }}>${label}</text>
    </view>`;
}

function Home({ state, view, theme, lastTile }) {
  const list = games(state, view);
  const tiles = list.filter(Boolean), buttons = view.rows.slice(tiles.length);
  const inTiles = view.selected < tiles.length;
  const chosen = inTiles ? tiles[view.selected] : null;
  const shownTile = inTiles ? view.selected : lastTile;
  return html`
    <view style=${fill}>
      <${TopBar} state=${state} theme=${theme} />
      <text style=${{ position: 'absolute', left: 104, right: 104, top: 168, fontSize: 36, fontWeight: 800, color: accent, textOverflow: 'marquee',
        opacity: chosen ? 1 : 0 }} transition=${{ opacity: 150 }}>${chosen ? chosen.title : ''}</text>
      <view style=${{ position: 'absolute', left: 0, right: 0, top: 230, height: 470, flexDirection: 'row', gap: 28, paddingHorizontal: 104,
        alignItems: 'center', overflow: 'hidden' }} follow=${shownTile} plane=${inTiles ? view.plane : null}>
        ${tiles.map(game => html`<${Tile} key=${game.key} game=${game} size=${400} theme=${theme} />`)}
        <${Frame} index=${shownTile} radius=${14} theme=${theme} active=${inTiles} />
      </view>
      <view style=${{ position: 'absolute', left: 0, right: 0, top: 760, flexDirection: 'row', justifyContent: 'center', gap: 40 }}>
        ${buttons.map(row => html`
          <view key=${row.label} style=${{ width: 116, height: 116, borderRadius: 58, backgroundColor: theme.surface, alignItems: 'center',
            justifyContent: 'center', shadowColor: '#00000033', shadowBlur: 8 }}>
            <${Icon} name=${actionIcons[row.action] ?? 'apps'} size=${56} color=${accent} />
          </view>`)}
        <${Frame} index=${inTiles ? 0 : view.selected - tiles.length} radius=${68} theme=${theme} active=${!inTiles} />
      </view>
      <text style=${{ position: 'absolute', left: 0, right: 0, top: 900, fontSize: 30, fontWeight: 700, textAlign: 'center', color: accent,
        opacity: inTiles ? 0 : 1 }} transition=${{ opacity: 150 }}>${inTiles ? '' : buttons[view.selected - tiles.length]?.label}</text>
    </view>`;
}

function Store({ state, view, theme }) {
  const tiles = games(state, view).filter(Boolean);
  return html`
    <view style=${fill}>
      <${TopBar} state=${state} theme=${theme} />
      <view style=${{ position: 'absolute', left: 104, right: 104, top: 130, flexDirection: 'row', alignItems: 'center', gap: 48 }}>
        <text style=${{ fontSize: 52, fontWeight: 800 }}>${view.title}</text>
        <text style=${{ flexGrow: 1, flexShrink: 1, fontSize: 36, fontWeight: 800, color: accent, textAlign: 'right', textOverflow: 'marquee' }}>
          ${tiles[view.selected]?.title ?? ''}</text>
      </view>
      <view style=${{ position: 'absolute', left: 0, right: 0, top: 220, bottom: 100, overflow: 'hidden', flexDirection: 'row', flexWrap: 'wrap',
        alignContent: 'flex-start', gap: 32, paddingHorizontal: 104, paddingVertical: 24 }} follow=${view.selected} plane=${view.plane}>
        ${tiles.map(game => html`<${Tile} key=${game.key} game=${game} size=${308} theme=${theme} />`)}
        <${Frame} index=${view.selected} radius=${14} theme=${theme} active=${true} />
      </view>
    </view>`;
}

function ActionList({ view, theme, width }) {
  return html`
    <view style=${{ width, gap: 16, overflow: 'hidden', padding: 10 }} follow=${view.selected} plane=${view.plane}>
      ${view.rows.map(row => html`
        <view key=${`${row.action} ${row.arg} ${row.label}`} style=${{ flexDirection: 'row', alignItems: 'center', gap: 20, paddingVertical: 20,
          paddingHorizontal: 28, borderRadius: 20, backgroundColor: theme.surface }}>
          <${Icon} name=${actionIcons[row.action] ?? 'chevron_right'} size=${40} color=${accent} />
          <text style=${{ flexGrow: 1, flexShrink: 1, fontSize: 34, fontWeight: 700 }}>${row.label}</text>
          ${row.value && html`<text style=${{ flexShrink: 1, maxWidth: '50%', fontSize: 30, color: theme.muted, textAlign: 'right' }}>${row.value}</text>`}
        </view>`)}
      <${Frame} index=${view.selected} radius=${26} theme=${theme} active=${true} inset=${-6} />
    </view>`;
}

function GamePage({ state, view, theme }) {
  const game = state.games.find(entry => entry.key === view.game);
  if (!game) return null;
  return html`
    <view style=${{ ...fill, backgroundColor: theme.background, flexDirection: 'row', paddingHorizontal: 160, paddingVertical: 140, gap: 80 }}
      from=${{ opacity: 0, translateY: 40 }} transition=${{ opacity: 200, translateY: 'spring' }}>
      <${Tile} game=${game} size=${560} theme=${theme} />
      <view style=${{ flexGrow: 1, flexShrink: 1, gap: 24 }}>
        <text style=${{ fontSize: 60, fontWeight: 800, lineHeight: 1.1 }}>${game.title}</text>
        <text style=${{ fontSize: 30, color: theme.muted, fontWeight: 700 }}>${[game.platform.toUpperCase(), game.status].filter(Boolean).join('  ·  ')}</text>
        <view style=${{ height: 24 }} />
        <${ActionList} view=${view} theme=${theme} width=${720} />
      </view>
    </view>`;
}

function Settings({ state, view, theme }) {
  const chosen = view.rows[view.selected];
  const system = view.screen === 'settings';
  return html`
    <view style=${{ ...fill, backgroundColor: theme.background, flexDirection: 'row' }}>
      <view style=${{ width: 640, paddingBottom: 2 * edge + barHeight, backgroundColor: theme.sidebar, paddingTop: 72, paddingHorizontal: 40, gap: 8 }}>
        <view style=${{ flexDirection: 'row', alignItems: 'center', gap: 16, paddingBottom: 40 }}>
          <${Icon} name=${system ? 'settings' : 'aspect_ratio'} size=${52} />
          <text style=${{ fontSize: 44, fontWeight: 800 }}>${system ? translate('System Settings') : view.title}</text>
        </view>
        <view style=${{ flexGrow: 1, flexShrink: 1, overflow: 'hidden', gap: 4 }} follow=${view.selected} plane=${view.plane}>
          <view style=${{ position: 'absolute', backgroundColor: theme.surface, borderRadius: 12, borderWidth: 0 }} track=${view.selected}
            transition=${{ track: 'spring' }} />
          <view style=${{ position: 'absolute', width: 8, backgroundColor: accent, borderRadius: 4 }} track=${view.selected}
            transition=${{ track: 'spring' }} />
          ${view.rows.map((row, index) => html`
            <text key=${row.label} style=${{ fontSize: 34, fontWeight: 700, paddingVertical: 18, paddingHorizontal: 32,
              color: index === view.selected ? accent : theme.text }} transition=${{ color: 150 }}>${row.label}</text>`)}
        </view>
      </view>
      ${chosen && html`
        <view style=${{ flexGrow: 1, flexShrink: 1, paddingHorizontal: 96, paddingTop: 180, gap: 40 }}>
          <text style=${{ fontSize: 56, fontWeight: 800 }}>${chosen.label}</text>
          <view style=${{ flexDirection: 'row', alignItems: 'center', gap: 24, backgroundColor: theme.surface, borderRadius: 20, padding: 32 }}>
            <${Icon} name="chevron_left" size=${56} color=${accent} />
            <text style=${{ flexGrow: 1, flexShrink: 1, fontSize: 40, fontWeight: 700, textAlign: 'center' }}>${chosen.value || chosen.label}</text>
            <${Icon} name="chevron_right" size=${56} color=${accent} />
          </view>
          ${chosen.value ? html`<text style=${{ fontSize: 28, color: theme.muted }}>${translate('Press left or right to change')}</text>` : html`
            <view style=${{ flexDirection: 'row', alignItems: 'center', gap: 12 }}>
              <text style=${{ fontSize: 28, color: theme.muted }}>${translate('Press')}</text>
              <${Face} face=${faces(state).confirm} theme=${theme} />
              <text style=${{ fontSize: 28, color: theme.muted }}>${translate('to %s').replace('%s', chosen.label.toLowerCase())}</text>
            </view>`}
        </view>`}
    </view>`;
}

function Dashboard({ state, view, theme, lastTile }) {
  const screens = { home: Home, store: Store, game: GamePage, settings: Settings, resolutions: Settings };
  const Screen = screens[view.screen] ?? Store;
  return html`<${Screen} state=${state} view=${view} theme=${theme} lastTile=${lastTile} />`;
}

function Slider({ value }) {
  const percent = Number.parseInt(value, 10);
  if (Number.isNaN(percent)) return null;
  return html`
    <view style=${{ width: 220, height: 12, borderRadius: 6, backgroundColor: '#80808066' }}>
      <view style=${{ width: `${percent}%`, height: '100%', borderRadius: 6, backgroundColor: accent }} transition=${{ width: 120 }} />
    </view>`;
}

function GuideRows({ view, theme }) {
  return html`
    <view style=${{ flexGrow: 1, flexShrink: 1, overflow: 'hidden', gap: 6, paddingHorizontal: 20, paddingBottom: 24 }} follow=${view.selected} plane=${view.plane}>
      <view style=${{ position: 'absolute', backgroundColor: theme.surface, borderRadius: 18 }} track=${view.selected} transition=${{ track: 'spring' }} />
      ${view.rows.map(row => html`
        <view key=${`${row.action} ${row.arg} ${row.label}`} style=${{ flexDirection: 'row', alignItems: 'center', gap: 20, paddingVertical: 18,
          paddingHorizontal: 24 }}>
          <${Icon} name=${actionIcons[row.action] ?? 'chevron_right'} size=${38} color=${accent} />
          <text style=${{ flexGrow: 1, flexShrink: 1, fontSize: 32, fontWeight: 700 }}>${row.label}</text>
          ${row.action.endsWith('volume') ? html`<${Slider} value=${row.value} />` : row.value &&
            html`<text style=${{ flexShrink: 1, maxWidth: '45%', fontSize: 28, color: theme.muted, textAlign: 'right' }}>${row.value}</text>`}
        </view>`)}
      <view style=${{ position: 'absolute', borderRadius: 18, borderWidth: 4, borderColor: accent }} track=${view.selected} transition=${{ track: 'spring' }} />
    </view>`;
}

function Tabs({ view, theme }) {
  return html`
    <view style=${{ flexDirection: 'row', alignItems: 'center', gap: 20, paddingHorizontal: 44, paddingBottom: 20 }}>
      <${Face} face="L" theme=${theme} />
      <view style=${{ flexGrow: 1, flexDirection: 'row', justifyContent: 'space-around' }}>
        ${view.tabs.map((tab, index) => html`
          <view key=${tab} style=${{ alignItems: 'center', gap: 8 }}>
            <text style=${{ fontSize: 30, fontWeight: 800, color: index === view.tab ? accent : theme.muted }} transition=${{ color: 150 }}>${tab}</text>
            <view style=${{ width: 48, height: 5, borderRadius: 3, backgroundColor: accent, opacity: index === view.tab ? 1 : 0 }} transition=${{ opacity: 150 }} />
          </view>`)}
      </view>
      <${Face} face="R" theme=${theme} />
    </view>`;
}

function Guide({ state, view, theme }) {
  const kept = useLatest(view);
  const now = useClock();
  if (!kept) return null;
  return html`
    <view style=${{ position: 'absolute', right: edge, top: edge, bottom: 2 * edge + barHeight, width: 720, borderRadius: 32, backgroundColor: theme.panel, backdropBlur: 1,
      shadowColor: '#00000066', shadowBlur: 30, opacity: view ? 1 : 0, translateX: view ? 0 : 760 }}
      from=${{ opacity: 0, translateX: 760 }} transition=${{ opacity: 180, translateX: 'spring' }}>
      <view style=${{ flexDirection: 'row', alignItems: 'center', paddingHorizontal: 44, paddingTop: 36, paddingBottom: 24, gap: 16 }}>
        <text style=${{ flexGrow: 1, flexShrink: 1, fontSize: 38, fontWeight: 800 }}>${kept.title}</text>
        <text style=${{ fontSize: 30, fontWeight: 700, color: theme.muted }}>${time(now)}</text>
      </view>
      ${kept.tabs && html`<${Tabs} view=${kept} theme=${theme} />`}
      <${GuideRows} view=${kept} theme=${theme} />
    </view>`;
}

function Carousel({ cells, selected, reverse, theme }) {
  return html`
    <view style=${{ position: 'absolute', left: edge, right: guideSpace, bottom: 2 * edge + barHeight, height: 220, borderRadius: 32, backgroundColor: theme.panel,
      backdropBlur: 1, flexDirection: reverse ? 'row-reverse' : 'row', gap: 20, padding: 28, overflow: 'hidden' }} follow=${selected} from=${{ translateY: 260 }}
      transition=${{ translateY: 'spring' }}>
      ${cells.map((cell, index) => html`
        <view key=${index} style=${{ width: 290, height: 164, borderRadius: 14, overflow: 'hidden', backgroundColor: '#000000' }}>
          ${cell.source && html`<image src=${cell.source} style=${fill} objectFit="cover" />`}
          ${cell.label && html`<text style=${{ position: 'absolute', left: 0, right: 0, bottom: 0, paddingVertical: 6, backgroundColor: '#000000a0',
            fontSize: 26, fontWeight: 800, textAlign: 'center', color: '#ffffff' }}>${cell.label}</text>`}
        </view>`)}
      <${Frame} index=${selected} radius=${20} theme=${theme} active=${true} inset=${-6} />
    </view>`;
}

function Hints({ shown, theme }) {
  return html`
    <view style=${{ position: 'absolute', left: edge, right: edge, bottom: edge, height: barHeight, borderRadius: 24, backgroundColor: theme.panel,
      backdropBlur: 1, flexDirection: 'row', alignItems: 'center', justifyContent: 'flex-end', gap: 32, paddingHorizontal: 32 }} from=${{ opacity: 0 }}
      transition=${{ opacity: 150 }}>
      ${shown.map(hint => html`<${Button} key=${hint.button} face=${hint.button} label=${hint.label} theme=${theme} />`)}
    </view>`;
}

function Dialog({ view, theme }) {
  return html`
    <view style=${{ ...fill, backgroundColor: theme.scrim, alignItems: 'center', justifyContent: 'center' }} from=${{ opacity: 0 }} transition=${{ opacity: 150 }}>
      <view style=${{ width: 760, borderRadius: 32, backgroundColor: theme.panel, backdropBlur: 1, padding: 48, gap: 40, alignItems: 'center',
        shadowColor: '#00000066', shadowBlur: 30 }} from=${{ scale: 0.9 }} transition=${{ scale: 'spring' }}>
        <text style=${{ fontSize: 40, fontWeight: 800, textAlign: 'center' }}>${view.title}</text>
        <view style=${{ paddingVertical: 18, paddingHorizontal: 56, borderRadius: 18, borderWidth: 4, borderColor: accent }}>
          <text style=${{ fontSize: 32, fontWeight: 800 }}>${view.rows[view.selected].label}</text>
        </view>
      </view>
    </view>`;
}

function Picker({ view, theme }) {
  return html`
    <view style=${{ ...fill, backgroundColor: theme.scrim, alignItems: 'center', justifyContent: 'center', gap: 64 }}
      from=${{ opacity: 0 }} transition=${{ opacity: 200 }}>
      <text style=${{ fontSize: 56, fontWeight: 800, color: '#ffffff' }}>${view.title}</text>
      <view style=${{ flexDirection: 'row', gap: 56 }} plane=${view.plane}>
        ${view.rows.map(row => html`
          <view key=${row.label} style=${{ width: 192, height: 192, borderRadius: 96, backgroundColor: hsl(row.arg * 90, 0.6, 0.5),
            alignItems: 'center', justifyContent: 'center' }}>
            <${Icon} name="person" size=${110} color="#ffffff" />
          </view>`)}
        <${Frame} index=${view.selected} radius=${106} theme=${theme} active=${true} />
      </view>
      <text style=${{ fontSize: 40, fontWeight: 800, color: '#ffffff' }}>${view.rows[view.selected]?.label ?? ''}</text>
      <text style=${{ fontSize: 30, color: '#d0d0d0' }}>${view.rows[view.selected]?.value ?? ''}</text>
    </view>`;
}

function Attract({ state, theme }) {
  const bright = usePulse(true);
  return html`
    <view style=${{ ...fill, backgroundColor: '#000000' }} from=${{ opacity: 0 }} transition=${{ opacity: 400 }}>
      <image src=${state.splash} style=${fill} />
      <text style=${{ position: 'absolute', left: 0, right: 0, top: 800, fontSize: 40, fontWeight: 800, textAlign: 'center', color: '#ffffff',
        opacity: bright ? 1 : 0.4 }} transition=${{ opacity: pulseMilliseconds }}>${translate('Press any button')}</text>
    </view>`;
}

function Launch({ game, percent, theme }) {
  return html`
    <view style=${{ ...fill, backgroundColor: theme.background, alignItems: 'center', justifyContent: 'center', gap: 40 }}
      from=${{ opacity: 0 }} transition=${{ opacity: 200 }}>
      <${Tile} game=${game} size=${360} theme=${theme} />
      <text style=${{ fontSize: 44, fontWeight: 800, maxWidth: 1400, textAlign: 'center' }}>${game.title}</text>
      <view style=${{ width: 560, height: 14, borderRadius: 7, backgroundColor: theme.surface }}>
        <view style=${{ width: `${percent}%`, height: '100%', borderRadius: 7, backgroundColor: accent }} transition=${{ width: 300 }} />
      </view>
      <text style=${{ fontSize: 28, color: theme.muted, fontWeight: 700 }}>${game.status}</text>
    </view>`;
}

function Notifications({ notifications, theme }) {
  return html`
    <view style=${{ position: 'absolute', top: 32, right: 32, width: 680, gap: 14, alignItems: 'flex-end' }}>
      ${notifications.map(text => html`
        <view key=${text} style=${{ flexDirection: 'row', alignItems: 'center', gap: 16, backgroundColor: theme.surface, borderRadius: 24,
          paddingVertical: 18, paddingHorizontal: 26, shadowColor: '#00000055', shadowBlur: 18 }}
          from=${{ opacity: 0, translateY: -30 }} transition=${{ opacity: 200, translateY: 'spring' }}>
          <${Icon} name="info" size=${36} color=${accent} />
          <text style=${{ flexShrink: 1, fontSize: 30, fontWeight: 700 }}>${text}</text>
        </view>`)}
    </view>`;
}

function navigate(name, state, lastTile) {
  const view = state.views[state.views.length - 1];
  if (!view) return false;
  const count = view.rows.length, selected = view.selected;
  const move = target => {
    if (target === selected || target < 0 || target >= count) return true;
    sound('hover');
    select(target);
    return true;
  };
  if (view.screen === 'home' && name in { ...moves, up: 1, down: 1 }) {
    const tiles = view.rows.filter(row => row.action === 'open-game').length;
    const inTiles = selected < tiles;
    if (name in moves) return move(inTiles ? Math.min(Math.max(selected + moves[name], 0), tiles - 1) : Math.max(selected + moves[name], tiles));
    if (name === 'down' && inTiles) return move(tiles < count ? tiles : selected);
    if (name === 'up' && !inTiles) return move(tiles ? lastTile : selected);
    return true;
  }
  if (view.screen === 'store' && name in steps) return move(Math.min(Math.max(selected + steps[name], 0), count - 1));
  if (view.screen === 'whois' && name in moves) return move(selected + moves[name]);
  if (name === 'up' || name === 'down' || name === 'page-up' || name === 'page-down') sound('hover');
  if (name === 'left' || name === 'right') sound('press');
  if (name === 'confirm' || name === 'x' || name === 'y') sound('select');
  if (name === 'back') sound('back');
  if (name === 'guide') sound(state.guide ? 'close' : 'open');
  return false;
}

function useNavigation(view) {
  const lastTile = useRef(0);
  if (view && view.screen === 'home' && view.rows[view.selected]?.action === 'open-game') lastTile.current = view.selected;
  useCommand(useCallback((name, state) => navigate(name, state, lastTile.current), []));
  return lastTile.current;
}

export function app(theme) {
  return function App() {
    const state = useConsole();
    const { dashboard, guide, picker, dialog } = planes(state);
    const shownDashboard = useLatest(dashboard);
    const launch = state.launching;
    const launched = launch && state.games.find(game => game.key === launch.game);
    const preview = state.rewind.preview ?? state.saves.preview;
    const shownHints = hints(state);
    const lastTile = useNavigation(dashboard);
    return html`
      <view style=${{ ...fill, fontFamily: font, color: theme.text }}>
        ${state.screen === 'attract' ? html`<${Attract} state=${state} theme=${theme} />` : shownDashboard && html`
          <view style=${{ ...fill, backgroundColor: theme.background, opacity: dashboard && !launch ? 1 : 0 }} from=${{ opacity: 0 }}
            transition=${{ opacity: 220 }}>
            <${Dashboard} state=${state} view=${shownDashboard} theme=${theme} lastTile=${lastTile} />
          </view>`}
        ${launched && html`<${Launch} game=${launched} percent=${launch.percent} theme=${theme} />`}
        ${preview && html`<view style=${{ ...fill, backgroundColor: '#000000' }}><image src=${preview} style=${fill} /></view>`}
        <${Guide} state=${state} view=${guide} theme=${theme} />
        ${state.rewind.choosing && html`<${Carousel} cells=${rewindCells(state.rewind)} selected=${state.rewind.back} reverse=${true} theme=${theme} />`}
        ${state.saves.choosing && html`<${Carousel} cells=${saveCells(state.saves)} selected=${state.saves.slot} theme=${theme} />`}
        ${dialog && html`<${Dialog} view=${dialog} theme=${theme} />`}
        ${shownHints.length > 0 && html`<${Hints} shown=${shownHints} theme=${theme} />`}
        ${picker && html`<${Picker} view=${picker} theme=${theme} />`}
        <${Notifications} notifications=${state.notifications} theme=${theme} />
      </view>`;
  };
}
