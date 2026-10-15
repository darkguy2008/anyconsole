import { fill, hints, html, mount, planes, rewindCells, saveCells, time, translate, useClock, useConsole, useLatest } from 'anyconsole';

const white = '#ffffff';
const black = '#000000';
const grey = '#8c8c8c';
const dim = '#000000b3';
const margin = 48;
const fade = { opacity: 200 };
const guideWidth = 640;
const barHeight = 72;
const carouselHeight = 226;
const pill = { borderRadius: 18, borderWidth: 2, borderColor: white, paddingVertical: 4 };
const faceGlyphs = { cross: '\u00d7', circle: '\u25cb', square: '\u25a1', triangle: '\u25b3' };

function Players({ players }) {
  return players.map(player => html`
    <view key=${player.profile} style=${{ ...pill, paddingHorizontal: 14 }}>
      <text style=${{ fontSize: 24, fontWeight: 600 }}>${`P${player.pad}`}</text>
    </view>`);
}

function TitleBar({ title, players }) {
  const now = useClock();
  return html`
    <view style=${{ flexDirection: 'row', alignItems: 'center', gap: 20, paddingHorizontal: margin, paddingTop: 40, paddingBottom: 24 }}>
      <text style=${{ flexGrow: 1, flexShrink: 1, fontSize: 52, fontWeight: 700 }}>${title}</text>
      ${players && html`<${Players} players=${players} />`}
      ${players && html`<text style=${{ fontSize: 32, color: grey }}>${time(now)}</text>`}
    </view>`;
}

function Row({ row, selected }) {
  return html`
    <view style=${{ flexDirection: 'row', alignItems: 'flex-start', gap: 32, paddingVertical: 16, paddingHorizontal: 24, color: selected ? black : white }}
      transition=${{ color: 150 }}>
      <text style=${{ flexGrow: 1, flexShrink: 1, fontSize: 36 }}>${row.label}</text>
      ${row.value && html`<text style=${{ flexShrink: 1, maxWidth: '55%', fontSize: 36, textAlign: 'right', color: selected ? '#404040' : grey }}
        transition=${{ color: 150 }}>${row.value}</text>`}
    </view>`;
}

function List({ view }) {
  return html`
    <view style=${{ flexGrow: 1, flexShrink: 1, overflow: 'hidden', paddingHorizontal: 24, paddingBottom: 24 }} follow=${view.selected} plane=${view.plane}>
      <view style=${{ position: 'absolute', backgroundColor: white, borderRadius: 16 }} track=${view.selected} />
      ${view.rows.map((row, index) => html`<${Row} key=${`${row.action} ${row.arg} ${row.label}`} row=${row} selected=${index === view.selected} />`)}
    </view>`;
}

function Tabs({ view }) {
  return html`
    <view style=${{ flexDirection: 'row', gap: 12, paddingHorizontal: margin, paddingBottom: 24 }}>
      ${view.tabs.map((tab, index) => html`
        <view key=${tab} style=${{ ...pill, paddingHorizontal: 18, backgroundColor: index === view.tab ? white : black, color: index === view.tab ? black : white }} transition=${{ backgroundColor: 150, color: 150 }}>
          <text style=${{ fontSize: 28 }}>${tab}</text>
        </view>`)}
    </view>`;
}

function Panel({ view, players, width, shown, side }) {
  const kept = useLatest(view);
  if (!kept) return null;
  return html`
    <view style=${{ position: 'absolute', left: 0, top: 0, bottom: barHeight, width, backgroundColor: black, opacity: shown ? 1 : 0, translateX: shown ? 0 : side }}
      from=${{ opacity: 0, translateX: side }} transition=${{ opacity: 200, translateX: { duration: 260, easing: 'out' } }}>
      <${TitleBar} title=${kept.title} players=${players} />
      ${kept.tabs && html`<${Tabs} view=${kept} />`}
      <${List} view=${kept} />
    </view>`;
}

function Attract({ state }) {
  return html`
    <view style=${{ ...fill, backgroundColor: black }}>
      <image src=${state.splash} style=${fill} />
      <text style=${{ position: 'absolute', left: 0, right: 0, top: 790, fontSize: 40, textAlign: 'center' }} from=${{ opacity: 0 }}
        transition=${{ opacity: 1000 }}>${translate('Press any button')}</text>
    </view>`;
}

function Launch({ game, percent }) {
  return html`
    <view style=${{ ...fill, backgroundColor: black, alignItems: 'center', justifyContent: 'center', gap: 32 }} from=${{ opacity: 0 }} transition=${fade}>
      <text style=${{ fontSize: 48, fontWeight: 600, maxWidth: 1400, textAlign: 'center' }}>${game.title}</text>
      <view style=${{ width: 640, height: 24, borderRadius: 12, borderWidth: 3, borderColor: white, padding: 4 }}>
        <view style=${{ width: `${percent}%`, height: '100%', borderRadius: 8, backgroundColor: white }} transition=${{ width: 300 }} />
      </view>
      <text style=${{ fontSize: 32, color: grey }}>${game.status}</text>
    </view>`;
}

