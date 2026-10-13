import { fill, html, mount, planes, rewindCells, time, useClock, useConsole, useLatest } from 'anyconsole';

const white = '#ffffff';
const black = '#000000';
const grey = '#8c8c8c';
const dim = '#000000b3';
const margin = 48;
const fade = { opacity: 200 };
const guideWidth = 640;

function Players({ players }) {
  return players.map(player => html`
    <view key=${player.profile} style=${{ borderRadius: 18, borderWidth: 2, borderColor: white, paddingHorizontal: 14, paddingVertical: 4 }}>
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

function Panel({ view, players, width, shown, side }) {
  const kept = useLatest(view);
  if (!kept) return null;
  return html`
    <view style=${{ position: 'absolute', left: 0, top: 0, bottom: 0, width, backgroundColor: black, opacity: shown ? 1 : 0, translateX: shown ? 0 : side }}
      from=${{ opacity: 0, translateX: side }} transition=${{ opacity: 200, translateX: { duration: 260, easing: 'out' } }}>
      <${TitleBar} title=${kept.title} players=${players} />
      <${List} view=${kept} />
    </view>`;
}

function Attract({ state }) {
  return html`
    <view style=${{ ...fill, backgroundColor: black }}>
      <image src=${state.splash} style=${fill} />
      <text style=${{ position: 'absolute', left: 0, right: 0, top: 790, fontSize: 40, textAlign: 'center' }} from=${{ opacity: 0 }}
        transition=${{ opacity: 1000 }}>Press any button</text>
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

function Rewind({ rewind }) {
  const cells = rewindCells(rewind);
  return html`
    <view style=${{ position: 'absolute', left: guideWidth, right: 0, bottom: 0, height: 226, backgroundColor: black, flexDirection: 'row-reverse',
      gap: 24, padding: 32, overflow: 'hidden' }} follow=${rewind.back} from=${{ translateY: 226 }} transition=${{ translateY: 220 }}>
      ${cells.map((cell, back) => html`<${Cell} key=${back} source=${cell.source} label=${cell.label} selected=${back === rewind.back} />`)}
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
  const { dashboard, guide, picker } = planes(state);
  const players = state.session.players;
  const launch = state.launching;
  const launched = launch && state.games.find(game => game.key === launch.game);
  const preview = state.rewind.preview;
  return html`
    <view style=${{ ...fill, fontFamily: 'Inter.ttf', color: white }}>
      ${state.screen === 'attract' ? html`<${Attract} state=${state} />` : html`
        <${Panel} view=${dashboard} players=${players} width=${1920} shown=${!!dashboard && !launch} side=${0} />`}
      ${launched && html`<${Launch} game=${launched} percent=${launch.percent} />`}
      ${preview && html`<view style=${{ ...fill, backgroundColor: black }}><image src=${preview} style=${fill} /></view>`}
      <${Panel} view=${guide} players=${players} width=${guideWidth} shown=${!!guide} side=${-guideWidth} />
      ${state.rewind.choosing && html`<${Rewind} rewind=${state.rewind} />`}
      ${picker && html`<view style=${{ ...fill, backgroundColor: black }}><${TitleBar} title=${picker.title} /><${List} view=${picker} /></view>`}
      <${Notifications} notifications=${state.notifications} />
    </view>`;
}

mount(App);
