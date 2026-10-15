'use strict';
const fs = require('node:fs/promises'); // 对照图、旧资源备份及JSON证据接口
const path = require('node:path'); // 工程内明确路径组合
const sharp = require('./deps.cjs').loadDependency('sharp'); // 诊断联系图与GIF编码接口
const { previewDelays } = require('./pack.cjs'); // 六十帧一秒GIF的真实延迟

/** 生成旧新步态诊断对照；root为正式资源目录，candidate为待人工复核的新六十帧目录。 */
async function review(root, candidate) {
  const qa = path.join(root, 'qa'); // 可提交的检查证据目录
  const before = path.join(qa, 'gait-v4-before'); // 本轮修复前的明确资源备份
  await fs.mkdir(before, { recursive: true });
  for (const [from, name] of [['atlases/side_slow_walk.png', 'side_slow_walk.png'], ['metadata/side_slow_walk.json', 'side_slow_walk.json'], ['previews/side_slow_walk.gif', 'side_slow_walk.gif']]) { // from为正式旧资源，name为备份名
    try { await fs.access(path.join(before, name)); }
    catch (error) { if (error.code !== 'ENOENT') throw error; await fs.copyFile(path.join(root, from), path.join(before, name)); } // error为备份存在性查询错误
  }
  const oldAtlas = await fs.readFile(path.join(before, 'side_slow_walk.png')); // 旧图集字节，只读保留原六十帧
  const pages = []; // 六十张诊断对照画面，绝不写入正式角色帧
  const keys = [0, 7, 15, 30, 45, 59]; // 对照图中的接触、下压、过腿、另一侧接触、过腿及闭合帧
  const contacts = []; // 联系图的旧新帧图层
  const header = Buffer.from('<svg width="512" height="40"><style>text{font:15px Arial;fill:#e8edf4}</style><text x="24" y="26">BEFORE</text><text x="280" y="26">AFTER / TWO STEPS</text></svg>'); // 诊断文字，来自代码而非美术资源
  for (let frame = 0; frame < 60; ++frame) { // 当前真实新旧同序号帧
    const previous = await sharp(oldAtlas).extract({ left: frame % 12 * 256, top: Math.floor(frame / 12) * 256, width: 256, height: 256 }).png().toBuffer(); // 图集中的旧帧，不改变原画
    const current = await fs.readFile(path.join(candidate, 'frames/side_slow_walk', String(frame).padStart(2, '0') + '.png')); // 候选新绘帧
    pages.push(await sharp({ create: { width: 512, height: 296, channels: 4, background: '#111c2b' } }).composite([{ input: header, left: 0, top: 0 }, { input: previous, left: 0, top: 40 }, { input: current, left: 256, top: 40 }]).png().toBuffer());
    const key = keys.indexOf(frame); // 当前帧在联系图中的列号，负值表示不采入
    if (key >= 0) { contacts.push({ input: previous, left: key * 256, top: 40 }, { input: current, left: key * 256, top: 336 }); }
  }
  await sharp(pages, { join: { animated: true } }).gif({ delay: previewDelays(1), loop: 0, keepDuplicateFrames: true, effort: 5 }).toFile(path.join(qa, 'gait-v4-comparison.gif'));
  const labels = Buffer.from('<svg width="1536" height="592"><style>text{font:16px Arial;fill:#e8edf4}</style><text x="20" y="27">BEFORE: near-static split legs</text><text x="20" y="324">AFTER: contact / down / pass / opposite contact / opposite pass / closing</text></svg>'); // 联系图文字图层
  await sharp({ create: { width: 1536, height: 592, channels: 4, background: '#111c2b' } }).composite([{ input: labels, left: 0, top: 0 }, ...contacts]).png().toFile(path.join(qa, 'gait-v4-keyframes.png'));
  console.log(JSON.stringify({ comparison: path.join(qa, 'gait-v4-comparison.gif'), keyframes: keys }));
}

if (require.main === module) {
  const root = path.resolve(__dirname, '../../图片/游戏素材/diluc-redrawn-60'); // 正式素材根目录
  review(root, path.resolve(process.argv[2] || path.join(root, '.staging/gait-v4'))).catch(error => { console.error(error); process.exitCode = 1; }); // error为对照资源或编码错误
}