function Cell({ source, label, selected }) {
  return html`
    <view style=${{ width: 288, height: 162, borderRadius: 12, borderWidth: selected ? 4 : 0, borderColor: white, backgroundColor: '#1a1a1a', overflow: 'hidden' }}
      transition=${{ borderWidth: 120 }}>
      ${source && html`<image src=${source} style=${{ ...fill }} />`}
      ${label && html`<text style=${{ position: 'absolute', left: 0, right: 0, bottom: 0, paddingVertical: 6, backgroundColor: dim, fontSize: 26, textAlign: 'center' }}>${label}</text>`}
    </view>`;
}

function Carousel({ cells, selected, reverse }) {
  return html`
    <view style=${{ position: 'absolute', left: guideWidth, right: 0, bottom: barHeight, height: carouselHeight, backgroundColor: black,
      flexDirection: reverse ? 'row-reverse' : 'row', gap: 24, padding: 32, overflow: 'hidden' }} follow=${selected}
      from=${{ translateY: carouselHeight + barHeight }} transition=${{ translateY: 220 }}>
      ${cells.map((cell, index) => html`<${Cell} key=${index} source=${cell.source} label=${cell.label} selected=${index === selected} />`)}
    </view>`;
}

function Hints({ shown }) {
  return html`
    <view style=${{ position: 'absolute', left: 0, right: 0, bottom: 0, height: barHeight, backgroundColor: black, borderTopWidth: 2,
      borderColor: grey, flexDirection: 'row', alignItems: 'center', justifyContent: 'flex-end', gap: 36, paddingHorizontal: margin }}>
      ${shown.map(hint => html`
        <view key=${hint.button} style=${{ flexDirection: 'row', alignItems: 'center', gap: 12 }}>
          <view style=${{ minWidth: 40, height: 40, borderRadius: 20, paddingHorizontal: 10, backgroundColor: white, alignItems: 'center', justifyContent: 'center' }}>
            <text style=${{ fontSize: 26, fontWeight: 700, color: black }}>${faceGlyphs[hint.button] ?? hint.button}</text>
          </view>
          <text style=${{ fontSize: 28 }}>${hint.label}</text>
        </view>`)}
    </view>`;
}

function Dialog({ view }) {
  return html`
    <view style=${{ ...fill, backgroundColor: dim, alignItems: 'center', justifyContent: 'center' }} from=${{ opacity: 0 }} transition=${fade}>
      <view style=${{ width: 720, backgroundColor: black, borderRadius: 16, borderWidth: 2, borderColor: white, padding: margin, gap: 36, alignItems: 'center' }}>
        <text style=${{ fontSize: 40, fontWeight: 700, textAlign: 'center' }}>${view.title}</text>
        <view style=${{ backgroundColor: white, borderRadius: 16, paddingVertical: 16, paddingHorizontal: 56 }}>
          <text style=${{ fontSize: 36, color: black }}>${view.rows[view.selected].label}</text>
        </view>
      </view>
    </view>`;
}

function Notifications({ notifications }) {
  return html`
    <view style=${{ position: 'absolute', top: 32, right: 32, width: 640, gap: 12, alignItems: 'flex-end' }}>
      ${notifications.map(text => html`
        <view key=${text} style=${{ backgroundColor: white, borderRadius: 16, paddingVertical: 14, paddingHorizontal: 24, color: black }}
          from=${{ opacity: 0, translateX: 40 }} transition=${{ opacity: 200, translateX: 'spring' }}>
          <text style=${{ fontSize: 30 }}>${text}</text>
        </view>`)}
    </view>`;
}

function App() {
  const state = useConsole();
  const { dashboard, guide, picker, dialog } = planes(state);
  const players = state.session.players;
  const launch = state.launching;
  const launched = launch && state.games.find(game => game.key === launch.game);
  const preview = state.rewind.preview ?? state.saves.preview;
  const shownHints = hints(state);
  return html`
    <view style=${{ ...fill, fontFamily: 'Inter.ttf', color: white }}>
      ${state.screen === 'attract' ? html`<${Attract} state=${state} />` : html`
        <${Panel} view=${dashboard} players=${players} width=${1920} shown=${!!dashboard && !launch} side=${0} />`}
      ${launched && html`<${Launch} game=${launched} percent=${launch.percent} />`}
      ${preview && html`<view style=${{ ...fill, backgroundColor: black }}><image src=${preview} style=${fill} /></view>`}
      <${Panel} view=${guide} players=${players} width=${guideWidth} shown=${!!guide} side=${-guideWidth} />
      ${state.rewind.choosing && html`<${Carousel} cells=${rewindCells(state.rewind)} selected=${state.rewind.back} reverse=${true} />`}
      ${state.saves.choosing && html`<${Carousel} cells=${saveCells(state.saves)} selected=${state.saves.slot} />`}
      ${dialog && html`<${Dialog} view=${dialog} />`}
      ${shownHints.length > 0 && html`<${Hints} shown=${shownHints} />`}
      ${picker && html`<view style=${{ ...fill, backgroundColor: black }}><${TitleBar} title=${picker.title} /><${List} view=${picker} /></view>`}
      <${Notifications} notifications=${state.notifications} />
    </view>`;
}

mount(App);
